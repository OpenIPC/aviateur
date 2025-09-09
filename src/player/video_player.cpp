#include "video_player.h"

#include <future>

#include "../gui_interface.h"

VideoPlayer::VideoPlayer() {
    // If the decoder fails, try to replay.
    connectionLostCallbacks.emplace_back([this] {
        stop();
        play(url, force_sw_decoder_);
    });
}

void VideoPlayer::update_video_info(const int width, const int height, const int format) {
    if (video_width_ != width) {
        video_width_ = width;
        make_video_info_dirty(true);
    }
    if (video_height_ != height) {
        video_height_ = height;
        make_video_info_dirty(true);
    }
    if (video_format_ != format) {
        video_format_ = format;
        make_video_info_dirty(true);
    }
}

void VideoPlayer::force_sw_decoder(const bool force) {
    force_sw_decoder_ = force;
}

void VideoPlayer::emit_connection_lost() {
    for (auto &callback : connectionLostCallbacks) {
        try {
            callback();
        } catch (std::bad_any_cast &) {
            abort();
        }
    }
}
