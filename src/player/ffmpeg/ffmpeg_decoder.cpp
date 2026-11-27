#include "ffmpeg_decoder.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include "src/gui_interface.h"

#undef min
#undef max

namespace {
// Helper to find Annex B start codes (00 00 01 or 00 00 00 01)
const uint8_t *find_start_code(const uint8_t *p, const uint8_t *end) {
    while (p + 3 < end) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1) return p;
        if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) return p;
        p++;
    }
    return nullptr;
}

// Callback for AVIOContext to read from memory
int read_sdp(void *opaque, uint8_t *buf, int buf_size) {
    auto *state = static_cast<SdpReadState *>(opaque);
    if (state->sizeLeft == 0) return AVERROR_EOF;

    int read_len = std::min(buf_size, static_cast<int>(state->sizeLeft));
    memcpy(buf, state->ptr, read_len);
    state->ptr += read_len;
    state->sizeLeft -= read_len;
    return read_len;
}

// Make standalone hardware decoders output frames in system memory, like the software decoder.
AVPixelFormat select_sw_pix_fmt(AVCodecContext *, const AVPixelFormat *fmts) {
    for (const AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        if (desc && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
            return *p;
        }
    }
    return AV_PIX_FMT_NONE;
}

// Formats YuvRenderer::updateTextureInfo can render.
bool is_renderable_pix_fmt(const int fmt) {
    return fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P || fmt == AV_PIX_FMT_NV12 ||
           fmt == AV_PIX_FMT_YUV444P;
}
} // namespace

constexpr size_t MAX_AUDIO_PACKET = 2 * 1024 * 1024;
constexpr int DEFAULT_TIMEOUT_MS = 1500;
constexpr int AUDIO_FIFO_BUFFER_COUNT = 10; // Store up to 10 decoded audio frames

bool FfmpegDecoder::OpenInput(std::string &inputFile, bool forceSoftwareDecoding) {
#ifndef NDEBUG
    av_log_set_level(AV_LOG_ERROR);
#endif

    GuiInterface::Instance().PutLog(LogLevel::Info, "{}", __FUNCTION__);

    ResetHeaderState();

    CloseInput();

    forceSwDecoder = forceSoftwareDecoding;

    abortRequest = false;

    AVDictionary *options = nullptr;

    av_dict_set(&options, "buffer_size", "2097152", 0);
    av_dict_set(&options, "rtsp_transport", "udp", 0);
    av_dict_set(&options, "protocol_whitelist", "file,udp,tcp,rtp,rtmp,rtsp,http", 0);

    // Reduce latency
    av_dict_set(&options, "fflags", "nobuffer", 0);
    av_dict_set(&options, "flags", "low_delay", 0);

    // RTP specific low-latency and robustness
    av_dict_set(&options, "reorder_queue_size", "0", 0);
    av_dict_set(&options, "overrun_nonfatal", "1", 0);
    av_dict_set(&options, "max_delay", "0", 0);

    // Speed up stream analysis - balanced for HEVC
    av_dict_set(&options, "probesize", "512000", 0);
    av_dict_set(&options, "analyzeduration", "500000", 0);

    // Timeout control
    startTime = std::chrono::steady_clock::now();

    const AVInputFormat *format = nullptr;
    int ret = 0;

    // SDP string in memory.
    if (inputFile.starts_with("v=0")) {
        // Handle in-memory SDP
        sdpBuffer.assign(inputFile.begin(), inputFile.end());
        sdpReadState.ptr = sdpBuffer.data();
        sdpReadState.sizeLeft = sdpBuffer.size();

        constexpr int avio_ctx_buffer_size = 4096;
        auto *avio_ctx_buffer = static_cast<unsigned char *>(av_malloc(avio_ctx_buffer_size));
        pAvioCtx =
            avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0, &sdpReadState, &read_sdp, nullptr, nullptr);

        pFormatCtx = avformat_alloc_context();
        pFormatCtx->pb = pAvioCtx;
        pFormatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

        // Force SDP format
        format = av_find_input_format("sdp");
    } else { // SDP file on disk.
        pFormatCtx = avformat_alloc_context();
    }

    // Set interrupt callback before opening input to protect the connection phase and active playback
    pFormatCtx->interrupt_callback.callback = [](void *opaque) -> int {
        auto *decoder = reinterpret_cast<FfmpegDecoder *>(opaque);
        if (decoder->abortRequest) return 1;

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - decoder->startTime).count() >
            DEFAULT_TIMEOUT_MS) {
            return 1;
        }
        return 0;
    };
    pFormatCtx->interrupt_callback.opaque = this;

    if (inputFile.starts_with("v=0")) {
        ret = avformat_open_input(&pFormatCtx, nullptr, format, &options);
    } else {
        ret = avformat_open_input(&pFormatCtx, inputFile.c_str(), nullptr, &options);
    }
    av_dict_free(&options); // Free remaining options

    if (ret != 0) {
        std::string err_msg = (ret == AVERROR_EXIT)
                                  ? "ffmpeg open input timeout (" + std::to_string(DEFAULT_TIMEOUT_MS) + "ms)"
                                  : "avformat_open_input failed: " + std::to_string(ret);
        GuiInterface::Instance().PutLog(LogLevel::Error, err_msg, __FUNCTION__);
        CloseInput();
        return false;
    }

    ret = avformat_find_stream_info(pFormatCtx, nullptr);
    if (ret < 0) {
        std::string err_msg = (ret == AVERROR_EXIT)
                                  ? "ffmpeg find stream info timeout (" + std::to_string(DEFAULT_TIMEOUT_MS) + "ms)"
                                  : "avformat_find_stream_info failed: " + std::to_string(ret);
        GuiInterface::Instance().PutLog(LogLevel::Error, err_msg, __FUNCTION__);
        CloseInput();
        return false;
    }

    hasVideoStream = OpenVideo();
    hasAudioStream = OpenAudio();

    if (!hasVideoStream) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "No video stream found", __FUNCTION__);
        CloseInput();
        return false;
    }

    sourceIsOpened = true;
    lastCountBitrateTime = std::chrono::steady_clock::now();

    // Convert time base
    if (videoStreamIndex != -1) {
        videoFramerate = static_cast<float>(av_q2d(pFormatCtx->streams[videoStreamIndex]->r_frame_rate));
        videoBaseTime = av_q2d(pFormatCtx->streams[videoStreamIndex]->time_base);

        GuiInterface::Instance().PutLog(LogLevel::Info, "Video frame rate: {}", videoFramerate);
    }

    if (audioStreamIndex != -1) {
        audioBaseTime = av_q2d(pFormatCtx->streams[audioStreamIndex]->time_base);
    }

    // Create audio buffer (fixed capacity; old data is dropped when full)
    if (hasAudioStream) {
        size_t count = GetAudioFrameSamples() * GetAudioChannelCount() * AUDIO_FIFO_BUFFER_COUNT;
        audioFifoBuffer = av_fifo_alloc2(count, sizeof(uint8_t), 0);
    }

    return true;
}

bool FfmpegDecoder::CloseInput() {
    abortRequest = true;

    std::lock_guard lck1(_releaseLock);
    std::lock_guard lck2(_readMtx);

    GuiInterface::Instance().PutLog(LogLevel::Info, "{}", __FUNCTION__);

    sourceIsOpened = false;

    CloseVideo();
    CloseAudio();

    if (pFormatCtx) {
        avformat_close_input(&pFormatCtx);
        pFormatCtx = nullptr;
    }

    if (pAvioCtx) {
        av_freep(&pAvioCtx->buffer);
        avio_context_free(&pAvioCtx);
        pAvioCtx = nullptr;
    }

    return true;
}

void freeFrame(AVFrame *f) {
    av_frame_free(&f);
}

void freePkt(AVPacket *f) {
    av_packet_free(&f);
}

void freeSwrCtx(SwrContext *s) {
    swr_free(&s);
}

std::shared_ptr<AVFrame> FfmpegDecoder::GetNextFrame() {
    if (videoStreamIndex == -1 && audioStreamIndex == -1) {
        return nullptr;
    }

    if (!sourceIsOpened) {
        return nullptr;
    }

    // Outer loop: Keep reading packets (audio, empty, or incomplete video GOP) until a full video frame is ready.
    while (true) {
        // 1. First, try to receive a frame from the decoder (drain)
        {
            std::lock_guard lck(_releaseLock);
            if (!pFormatCtx || !sourceIsOpened) return nullptr;

            if (pVideoCodecCtx) {
                std::shared_ptr<AVFrame> latestVideoFrame = nullptr;
                // Inner loop: Drain all backlogged frames from the decoder buffer and keep only the most recent one.
                while (true) {
                    std::shared_ptr<AVFrame> pFrameVideo = std::shared_ptr<AVFrame>(av_frame_alloc(), &freeFrame);
                    AVFrame *frameToReceive = pFrameVideo.get();

#ifdef __APPLE__
                    const bool zeroCopyThisFrame = hwDecoderEnabled && mZeroCopyEnabled;
#else
                    const bool zeroCopyThisFrame = false;
#endif

                    if (hwDecoderEnabled && !zeroCopyThisFrame) {
                        if (!hwFrame) {
                            hwFrame = std::shared_ptr<AVFrame>(av_frame_alloc(), &freeFrame);
                        }
                        frameToReceive = hwFrame.get();
                    }

                    int ret = avcodec_receive_frame(pVideoCodecCtx, frameToReceive);
                    if (ret == 0) {
                        // FFmpeg silently falls back to software decoding when hwaccel initialization fails
                        // (e.g. a Vulkan device without VK_KHR_video_decode_queue). Switch to a standalone hardware
                        // decoder if one opens, otherwise to the software path.
                        if (hwDecoderEnabled && frameToReceive->format != hwPixFmt) {
                            GuiInterface::Instance().PutLog(LogLevel::Warn,
                                                            "Hardware decoder {} returned software frames",
                                                            hwDecoderName.value_or(""));
                            hwDecoderEnabled = false;
                            hwDecoderName = {};

                            if (AVCodecContext *ctx =
                                    openHwDecoder(pFormatCtx->streams[videoStreamIndex]->codecpar,
                                                  frameToReceive->width,
                                                  frameToReceive->height,
                                                  false)) {
                                av_frame_unref(frameToReceive);
                                avcodec_free_context(&pVideoCodecCtx);
                                av_buffer_unref(&hwDeviceCtx);
                                pVideoCodecCtx = ctx;
                                // The new decoder starts at the next keyframe.
                                isWaitingForKeyframe = true;
                                width = 0;
                                height = 0;
                                break;
                            }

                            GuiInterface::Instance().PutLog(LogLevel::Warn, "Falling back to software decoding");
                            GuiInterface::Instance().ShowTip("hw decoding failed", true);
                            if (frameToReceive != pFrameVideo.get()) {
                                av_frame_move_ref(pFrameVideo.get(), frameToReceive);
                                frameToReceive = pFrameVideo.get();
                            }
                            // Force a video config update, the frame format is no longer NV12.
                            width = 0;
                            height = 0;
                        }

                        // Standalone hardware decoders output whatever format the driver picks.
                        if (!hwDecoderEnabled && hwDecoderName && !is_renderable_pix_fmt(frameToReceive->format)) {
                            GuiInterface::Instance().PutLog(
                                LogLevel::Warn,
                                "Hardware decoder {} returned unsupported format {}, falling back to software decoding",
                                hwDecoderName.value(),
                                av_get_pix_fmt_name(static_cast<AVPixelFormat>(frameToReceive->format)));
                            GuiInterface::Instance().ShowTip("hw decoding failed", true);
                            hwDecoderName = {};
                            av_frame_unref(frameToReceive);
                            avcodec_free_context(&pVideoCodecCtx);
                            pVideoCodecCtx = openSwDecoder(pFormatCtx->streams[videoStreamIndex]->codecpar);
                            // The new decoder starts at the next keyframe.
                            isWaitingForKeyframe = true;
                            width = 0;
                            height = 0;
                            break;
                        }

                        // Check if resolution is valid
                        if (frameToReceive->width <= 0 || frameToReceive->height <= 0) {
                            av_frame_unref(frameToReceive);
                            continue;
                        }

                        // Check if resolution has changed or was initially unknown
                        if (frameToReceive->width != width || frameToReceive->height != height) {
                            width = frameToReceive->width;
                            height = frameToReceive->height;
                            GuiInterface::Instance().PutLog(LogLevel::Info,
                                                            "Video resolution updated: {}x{}",
                                                            width,
                                                            height);
                            if (videoConfigChangedCallback) {
                                videoConfigChangedCallback(width, height, GetVideoFrameFormat());
                            }
                        }

                        if (hwDecoderEnabled && !zeroCopyThisFrame) {
                            if (dropCurrentVideoFrame) {
                                dropCurrentVideoFrame = false;
                                continue;
                            }
                            if (av_hwframe_transfer_data(pFrameVideo.get(), hwFrame.get(), 0) < 0) {
                                GuiInterface::Instance().PutLog(LogLevel::Warn, "av_hwframe_transfer_data failed");
                                continue;
                            }
                            av_frame_copy_props(pFrameVideo.get(), hwFrame.get());
                        } else if (hwDecoderEnabled && zeroCopyThisFrame) {
                            if (dropCurrentVideoFrame) {
                                dropCurrentVideoFrame = false;
                                continue;
                            }
                            // Zero-copy path: pFrameVideo already contains the hardware-decoded frame
                            // No transfer needed - the CVPixelBuffer is directly accessible via data[3]
                        }
                        invokeGotVideoFrameCallback(pFrameVideo);

                        latestVideoFrame = pFrameVideo;
                        continue;
                    } else if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                        char errStr[AV_ERROR_MAX_STRING_SIZE];
                        av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
                        GuiInterface::Instance().PutLog(LogLevel::Error, "avcodec_receive_frame error: {}", errStr);
                        break;
                    }
                    break;
                }
                if (latestVideoFrame) {
                    return latestVideoFrame;
                }
            }
        }

        // 2. If no frame available, read a new packet
        auto packet = std::shared_ptr<AVPacket>(av_packet_alloc(), &freePkt);
        int ret = -1;
        {
            std::lock_guard lck_io(_readMtx);
            if (!pFormatCtx || !sourceIsOpened || abortRequest) return nullptr;
            ret = av_read_frame(pFormatCtx, packet.get());
        }

        if (ret < 0) {
            if (abortRequest) return nullptr;
            char errStr[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
            throw ReadFrameException("av_read_frame failed: " + std::string(errStr));
        }

        // Heartbeat: update startTime after every successful packet read to prevent timeout
        startTime = std::chrono::steady_clock::now();

        // Calculate bitrate
        {
            bytesSecond += packet->size;
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCountBitrateTime).count();
            if (duration >= 1000) {
                bitrate = bytesSecond.load() * 8 * 1000 / duration;
                bytesSecond = 0;
                emitBitrateUpdate(bitrate.load());
                lastCountBitrateTime = now;
            }
        }

        // 3. Handle video packet
        if (packet->stream_index == videoStreamIndex) {
            // Stability: check for SPS/PPS/IDR
            if (!parseNalUnits(packet.get())) {
                continue;
            }

            std::lock_guard lck(_releaseLock);
            if (!sourceIsOpened || !pVideoCodecCtx) return nullptr;

            invokeGotPktCallback(packet);
            ret = avcodec_send_packet(pVideoCodecCtx, packet.get());
            if (ret < 0) {
                char errStr[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
                throw SendPacketException("avcodec_send_packet failed: " + std::string(errStr));
            }
            // After sending a packet, loop back to try receive_frame
            continue;
        }

        // 4. Handle audio packet
        if (packet->stream_index == audioStreamIndex) {
            std::lock_guard lck(_releaseLock);
            if (!sourceIsOpened || !pAudioCodecCtx) return nullptr;

            invokeGotPktCallback(packet);

            if (audioDecodeBuffer.size() < MAX_AUDIO_PACKET) {
                audioDecodeBuffer.resize(MAX_AUDIO_PACKET);
            }
            if (const size_t nDecodedSize =
                    DecodeAudio(packet.get(), audioDecodeBuffer.data(), audioDecodeBuffer.size());
                nDecodedSize > 0) {
                writeAudioBuff(audioDecodeBuffer.data(), nDecodedSize);
            }

            if (!HasVideo()) return nullptr;
        }
    }
}

bool FfmpegDecoder::createHwCtx(AVCodecContext *ctx, const AVHWDeviceType type) {
    if (av_hwdevice_ctx_create(&hwDeviceCtx, type, nullptr, nullptr, 0) < 0) {
        return false;
    }
    ctx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);

    return true;
}

void FfmpegDecoder::SetGotPktCallback(std::function<void(const std::shared_ptr<AVPacket> &packet)> cb) {
    std::lock_guard<std::mutex> lock(callbackMtx);
    gotPktCallback = std::move(cb);
}

void FfmpegDecoder::SetGotVideoFrameCallback(std::function<void(const std::shared_ptr<AVFrame> &frame)> cb) {
    std::lock_guard<std::mutex> lock(callbackMtx);
    gotVideoFrameCallback = std::move(cb);
}

void FfmpegDecoder::invokeGotPktCallback(const std::shared_ptr<AVPacket> &packet) {
    std::function<void(const std::shared_ptr<AVPacket> &packet)> cb;
    {
        std::lock_guard<std::mutex> lock(callbackMtx);
        cb = gotPktCallback;
    }
    if (cb) {
        cb(packet);
    }
}

void FfmpegDecoder::invokeGotVideoFrameCallback(const std::shared_ptr<AVFrame> &frame) {
    std::function<void(const std::shared_ptr<AVFrame> &frame)> cb;
    {
        std::lock_guard<std::mutex> lock(callbackMtx);
        cb = gotVideoFrameCallback;
    }
    if (cb) {
        cb(frame);
    }
}

AVCodecContext *FfmpegDecoder::openHwDecoder(const AVCodecParameters *codecpar,
                                             const int frameWidth,
                                             const int frameHeight,
                                             const bool allowHwaccel) {
    const auto allocContext = [&](const AVCodec *codec) -> AVCodecContext * {
        AVCodecContext *ctx = avcodec_alloc_context3(codec);
        if (!ctx) {
            return nullptr;
        }

        if (avcodec_parameters_to_context(ctx, codecpar) < 0) {
            avcodec_free_context(&ctx);
            return nullptr;
        }

        if (ctx->width <= 0 || ctx->height <= 0) {
            ctx->width = frameWidth;
            ctx->height = frameHeight;
        }

        // Minimize decoding latency
        ctx->thread_count = 1;
        ctx->thread_type = FF_THREAD_SLICE;
        ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
#ifdef AV_CODEC_FLAG2_FAST
        ctx->flags2 |= AV_CODEC_FLAG2_FAST;
#endif
        return ctx;
    };

    const AVCodec *swCodec = allowHwaccel ? avcodec_find_decoder(codecpar->codec_id) : nullptr;
    for (int configIndex = 0; swCodec; configIndex++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(swCodec, configIndex);
        if (!config) {
            break;
        }

        if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
            continue;
        }

        const auto decoderName = std::string(av_hwdevice_get_type_name(config->device_type));
        GuiInterface::Instance().PutLog(LogLevel::Info, "Configuring hardware decoder: " + decoderName);

        std::ostringstream oss;
        oss << "Hardware acceleration pixel format: " << config->pix_fmt;
        GuiInterface::Instance().PutLog(LogLevel::Info, oss.str());

        AVCodecContext *ctx = allocContext(swCodec);
        if (!ctx) {
            continue;
        }

        if (!createHwCtx(ctx, config->device_type)) {
            GuiInterface::Instance().PutLog(LogLevel::Warn, "Creating hardware context failed");
            avcodec_free_context(&ctx);
            continue;
        }

        if (avcodec_open2(ctx, swCodec, nullptr) < 0) {
            GuiInterface::Instance().PutLog(LogLevel::Warn, "avcodec_open2 failed");
            avcodec_free_context(&ctx);
            av_buffer_unref(&hwDeviceCtx);
            continue;
        }

        hwDecoderEnabled = true;
        hwPixFmt = config->pix_fmt;
        hwDecoderType = config->device_type;
        hwDecoderName = decoderName;
        GuiInterface::Instance().PutLog(LogLevel::Info, "Using hardware decoder: {}", decoderName);
        return ctx;
    }

    void *iter = nullptr;
    const AVCodec *codec;
    while ((codec = av_codec_iterate(&iter))) {
        if (!av_codec_is_decoder(codec) || codec->id != codecpar->codec_id ||
            !(codec->capabilities & AV_CODEC_CAP_HARDWARE)) {
            continue;
        }

        GuiInterface::Instance().PutLog(LogLevel::Info, "Trying hardware decoder: {}", codec->name);

        AVCodecContext *ctx = allocContext(codec);
        if (!ctx) {
            continue;
        }

        ctx->get_format = select_sw_pix_fmt;

        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            GuiInterface::Instance().PutLog(LogLevel::Info, "Hardware decoder {} unavailable", codec->name);
            avcodec_free_context(&ctx);
            continue;
        }

        hwDecoderName = codec->name;
        GuiInterface::Instance().PutLog(LogLevel::Info, "Using hardware decoder: {}", codec->name);
        return ctx;
    }

    return nullptr;
}

AVCodecContext *FfmpegDecoder::openSwDecoder(const AVCodecParameters *codecpar) {
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        return nullptr;
    }

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        return nullptr;
    }

    if (avcodec_parameters_to_context(ctx, codecpar) < 0) {
        avcodec_free_context(&ctx);
        return nullptr;
    }

    // Minimize decoding latency
    ctx->thread_count = 1;
    ctx->thread_type = FF_THREAD_SLICE;
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
#ifdef AV_CODEC_FLAG2_FAST
    ctx->flags2 |= AV_CODEC_FLAG2_FAST;
#endif

    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        GuiInterface::Instance().PutLog(LogLevel::Warn, "avcodec_open2 failed");
        avcodec_free_context(&ctx);
        return nullptr;
    }

    return ctx;
}

bool FfmpegDecoder::OpenVideo() {
    bool res = false;

    if (pFormatCtx) {
        videoStreamIndex = -1;

        for (uint32_t i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                continue;
            }

            videoStreamIndex = i;

            const AVCodecID codecId = pFormatCtx->streams[i]->codecpar->codec_id;
            GuiInterface::Instance().PutLog(LogLevel::Info, "Video codec ID: {}", (int)codecId);

            const AVCodec *codec = avcodec_find_decoder(codecId);
            if (!codec) {
                continue;
            }

            GuiInterface::Instance().PutLog(LogLevel::Info, "Video codec name: {}", codec->long_name);

            hwDecoderEnabled = false;
            hwDecoderName = {};

            if (!forceSwDecoder) {
                // Log available hardware decoder types.
                AVHWDeviceType decoderType = AV_HWDEVICE_TYPE_NONE;
                while ((decoderType = av_hwdevice_iterate_types(decoderType)) != AV_HWDEVICE_TYPE_NONE) {
                    auto decoderName = std::string(av_hwdevice_get_type_name(decoderType));
                    GuiInterface::Instance().PutLog(LogLevel::Info, "Found hardware decoder: " + decoderName);
                }

                pVideoCodecCtx = openHwDecoder(pFormatCtx->streams[i]->codecpar, 0, 0, true);
                if (pVideoCodecCtx) {
                    width = pVideoCodecCtx->width;
                    height = pVideoCodecCtx->height;
                    res = true;
                    break;
                }

                GuiInterface::Instance().PutLog(LogLevel::Warn,
                                                "No valid hardware decoder found, disabling hardware decoding");
            } else {
                GuiInterface::Instance().PutLog(LogLevel::Info, "Software decoding is forced");
            }

            pVideoCodecCtx = openSwDecoder(pFormatCtx->streams[i]->codecpar);
            if (!pVideoCodecCtx) {
                continue;
            }

            width = pVideoCodecCtx->width;
            height = pVideoCodecCtx->height;
            res = true;
            break;
        }
    }

    if (!res) {
        CloseVideo();
    }

    return res;
}

bool FfmpegDecoder::OpenAudio() {
    bool res = false;

    if (pFormatCtx) {
        audioStreamIndex = -1;

        for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audioStreamIndex = i;

                if (const AVCodec *codec = avcodec_find_decoder(pFormatCtx->streams[i]->codecpar->codec_id)) {
                    pAudioCodecCtx = avcodec_alloc_context3(codec);
                    if (pAudioCodecCtx) {
                        if (avcodec_parameters_to_context(pAudioCodecCtx, pFormatCtx->streams[i]->codecpar) >= 0) {
                            res = avcodec_open2(pAudioCodecCtx, codec, nullptr) >= 0;
                        }
                    }
                }

                break;
            }
        }

        if (!res) {
            CloseAudio();
        }
    }

    return res;
}

void FfmpegDecoder::CloseVideo() {
    if (pVideoCodecCtx) {
        avcodec_free_context(&pVideoCodecCtx);
        pVideoCodecCtx = nullptr;
        videoStreamIndex = -1;
    }
    if (hwDeviceCtx) {
        av_buffer_unref(&hwDeviceCtx);
        hwDeviceCtx = nullptr;
    }
}

void FfmpegDecoder::CloseAudio() {
    ClearAudioBuff();

    if (pAudioCodecCtx) {
        avcodec_free_context(&pAudioCodecCtx);
        pAudioCodecCtx = nullptr;
        audioStreamIndex = -1;
    }
    swrCtx.reset();
    lastAudioFrameFormat = AV_SAMPLE_FMT_NONE;
}

size_t FfmpegDecoder::DecodeAudio(const AVPacket *av_pkt, uint8_t *pOutBuffer, size_t nOutBufferSize) {
    size_t decodedSize = 0;

    int ret = avcodec_send_packet(pAudioCodecCtx, av_pkt);
    if (ret < 0) {
        char errStr[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
        throw SendPacketException("avcodec_send_packet failed: " + std::string(errStr));
    }

    AVFrame *audioFrame = av_frame_alloc();
    if (!audioFrame) {
        throw std::runtime_error("Failed to allocate audio frame");
    }

    while (true) {
        if (decodedSize >= nOutBufferSize) {
            break;
        }

        uint8_t *pDest = pOutBuffer + decodedSize;
        const size_t remainingSpace = nOutBufferSize - decodedSize;

        bool shouldBreakLoop = false;

        ret = avcodec_receive_frame(pAudioCodecCtx, audioFrame);

        switch (ret) {
            case 0: {
                const int bytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
                const int nbChannels = pAudioCodecCtx->ch_layout.nb_channels;

                if (audioFrame->format != AV_SAMPLE_FMT_S16) {
                    // (Re)create swrCtx if missing or input format changed
                    if (!swrCtx || lastAudioFrameFormat != audioFrame->format) {
                        SwrContext *ptr = nullptr;
                        swr_alloc_set_opts2(&ptr,
                                            &pAudioCodecCtx->ch_layout,
                                            AV_SAMPLE_FMT_S16,
                                            pAudioCodecCtx->sample_rate,
                                            &pAudioCodecCtx->ch_layout,
                                            static_cast<AVSampleFormat>(audioFrame->format),
                                            pAudioCodecCtx->sample_rate,
                                            0,
                                            nullptr);

                        if (const int ret2 = swr_init(ptr); ret2 < 0) {
                            char errStr[AV_ERROR_MAX_STRING_SIZE];
                            av_strerror(ret2, errStr, AV_ERROR_MAX_STRING_SIZE);
                            swr_free(&ptr);
                            throw std::runtime_error("Decoding audio failed: " + std::string(errStr));
                        }
                        swrCtx = std::shared_ptr<SwrContext>(ptr, &freeSwrCtx);
                        lastAudioFrameFormat = static_cast<AVSampleFormat>(audioFrame->format);
                    }

                    // Limit output samples to available buffer space to avoid overflow
                    const int maxOutSamples = static_cast<int>(remainingSpace / (nbChannels * bytesPerSample));
                    const int samplesToConvert = std::min(audioFrame->nb_samples, maxOutSamples);
                    if (samplesToConvert <= 0) {
                        shouldBreakLoop = true;
                        break;
                    }

                    // Convert audio frame to S16 format
                    const int samples = swr_convert(swrCtx.get(),
                                                    &pDest,
                                                    samplesToConvert,
                                                    (const uint8_t **)audioFrame->data,
                                                    audioFrame->nb_samples);
                    if (samples < 0) {
                        char errStr[AV_ERROR_MAX_STRING_SIZE];
                        av_strerror(samples, errStr, AV_ERROR_MAX_STRING_SIZE);
                        GuiInterface::Instance().PutLog(LogLevel::Warn, "swr_convert failed: " + std::string(errStr));
                        shouldBreakLoop = true;
                        break;
                    }
                    decodedSize += static_cast<size_t>(samples) * nbChannels * bytesPerSample;
                } else {
                    // Copy S16 audio data directly
                    const int bufSize =
                        av_samples_get_buffer_size(nullptr, nbChannels, audioFrame->nb_samples, AV_SAMPLE_FMT_S16, 1);
                    if (bufSize < 0 || static_cast<size_t>(bufSize) > remainingSpace) {
                        GuiInterface::Instance().PutLog(LogLevel::Warn, "audio S16 copy overflow");
                        shouldBreakLoop = true;
                        break;
                    }
                    memcpy(pDest, audioFrame->data[0], static_cast<size_t>(bufSize));
                    decodedSize += static_cast<size_t>(bufSize);
                }
            } break;
            case AVERROR(EAGAIN):
            case AVERROR_EOF:
            case AVERROR_INVALIDDATA:
            default:
                shouldBreakLoop = true;
                break;
        }

        av_frame_unref(audioFrame);

        if (shouldBreakLoop) {
            break;
        }
    }

    av_frame_free(&audioFrame);

    return decodedSize;
}

void FfmpegDecoder::writeAudioBuff(const uint8_t *aSample, const size_t aSize) {
    std::lock_guard lck(abBuffMtx);

    if (!audioFifoBuffer) return;

    if (const size_t free_space = av_fifo_can_write(audioFifoBuffer); free_space < aSize) {
        // Drop old data to make room for new data.
        av_fifo_drain2(audioFifoBuffer, aSize - free_space);
    }

    if (const int ret = av_fifo_write(audioFifoBuffer, aSample, aSize); ret < 0) {
        GuiInterface::Instance().PutLog(LogLevel::Warn, "av_fifo_write failed!");
    }
}

bool FfmpegDecoder::ReadAudioBuff(uint8_t *aSample, const size_t aSize) {
    std::lock_guard lck(abBuffMtx);

    if (!audioFifoBuffer) return false;

    if (const size_t available_size = av_fifo_can_read(audioFifoBuffer); available_size < aSize) {
        // Not enough to read.
        return false;
    }
    const int ret = av_fifo_read(audioFifoBuffer, aSample, aSize);

    return ret >= 0;
}

void FfmpegDecoder::ClearAudioBuff() {
    std::lock_guard lck(abBuffMtx);

    if (audioFifoBuffer) {
        av_fifo_freep2(&audioFifoBuffer);
        audioFifoBuffer = nullptr;
    }
}

bool FfmpegDecoder::parseNalUnits(const AVPacket *pkt) {
    if (!pVideoCodecCtx) return true;

    const AVCodecID codecId = pVideoCodecCtx->codec_id;
    const uint8_t *data = pkt->data;
    const int size = pkt->size;

    bool containsIdr = false;

    // Fast check for Annex B start codes
    const uint8_t *p = data;
    const uint8_t *end = data + size;

    while (p < end) {
        p = find_start_code(p, end);
        if (!p) break;

        // Skip start code
        if (p[2] == 1)
            p += 3;
        else
            p += 4;

        if (p >= end) break;

        if (codecId == AV_CODEC_ID_H264) {
            int type = p[0] & 0x1F;
            if (type == 7) { // SPS
                hasSps = true;
            } else if (type == 8) { // PPS
                hasPps = true;
            } else if (type == 5) { // IDR
                containsIdr = true;
            }
        } else if (codecId == AV_CODEC_ID_HEVC) {
            int type = (p[0] >> 1) & 0x3F;
            if (type == 33) { // SPS
                hasSps = true;
            } else if (type == 34) { // PPS
                hasPps = true;
            } else if (type >= 16 && type <= 21) { // IRAP (IDR/CRA/BLA)
                containsIdr = true;
            }
        }
    }

    // Keyframe flag from FFmpeg is also a good indicator
    if (pkt->flags & AV_PKT_FLAG_KEY) {
        containsIdr = true;
    }

    // Logic:
    // 1. If we got IDR and we have SPS/PPS, we can stop waiting.
    if (containsIdr && hasSps && hasPps) {
        if (isWaitingForKeyframe) {
            GuiInterface::Instance().PutLog(LogLevel::Info, "SPS + PPS + Keyframe received. Starting decoder.");
        }
        isWaitingForKeyframe = false;
    }

    // 2. If we are still waiting for a keyframe, drop everything else.
    if (isWaitingForKeyframe) {
        // If it's a keyframe but missing SPS/PPS, we still drop it (or we can't decode it anyway)
        return false;
    }

    return true;
}
