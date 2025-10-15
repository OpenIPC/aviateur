#ifdef AVIATEUR_USE_GSTREAMER
    #include "video_player.h"

    #include <gst/video/video-info.h>

    #include <future>

    #include "../../gui_interface.h"

VideoPlayerGst::VideoPlayerGst(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue) {
    gst_init(NULL, NULL);

    gst_debug_set_default_threshold(GST_LEVEL_WARNING);

    gst_decoder_ = std::make_shared<GstDecoder>();

    device_ = device;
    queue_ = queue;
    fence_ = device->create_fence("VideoPlayerGst fence");
}

VideoPlayerGst::~VideoPlayerGst() {
    stop();
    gst_deinit();
}

void VideoPlayerGst::update(float dt) {
    if (should_stop_playing_) {
    }
}

void VideoPlayerGst::render(std::shared_ptr<Pathfinder::Texture> target) {
    GstSample *sample = gst_decoder_->try_pull_sample();
    if (sample) {
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstCaps *caps = gst_sample_get_caps(sample);

        GstVideoInfo info;
        gst_video_info_from_caps(&info, caps);
        gint width = GST_VIDEO_INFO_WIDTH(&info);
        gint height = GST_VIDEO_INFO_HEIGHT(&info);
        assert(width == target->get_size().x && height == target->get_size().y);

        // g_print("%s: frame %d (w) x %d (h)\n", __FUNCTION__, width, height);

        // Map the buffer to access the raw data (the bitmap)
        GstMapInfo map;
        gboolean res = gst_buffer_map(buffer, &map, GST_MAP_READ);
        if (!res) {
            g_printerr("Could not map buffer data.\n");
            gst_sample_unref(sample);
            return;
        }

        // --- Frame Data (Bitmap) is now accessible ---
        // 'map.data' is the raw pointer to the pixel data.
        // 'map.size' is the total size in bytes (width * height * 3 for BGR).
        // The format is RGBA (Red, Green, Blue, Alpha) bytes, 8 bits per channel.

        // g_print("Frame grabbed: W=%d, H=%d, Size=%zu bytes. Raw Data Pointer: %p\n", width, height, map.size,
        // map.data);

        auto encoder = device_->create_command_encoder("upload appsink sample");

        encoder->write_texture(target, {}, map.data);

        queue_->submit(encoder, nullptr);
        // queue_->submit_and_wait(encoder);

        // 4. Unmap and unreference the sample
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
    }
}

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
