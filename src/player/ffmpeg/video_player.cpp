#include "video_player.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

#include <future>
#include <sstream>

#include "../../gui_interface.h"
#include "jpeg_encoder.h"

#define DEFAULT_GIF_FRAMERATE 10

VideoPlayerFfmpeg::VideoPlayerFfmpeg(const std::shared_ptr<Pathfinder::Device> &device,
                                     const std::shared_ptr<Pathfinder::Queue> &queue)
    : VideoPlayer(device, queue) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        GuiInterface::Instance().PutLog(LogLevel::Warn, "SDL init audio failed!");
    }
}

void VideoPlayerFfmpeg::update(float dt) {
    if (should_stop_playing_) {
        return;
    }

    if (video_info_changed_) {
        yuvRenderer_->updateTextureInfo(video_width_, video_height_, video_format_);
        video_info_changed_ = false;
    }

    std::shared_ptr<AVFrame> frame = getFrame();
    if (frame && frame->linesize[0]) {
        yuvRenderer_->updateTextureData(frame);
    }
}

void VideoPlayerFfmpeg::render(std::shared_ptr<Pathfinder::Texture> target) {
    yuvRenderer_->render(target);
}

std::shared_ptr<AVFrame> VideoPlayerFfmpeg::getFrame() {
    std::lock_guard lck(mtx);

    // No frame in the queue
    if (videoFrameQueue.empty()) {
        return nullptr;
    }

    // Get a frame from the queue
    std::shared_ptr<AVFrame> frame = videoFrameQueue.front();

    // Remove the frame from the queue.
    videoFrameQueue.pop();

    lastFrame_ = frame;

    return frame;
}

void VideoPlayerFfmpeg::play(const std::string &playUrl, bool forceSoftwareDecoding) {
    stop(); // Ensure previous threads are joined and resources released

    should_stop_playing_ = false;
    has_emitted_ready_ = false;
    url = playUrl;

    decoder = std::make_shared<FfmpegDecoder>();

    analysisThread = std::thread([this, forceSoftwareDecoding, localDecoder = decoder] {
        bool ok = localDecoder->OpenInput(url, forceSoftwareDecoding);
        if (!ok) {
            GuiInterface::Instance().PutLog(LogLevel::Error, "Loading URL failed: {}", url);
            GuiInterface::Instance().ShowTip(FTR("failed to connect"), true);
            GuiInterface::Instance().EmitUrlStreamShouldStop();
            return;
        }

        current_decoder_name = localDecoder->hwDecoderName.has_value() ? localDecoder->hwDecoderName.value() : "Software";

        if (!isMuted && localDecoder->HasAudio()) {
            enableAudio();
        }

        // Bitrate callback.
        localDecoder->bitrateUpdateCallback = [](uint64_t bitrate) { GuiInterface::Instance().EmitBitrateUpdate(bitrate); };

        // Handle dynamic resolution change
        localDecoder->videoConfigChangedCallback = [this](int w, int h, AVPixelFormat fmt) {
            if (w > 0 && h > 0) {
                // If resolution changed, re-emit ready signal to update UI labels
                if (!has_emitted_ready_ || w != video_width() || h != video_height()) {
                    GuiInterface::Instance().EmitDecoderReady(w, h, decoder->GetFramerate(), current_decoder_name);
                    has_emitted_ready_ = true;
                }
                update_video_info(w, h, fmt);
            }
        };

        decodeThread = std::thread([this, forceSoftwareDecoding, localDecoder] {
            int readRetryCount = 0;
            int sendPacketErrorCount = 0;
            int consecutiveFrameCount = 0;
            bool isSignalLostNotified = false;

            while (!should_stop_playing_) {
                try {
                    // Getting frame.
                    auto frame = localDecoder->GetNextFrame();
                    if (!frame) {
                        consecutiveFrameCount = 0;
                        continue;
                    }

                    consecutiveFrameCount++;

                    // Success path: notify recovery only if we previously lost signal AND we have stable frames
                    if (consecutiveFrameCount >= 5) {
                        if (isSignalLostNotified) {
                            GuiInterface::Instance().ShowTip(FTR("signal restored"), false);
                            isSignalLostNotified = false;
                        }
                        readRetryCount = 0;
                        sendPacketErrorCount = 0;
                    }

                    // Sync video info and emit ready signal if not done yet
                    if (!has_emitted_ready_ && frame->width > 0 && frame->height > 0) {
                        GuiInterface::Instance().EmitDecoderReady(frame->width,
                                                                  frame->height,
                                                                  localDecoder->GetFramerate(),
                                                                  current_decoder_name);
                        update_video_info(frame->width, frame->height, localDecoder->GetVideoFrameFormat());
                        has_emitted_ready_ = true;
                    }

                    // Push frame to the buffer queue.
                    std::lock_guard lck(mtx);
                    if (videoFrameQueue.size() > 1) {
                        videoFrameQueue.pop();
                    }
                    videoFrameQueue.push(frame);
                }
                // Decoder error.
                catch (const SendPacketException &e) {
                    consecutiveFrameCount = 0;
                    has_emitted_ready_ = false;
                    GuiInterface::Instance().PutLog(LogLevel::Error, "Send packet failed: {}", e.what());

                    if (++sendPacketErrorCount > 10) {
                        GuiInterface::Instance().ShowTip(FTR("codec error, reconnecting..."), true);
                        isSignalLostNotified = true;

                        localDecoder->CloseInput();
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        if (localDecoder->OpenInput(url, forceSoftwareDecoding)) {
                            sendPacketErrorCount = 0;
                            readRetryCount = 0;
                        }
                    }
                }
                // Read frame error, mostly due to a lost signal.
                catch (const ReadFrameException &e) {
                    consecutiveFrameCount = 0;
                    has_emitted_ready_ = false;
                    GuiInterface::Instance().PutLog(LogLevel::Error, "Read frame failed: {}", e.what());

                    readRetryCount++;

                    if (!isSignalLostNotified) {
                        GuiInterface::Instance().ShowTip(FTR("no signal"), true);
                        isSignalLostNotified = true;
                    }

                    if (readRetryCount >= 2) {
                        // Re-open input to reset FFmpeg RTP state (SSRC, sequence numbers, etc.)
                        localDecoder->CloseInput();
                        std::this_thread::sleep_for(std::chrono::seconds(1));

                        if (localDecoder->OpenInput(url, forceSoftwareDecoding)) {
                            GuiInterface::Instance().PutLog(LogLevel::Info, "Input reopened successfully, waiting for data...");
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                // Break on other unknown errors.
                catch (const std::exception &e) {
                    GuiInterface::Instance().PutLog(LogLevel::Error, "Unknown error in decode thread: {}", e.what());
                    break;
                }
            }
        });
    });
}

void VideoPlayerFfmpeg::stop() {
    should_stop_playing_ = true;

    if (decoder) {
        decoder->abortRequest = true;
    }

    if (analysisThread.joinable()) {
        analysisThread.join();
    }
    if (decodeThread.joinable()) {
        decodeThread.join();
    }

    {
        std::lock_guard lck(mtx);
        videoFrameQueue = std::queue<std::shared_ptr<AVFrame>>();
    }

    // Do this before closing input.
    disableAudio();

    if (decoder) {
        decoder->CloseInput();
        decoder.reset();
    }
}

void VideoPlayerFfmpeg::set_muted(bool muted) {
    if (!decoder->HasAudio()) {
        return;
    }

    if (!muted && decoder) {
        decoder->ClearAudioBuff();

        if (!enableAudio()) {
            return;
        }
    } else {
        disableAudio();
    }

    isMuted = muted;
    // emit onMutedChanged(muted);
}

VideoPlayerFfmpeg::~VideoPlayerFfmpeg() {
    stop();

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::string VideoPlayerFfmpeg::capture_jpeg() {
    if (!lastFrame_) {
        return "";
    }

    auto dir = GuiInterface::GetCaptureDir();

    try {
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    std::stringstream filePath;
    filePath << dir;
    filePath << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()
             << ".jpg";

    std::ofstream outfile(filePath.str());
    outfile.close();

    auto ok = JpegEncoder::encodeJpeg(filePath.str(), lastFrame_);

    return ok ? std::string(filePath.str()) : "";
}

bool VideoPlayerFfmpeg::start_mp4_recording() {
    if (should_stop_playing_ && !lastFrame_) {
        return false;
    }

    auto dir = GuiInterface::GetCaptureDir();

    try {
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    std::stringstream filePath;
    filePath << dir;
    filePath << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()
             << ".mp4";

    std::ofstream outfile(filePath.str());
    outfile.close();

    mp4Encoder_ = std::make_shared<Mp4Encoder>(filePath.str());

    // Audio track not handled for now.
    if (decoder->HasAudio()) {
        mp4Encoder_->addTrack(decoder->pFormatCtx->streams[decoder->audioStreamIndex]);
    }

    // Add video track.
    if (decoder->HasVideo()) {
        mp4Encoder_->addTrack(decoder->pFormatCtx->streams[decoder->videoStreamIndex]);
    }

    if (!mp4Encoder_->start()) {
        return false;
    }

    // 设置获得NALU回调
    decoder->gotPktCallback = [this](const std::shared_ptr<AVPacket> &packet) {
        // 输入编码器
        mp4Encoder_->writePacket(packet, packet->stream_index == decoder->videoStreamIndex);
    };

    return true;
}

std::string VideoPlayerFfmpeg::stop_mp4_recording() const {
    if (!mp4Encoder_) {
        return {};
    }
    mp4Encoder_->stop();
    decoder->gotPktCallback = nullptr;

    return mp4Encoder_->saveFilePath_;
}

bool VideoPlayerFfmpeg::start_gif_recording() {
    if (should_stop_playing_) {
        return false;
    }

    if (!(decoder && decoder->HasVideo())) {
        return false;
    }

    std::stringstream gif_file_path;
    gif_file_path << "recording/";
    gif_file_path << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count()
                  << ".gif";

    gifEncoder_ = std::make_shared<GifEncoder>();

    if (!gifEncoder_->open(decoder->width,
                           decoder->height,
                           decoder->GetVideoFrameFormat(),
                           DEFAULT_GIF_FRAMERATE,
                           gif_file_path.str())) {
        return false;
    }

    // 设置获得解码帧回调
    decoder->gotVideoFrameCallback = [this](const std::shared_ptr<AVFrame> &frame) {
        if (!gifEncoder_) {
            return;
        }
        if (!gifEncoder_->isOpened()) {
            return;
        }
        // 根据GIF帧率跳帧
        uint64_t now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        if (gifEncoder_->getLastEncodeTime() + 1000 / gifEncoder_->getFrameRate() > now) {
            return;
        }

        gifEncoder_->encodeFrame(frame);
    };

    return true;
}

std::string VideoPlayerFfmpeg::stop_gif_recording() const {
    decoder->gotVideoFrameCallback = nullptr;
    if (!gifEncoder_) {
        return "";
    }
    gifEncoder_->close();
    return gifEncoder_->_saveFilePath;
}

std::shared_ptr<FfmpegDecoder> VideoPlayerFfmpeg::getDecoder() const {
    return decoder;
}

void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    if (additional_amount > 0) {
        Uint8 *data = SDL_stack_alloc(Uint8, additional_amount);
        if (data) {
            auto *player = static_cast<VideoPlayerFfmpeg *>(userdata);

            const bool ret = player->getDecoder()->ReadAudioBuff(data, additional_amount);

            if (ret) {
                SDL_PutAudioStreamData(stream, data, additional_amount);
                SDL_stack_free(data);
            }
        }
    }
}

bool VideoPlayerFfmpeg::enableAudio() {
    if (!decoder) {
        return false;
    }
    if (!decoder->HasAudio()) {
        return false;
    }
    if (stream) {
        GuiInterface::Instance().PutLog(LogLevel::Warn, "Audio stream already exists!");
        return false;
    }

    const SDL_AudioSpec spec = {SDL_AUDIO_S16, decoder->GetAudioChannelCount(), decoder->GetAudioSampleRate()};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, this);
    SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(stream));

    return true;
}

void VideoPlayerFfmpeg::disableAudio() {
    if (stream) {
        SDL_CloseAudioDevice(SDL_GetAudioStreamDevice(stream));
        stream = nullptr;
    }
}

bool VideoPlayerFfmpeg::hasAudio() const {
    if (!decoder) {
        return false;
    }

    return decoder->HasAudio();
}
