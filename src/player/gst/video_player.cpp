#ifdef AVIATEUR_USE_GSTREAMER
    #include "video_player.h"

    #include <gst/video/video-info.h>

    #include <future>

    #include "../../gui_interface.h"

VideoPlayerGst::VideoPlayerGst(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue)
    : VideoPlayer(device, queue) {
    gst_init(NULL, NULL);

    gst_debug_set_default_threshold(GST_LEVEL_WARNING);

    gst_decoder_ = std::make_shared<GstDecoder>();

    device_ = device;
    queue_ = queue;
}

VideoPlayerGst::~VideoPlayerGst() {
    stop();
    // Should never call this.
    // gst_deinit();
}

void VideoPlayerGst::update(float dt) {
    if (should_stop_playing_) {
        return;
    }

    GstSample *sample = gst_decoder_->try_pull_sample();
    if (sample) {
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstCaps *caps = gst_sample_get_caps(sample);

        // 1. Get GstVideoInfo (same as before)
        GstVideoInfo info;
        gst_video_info_from_caps(&info, caps);

        gint width = info.width;
        gint height = info.height;
        GstVideoFormat format = info.finfo->format;

        // g_print("Video Size: %d (w) x %d (h)\n", width, height);

        // Ensure format is NV12, or switch to handling I420 if requested
        if (format != GST_VIDEO_FORMAT_NV12) {
            // Handle error or unref and return
            g_printerr("Expected NV12 format, got %s\n", gst_video_format_to_string(format));
            gst_sample_unref(sample);
            return;
        }

        update_video_info(width, height, format);

        if (video_info_changed_) {
            yuvRenderer_->updateTextureInfoGst(video_width_, video_height_, info.finfo->format);
            video_info_changed_ = false;
        }

        // 2. Use GstVideoFrame to map and access planes
        GstVideoFrame vframe;
        // This maps the buffer and fills 'vframe' with info about planes, strides, etc.
        gboolean res = gst_video_frame_map(&vframe, &info, buffer, GST_MAP_READ);
        if (!res) {
            g_printerr("Could not map video frame data.\n");
            gst_sample_unref(sample);
            return;
        }

        yuvRenderer_->updateTextureDataGst(vframe);

        // 4. Unmap the video frame (this also unmaps the GstBuffer)
        gst_video_frame_unmap(&vframe);
        gst_sample_unref(sample);
    }

    if (video_info_changed_) {
        yuvRenderer_->updateTextureInfo(video_width_, video_height_, video_format_);
        video_info_changed_ = false;
    }
}

void VideoPlayerGst::render(std::shared_ptr<Pathfinder::Texture> target) {
    yuvRenderer_->render(target);
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
    should_stop_playing_ = true;

    gst_decoder_->stop_pipeline();
    gst_decoder_->destroy();
}

void VideoPlayerGst::set_muted(bool muted) {}

#endif
