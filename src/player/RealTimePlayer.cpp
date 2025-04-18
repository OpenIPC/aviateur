﻿#include "RealTimePlayer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

#include <future>
#include <sstream>

#include "../gui_interface.h"
#include "JpegEncoder.h"

// GIF默认帧率
#define DEFAULT_GIF_FRAMERATE 10

RealTimePlayer::RealTimePlayer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue) {
    yuvRenderer_ = std::make_shared<YuvRenderer>(device, queue);
    yuvRenderer_->init();

    // If the decoder fails, try to replay.
    connectionLostCallbacks.push_back([this] {
        stop();
        play(url, forceSoftwareDecoding_);
    });

    SDL_Init(SDL_INIT_AUDIO);
}

void RealTimePlayer::update(float dt) {
    if (playStop) {
        return;
    }

    if (infoChanged_) {
        yuvRenderer_->updateTextureInfo(videoWidth_, videoHeight_, videoFormat_);
        infoChanged_ = false;
    }

    std::shared_ptr<AVFrame> frame = getFrame();
    if (frame && frame->linesize[0]) {
        yuvRenderer_->updateTextureData(frame);
    }
}

std::shared_ptr<AVFrame> RealTimePlayer::getFrame() {
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

void RealTimePlayer::onVideoInfoReady(int width, int height, int format) {
    if (videoWidth_ != width) {
        videoWidth_ = width;
        makeInfoDirty(true);
    }
    if (videoHeight_ != height) {
        videoHeight_ = height;
        makeInfoDirty(true);
    }
    if (videoFormat_ != format) {
        videoFormat_ = format;
        makeInfoDirty(true);
    }
}

void RealTimePlayer::play(const std::string &playUrl, bool forceSoftwareDecoding) {
    playStop = false;

    if (analysisThread.joinable()) {
        analysisThread.join();
    }

    url = playUrl;

    decoder = std::make_shared<FfmpegDecoder>();

    analysisThread = std::thread([this, forceSoftwareDecoding] {
        bool ok = decoder->OpenInput(url, forceSoftwareDecoding);
        if (!ok) {
            GuiInterface::Instance().PutLog(LogLevel::Error, "Loading URL failed");
            return;
        }

        GuiInterface::Instance().EmitDecoderReady(decoder->GetWidth(), decoder->GetHeight(), decoder->GetFps());

        if (!isMuted && decoder->HasAudio()) {
            enableAudio();
        }

        if (decoder->HasVideo()) {
            onVideoInfoReady(decoder->GetWidth(), decoder->GetHeight(), decoder->GetVideoFrameFormat());
        }

        // Bitrate callback.
        decoder->bitrateUpdateCallback = [](uint64_t bitrate) { GuiInterface::Instance().EmitBitrateUpdate(bitrate); };

        hwEnabled = decoder->hwDecoderEnabled;

        decodeThread = std::thread([this] {
            while (!playStop) {
                try {
                    // Getting frame.
                    auto frame = decoder->GetNextFrame();
                    if (!frame) {
                        continue;
                    }

                    // Push frame to the buffer queue.
                    std::lock_guard lck(mtx);
                    if (videoFrameQueue.size() > 10) {
                        videoFrameQueue.pop();
                    }
                    videoFrameQueue.push(frame);
                }
                // Decoder error.
                catch (const SendPacketException &e) {
                    GuiInterface::Instance().PutLog(LogLevel::Error, e.what());
                    GuiInterface::Instance().ShowTip(FTR("hw decoder error"));
                }
                // Read frame error, mostly due to a lost signal.
                catch (const ReadFrameException &e) {
                    GuiInterface::Instance().PutLog(LogLevel::Error, e.what());
                    GuiInterface::Instance().ShowTip(FTR("signal lost"));
                }
                // Break on other unknown errors.
                catch (const std::exception &e) {
                    GuiInterface::Instance().PutLog(LogLevel::Error, e.what());
                    break;
                }
            }
        });

        // Start decode thread.
        decodeThread.detach();
    });

    // Start analysis thread.
    analysisThread.detach();
}

void RealTimePlayer::stop() {
    playStop = true;

    if (decoder && decoder->pFormatCtx) {
        decoder->pFormatCtx->interrupt_callback.callback = [](void *) { return 1; };
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

    if (decoder) {
        decoder->CloseInput();
        decoder.reset();
    }

    disableAudio();
}

void RealTimePlayer::setMuted(bool muted) {
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

RealTimePlayer::~RealTimePlayer() {
    stop();

    SDL_Quit();
}

std::string RealTimePlayer::captureJpeg() {
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

bool RealTimePlayer::startRecord() {
    if (playStop && !lastFrame_) {
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

std::string RealTimePlayer::stopRecord() const {
    if (!mp4Encoder_) {
        return {};
    }
    mp4Encoder_->stop();
    decoder->gotPktCallback = nullptr;

    return mp4Encoder_->saveFilePath_;
}

int RealTimePlayer::getVideoWidth() const {
    if (!decoder) {
        return 0;
    }
    return decoder->width;
}

int RealTimePlayer::getVideoHeight() const {
    if (!decoder) {
        return 0;
    }
    return decoder->height;
}

void RealTimePlayer::forceSoftwareDecoding(bool force) {
    forceSoftwareDecoding_ = force;
}

bool RealTimePlayer::isHardwareAccelerated() const {
    return hwEnabled;
}

std::shared_ptr<FfmpegDecoder> RealTimePlayer::getDecoder() const {
    return decoder;
}

void RealTimePlayer::emitConnectionLost() {
    for (auto &callback : connectionLostCallbacks) {
        try {
            callback();
        } catch (std::bad_any_cast &) {
            abort();
        }
    }
}

void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    if (additional_amount > 0) {
        Uint8 *data = SDL_stack_alloc(Uint8, additional_amount);
        if (data) {
            auto *player = static_cast<RealTimePlayer *>(userdata);
            player->getDecoder()->ReadAudioBuff(data, additional_amount);

            SDL_PutAudioStreamData(stream, data, additional_amount);
            SDL_stack_free(data);
        }
    }
}

bool RealTimePlayer::enableAudio() {
    if (!decoder->HasAudio()) {
        return false;
    }

    const SDL_AudioSpec spec = {SDL_AUDIO_S16, decoder->GetAudioChannelCount(), decoder->GetAudioSampleRate()};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, this);
    SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(stream));

    return true;
}

void RealTimePlayer::disableAudio() {
    if (stream) {
        SDL_CloseAudioDevice(SDL_GetAudioStreamDevice(stream));
        stream = nullptr;
    }
}

bool RealTimePlayer::hasAudio() const {
    if (!decoder) {
        return false;
    }

    return decoder->HasAudio();
}

bool RealTimePlayer::startGifRecord() {
    if (playStop) {
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
    decoder->gotFrameCallback = [this](const std::shared_ptr<AVFrame> &frame) {
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

std::string RealTimePlayer::stopGifRecord() const {
    decoder->gotFrameCallback = nullptr;
    if (!gifEncoder_) {
        return "";
    }
    gifEncoder_->close();
    return gifEncoder_->_saveFilePath;
}
