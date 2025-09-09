#ifdef AVIATEUR_USE_GSTREAMER
    #include "video_player.h"

    #include <future>

    #include "../../gui_interface.h"

VideoPlayerGst::VideoPlayerGst(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue) {
    gst_init(NULL, NULL);

    gst_debug_set_default_threshold(GST_LEVEL_WARNING);

    gst_decoder_ = std::make_shared<GstDecoder>();
}

VideoPlayerGst::~VideoPlayerGst() {
    stop();
    gst_deinit();
}

void VideoPlayerGst::update(float dt) {
    if (should_stop_playing_) {
    }
}

void VideoPlayerGst::render(std::shared_ptr<Pathfinder::Texture> target) {}

void VideoPlayerGst::play(const std::string &playUrl, bool forceSoftwareDecoding) {
    should_stop_playing_ = false;

    url = playUrl;

    if (url.starts_with("udp://")) {
        gst_decoder_->create_pipeline(GuiInterface::Instance().rtp_codec_);
        gst_decoder_->play_pipeline(url);
    } else {
        gst_decoder_->create_pipeline(GuiInterface::Instance().playerCodec);
        gst_decoder_->play_pipeline("");
    }
}

void VideoPlayerGst::stop() {
    gst_decoder_->stop_pipeline();
    gst_decoder_->destroy();
}

void VideoPlayerGst::set_muted(bool muted) {}

#endif
