#include "ffmpeg_decoder.h"

#include <cassert>
#include <iostream>
#include <vector>

#include "src/gui_interface.h"

constexpr size_t MAX_AUDIO_PACKET = 2 * 1024 * 1024;

bool FfmpegDecoder::OpenInput(std::string &inputFile, bool forceSoftwareDecoding) {
#ifndef NDEBUG
    av_log_set_level(AV_LOG_ERROR);
#endif

    GuiInterface::Instance().PutLog(LogLevel::Info, "{}", __FUNCTION__);

    CloseInput();

    forceSwDecoder = forceSoftwareDecoding;

    // Check if any hardware decoder exists.
    if (!forceSoftwareDecoding) {
        AVHWDeviceType decoderType = AV_HWDEVICE_TYPE_NONE;
        std::vector<AVHWDeviceType> supportedHwDevices;
        do {
            decoderType = av_hwdevice_iterate_types(decoderType);

            if (decoderType != AV_HWDEVICE_TYPE_NONE) {
                auto decoderName = std::string(av_hwdevice_get_type_name(decoderType));
                GuiInterface::Instance().PutLog(LogLevel::Info, "Found hardware decoder: " + decoderName);
                supportedHwDevices.push_back(decoderType);
            }
        } while (decoderType != AV_HWDEVICE_TYPE_NONE);
    }

    AVDictionary *options = nullptr;

    av_dict_set(&options, "preset", "ultrafast", 0);
    av_dict_set(&options, "tune", "zerolatency", 0);
    av_dict_set(&options, "buffer_size", "425984", 0);
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "protocol_whitelist", "file,udp,tcp,rtp,rtmp,rtsp,http", 0);

    // Reduce latency
    av_dict_set(&options, "fflags", "nobuffer", 0);
    av_dict_set(&options, "flags", "low_delay", 0);

    // av_dict_set(&options, "probesize", "10000000", 0); // Increase to 10 MB
    // av_dict_set(&options, "analyzeduration", "5000000", 0); // Increase to 5 seconds

    if (avformat_open_input(&pFormatCtx, inputFile.c_str(), nullptr, &options) != 0) {
        CloseInput();
        return false;
    }

    // Timeout
    static constexpr int timeout = 10;
    startTime = std::chrono::steady_clock::now();

    pFormatCtx->interrupt_callback.callback = [](void *timestamp) -> int {
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> duration =
            now - *(std::chrono::time_point<std::chrono::steady_clock> *)timestamp;
        return duration.count() > timeout;
    };
    pFormatCtx->interrupt_callback.opaque = &startTime;

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        CloseInput();
        return false;
    }

    // Timeout
    if (const std::chrono::duration<double> duration = std::chrono::steady_clock::now() - startTime;
        duration.count() > timeout) {
        CloseInput();
        return false;
    }
    pFormatCtx->interrupt_callback.callback = nullptr;
    pFormatCtx->interrupt_callback.opaque = nullptr;

    hasVideoStream = OpenVideo();
    hasAudioStream = OpenAudio();

    sourceIsOpened = true;

    // Convert time base
    if (videoStreamIndex != -1) {
        videoFramerate = static_cast<float>(av_q2d(pFormatCtx->streams[videoStreamIndex]->r_frame_rate));
        videoBaseTime = av_q2d(pFormatCtx->streams[videoStreamIndex]->time_base);

        GuiInterface::Instance().PutLog(LogLevel::Info, "Video framerate: {}", videoFramerate);
    }

    if (audioStreamIndex != -1) {
        audioBaseTime = av_q2d(pFormatCtx->streams[audioStreamIndex]->time_base);
    }

    // Create audio buffer
    if (hasAudioStream) {
        size_t count = GetAudioFrameSamples() * GetAudioChannelCount() * 10;
        audioFifoBuffer = av_fifo_alloc2(count, sizeof(uint8_t), AV_FIFO_FLAG_AUTO_GROW);
    }

    return true;
}

bool FfmpegDecoder::CloseInput() {
    std::lock_guard lck(_releaseLock);

    GuiInterface::Instance().PutLog(LogLevel::Info, "{}", __FUNCTION__);

    sourceIsOpened = false;

    CloseVideo();
    CloseAudio();

    if (pFormatCtx) {
        avformat_close_input(&pFormatCtx);
        pFormatCtx = nullptr;
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
    std::lock_guard lck(_releaseLock);

    std::shared_ptr<AVFrame> res;

    if (videoStreamIndex == -1 && audioStreamIndex == -1) {
        return res;
    }

    if (!sourceIsOpened) {
        return res;
    }

    while (true) {
        if (!pFormatCtx) {
            throw std::runtime_error("AVFormatContext is null");
        }

        auto timestamp = std::make_shared<revector::Timestamp>("Aviateur");
        timestamp->set_enabled(false);

        std::shared_ptr<AVPacket> packet = std::shared_ptr<AVPacket>(av_packet_alloc(), &freePkt);

        int ret = av_read_frame(pFormatCtx, packet.get());
        if (ret < 0) {
            char errStr[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);

            throw ReadFrameException("av_read_frame failed: " + std::string(errStr));
        }

        timestamp->record("av_read_frame");

        // Calculate bitrate
        {
            bytesSecond += packet->size;

            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
            if (now - lastCountBitrateTime >= 1000) {
                // 计算码率定时器
                bitrate = bytesSecond * 8 * 1000 / (now - lastCountBitrateTime);
                bytesSecond = 0;

                emitBitrateUpdate(bitrate);

                lastCountBitrateTime = now;
            }
        }

        // Handle video
        if (packet->stream_index == videoStreamIndex) {
            if (gotPktCallback) {
                gotPktCallback(packet);
            }

            timestamp->record("gotPktCallback");

            std::shared_ptr<AVFrame> pFrameVideo = std::shared_ptr<AVFrame>(av_frame_alloc(), &freeFrame);

            if (bool successful = DecodeVideo(packet.get(), pFrameVideo)) {
                res = pFrameVideo;
            }

            timestamp->record("DecodeVideo");

            // Trigger callback
            if (gotVideoFrameCallback) {
                gotVideoFrameCallback(pFrameVideo);
            }

            timestamp->record("gotVideoFrameCallback");
            timestamp->print();

            break;
        }

        // Handle audio
        if (packet->stream_index == audioStreamIndex) {
            if (gotPktCallback) {
                gotPktCallback(packet);
            }

            if (packet->dts != AV_NOPTS_VALUE) {
                constexpr int audioFrameSize = MAX_AUDIO_PACKET;
                auto pFrameAudio = std::shared_ptr<uint8_t>(new uint8_t[audioFrameSize]);

                if (const size_t nDecodedSize = DecodeAudio(packet.get(), pFrameAudio.get(), audioFrameSize);
                    nDecodedSize > 0) {
                    writeAudioBuff(pFrameAudio.get(), nDecodedSize);
                }
            }

            if (!HasVideo()) {
                return res;
            }
        }
    }
    return res;
}

bool FfmpegDecoder::createHwCtx(AVCodecContext *ctx, const AVHWDeviceType type) {
    if (av_hwdevice_ctx_create(&hwDeviceCtx, type, nullptr, nullptr, 0) < 0) {
        return false;
    }
    ctx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);

    return true;
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

            pVideoCodecCtx = avcodec_alloc_context3(codec);
            if (!pVideoCodecCtx) {
                continue;
            }

            hwDecoderEnabled = false;
            hwDecoderName = {};

            if (!forceSwDecoder) {
                for (int configIndex = 0;; configIndex++) {
                    const AVCodecHWConfig *config = avcodec_get_hw_config(codec, configIndex);
                    if (!config) {
                        break;
                    }

                    if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
                        hwDecoderEnabled = true;

                        hwPixFmt = config->pix_fmt;
                        hwDecoderType = config->device_type;

                        auto decoderName = std::string(av_hwdevice_get_type_name(hwDecoderType));
                        GuiInterface::Instance().PutLog(LogLevel::Info, "Configuring hardware decoder: " + decoderName);

                        std::ostringstream oss;
                        oss << "Hardware acceleration pixel format: " << hwPixFmt;
                        GuiInterface::Instance().PutLog(LogLevel::Info, oss.str());

                        hwDecoderEnabled = createHwCtx(pVideoCodecCtx, hwDecoderType);

                        if (!hwDecoderEnabled) {
                            GuiInterface::Instance().PutLog(LogLevel::Warn, "Creating hardware contex failed");
                            continue;
                        }

                        hwDecoderName = decoderName;
                        GuiInterface::Instance().PutLog(LogLevel::Info, "Using hardware decoder: {}", decoderName);

                        break;
                    }
                }

                if (!hwDecoderEnabled) {
                    GuiInterface::Instance().PutLog(LogLevel::Warn,
                                                    "No valid hardware decoder found, disabling hardware decoding");
                }
            } else {
                GuiInterface::Instance().PutLog(LogLevel::Info, "Software decoding is forced");
            }

            if (avcodec_parameters_to_context(pVideoCodecCtx, pFormatCtx->streams[i]->codecpar) >= 0) {
                res = avcodec_open2(pVideoCodecCtx, codec, nullptr) >= 0;
                if (res) {
                    width = pVideoCodecCtx->width;
                    height = pVideoCodecCtx->height;
                } else {
                    GuiInterface::Instance().PutLog(LogLevel::Warn, "avcodec_open2 failed");
                    continue;
                }
            }

            break;
        }
    }

    if (!res) {
        CloseVideo();
    }

    return res;
}

bool FfmpegDecoder::DecodeVideo(const AVPacket *av_pkt, std::shared_ptr<AVFrame> &pOutFrame) {
    bool res = false;

    if (pVideoCodecCtx && av_pkt && pOutFrame) {
        int ret = avcodec_send_packet(pVideoCodecCtx, av_pkt);
        if (ret < 0) {
            char errStr[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
            throw SendPacketException("avcodec_send_packet failed: " + std::string(errStr));
        }

        if (hwDecoderEnabled) {
            // Initialize the hardware frame.
            if (!hwFrame) {
                hwFrame = std::shared_ptr<AVFrame>(av_frame_alloc(), &freeFrame);
            }

            ret = avcodec_receive_frame(pVideoCodecCtx, hwFrame.get());
        } else {
            ret = avcodec_receive_frame(pVideoCodecCtx, pOutFrame.get());
        }

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No output available right now or end of stream
            res = false;
        } else if (ret < 0) {
            char errStr[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
            throw std::runtime_error("avcodec_receive_frame failed: " + std::string(errStr));
        } else {
            // Successfully decoded a frame
            res = true;
        }

        if (res && hwDecoderEnabled) {
            if (dropCurrentVideoFrame) {
                pOutFrame.reset();
                return false;
            }

            // Copy data from the hw surface to the out frame.
            ret = av_hwframe_transfer_data(pOutFrame.get(), hwFrame.get(), 0);

            if (ret < 0) {
                char errStr[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
                throw std::runtime_error("av_hwframe_transfer_data failed: " + std::string(errStr));
            }
        }
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
}

void FfmpegDecoder::CloseAudio() {
    ClearAudioBuff();

    if (pAudioCodecCtx) {
        avcodec_free_context(&pAudioCodecCtx);
        pAudioCodecCtx = nullptr;
        audioStreamIndex = -1;
    }
}

size_t FfmpegDecoder::DecodeAudio(const AVPacket *av_pkt, uint8_t *pOutBuffer, size_t nOutBufferSize) {
    size_t decodedSize = 0;

    int ret = avcodec_send_packet(pAudioCodecCtx, av_pkt);
    if (ret < 0) {
        char errStr[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
        throw SendPacketException("avcodec_send_packet failed: " + std::string(errStr));
    }

    while (true) {
        uint8_t *pDest = pOutBuffer + decodedSize;
        if (nOutBufferSize - decodedSize < 0) {
            break;
        }

        AVFrame *audioFrame = av_frame_alloc();
        if (!audioFrame) {
            throw std::runtime_error("Failed to allocate audio frame");
        }

        bool shouldBreakLoop = false;

        ret = avcodec_receive_frame(pAudioCodecCtx, audioFrame);

        switch (ret) {
            case 0: {
                if (audioFrame->format != AV_SAMPLE_FMT_S16) {
                    // Convert frame to AV_SAMPLE_FMT_S16 if needed
                    if (!swrCtx) {
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
                            throw std::runtime_error("Decoding audio failed: " + std::string(errStr));
                        }
                        swrCtx = std::shared_ptr<SwrContext>(ptr, &freeSwrCtx);
                    }

                    // Convert audio frame to S16 format
                    const int samples = swr_convert(swrCtx.get(),
                                                    &pDest,
                                                    audioFrame->nb_samples,
                                                    (const uint8_t **)audioFrame->data,
                                                    audioFrame->nb_samples);
                    const size_t sizeToDecode =
                        samples * pAudioCodecCtx->ch_layout.nb_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

                    decodedSize += sizeToDecode;
                } else {
                    // Copy S16 audio data directly
                    const size_t sizeToDecode = av_samples_get_buffer_size(nullptr,
                                                                           pAudioCodecCtx->ch_layout.nb_channels,
                                                                           audioFrame->nb_samples,
                                                                           AV_SAMPLE_FMT_S16,
                                                                           1);
                    memcpy(pDest, audioFrame->data[0], sizeToDecode);

                    decodedSize += sizeToDecode;
                }
            } break;
            case AVERROR(EAGAIN): {
                shouldBreakLoop = true;
            } break;
            case AVERROR_EOF: {
                shouldBreakLoop = true;
            } break;
            case AVERROR_INVALIDDATA: {
                shouldBreakLoop = true;
            } break;
            default: {
                shouldBreakLoop = true;
            } break;
        }

        av_frame_free(&audioFrame);

        if (shouldBreakLoop) {
            break;
        }
    }

    return decodedSize;
}

void FfmpegDecoder::writeAudioBuff(const uint8_t *aSample, const size_t aSize) {
    std::lock_guard lck(abBuffMtx);

    if (const size_t free_space = av_fifo_can_write(audioFifoBuffer); free_space < aSize) {
        // Drop old data.
        std::vector<uint8_t> tmp;
        tmp.resize(aSize);
        av_fifo_read(audioFifoBuffer, tmp.data(), aSize);
    }

    if (const int ret = av_fifo_write(audioFifoBuffer, aSample, aSize); ret < 0) {
        GuiInterface::Instance().PutLog(LogLevel::Warn, "av_fifo_write failed!");
    }
}

int FfmpegDecoder::ReadAudioBuff(uint8_t *aSample, const size_t aSize) {
    std::lock_guard lck(abBuffMtx);

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
        av_fifo_reset2(audioFifoBuffer);
        audioFifoBuffer = nullptr;
    }
}
