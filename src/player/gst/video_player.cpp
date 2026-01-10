#ifdef AVIATEUR_USE_GSTREAMER
    #include "video_player.h"

    #include <gst/app/app.h>
    #include <gst/video/video-info.h>

    #include <future>

    #include "../../gui_interface.h"

VideoPlayerGst::VideoPlayerGst(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue)
    : VideoPlayer(device, queue) {
    // Set GST_PLUGIN_PATH for release build.
    #if defined(NDEBUG) && defined(_WIN32)
    _putenv("GST_PLUGIN_PATH=./gstreamer-1.0/");
    #endif

    gst_init(NULL, NULL);

    gst_debug_set_default_threshold(GST_LEVEL_WARNING);

    gst_decoder_ = std::make_shared<GstDecoder>();

    device_ = device;
    queue_ = queue;
}

VideoPlayerGst::~VideoPlayerGst() {
    stop();

    if (prev_sample_) {
        gst_sample_unref(prev_sample_);
    }

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

        std::lock_guard lock(prev_sample_mutex_);

        if (prev_sample_) {
            gst_sample_unref(prev_sample_);
        }
        prev_sample_ = sample;
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

std::string VideoPlayerGst::capture_jpeg() {
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

    std::lock_guard lock(prev_sample_mutex_);

    // --- Start Sub-Pipeline for Encoding/Saving ---
    GstBuffer *buffer = gst_sample_get_buffer(prev_sample_);
    GstCaps *caps = gst_sample_get_caps(prev_sample_);

    // 1. Create the temporary pipeline
    gchar *pipe_str = g_strdup_printf("appsrc name=src caps=\"%s\" ! videoconvert ! jpegenc ! filesink location=\"%s\"",
                                      gst_caps_to_string(caps),
                                      filePath.str().c_str());

    GstElement *capture_pipe = gst_parse_launch(pipe_str, NULL);
    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(capture_pipe), "src");

    // 2. Set state to PLAYING
    gst_element_set_state(capture_pipe, GST_STATE_PLAYING);

    // 3. Push the buffer (must be a new reference)
    GstBuffer *buffer_ref = gst_buffer_copy_deep(buffer);
    gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer_ref);

    // 4. Send EOS and wait for file to be saved
    gst_app_src_end_of_stream(GST_APP_SRC(appsrc));

    // 5. Wait for file to be saved (Synchronous Bus Wait)
    GstBus *bus = gst_element_get_bus(capture_pipe);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus,
                                                 GST_CLOCK_TIME_NONE, // Wait indefinitely (or use a timeout like 500ms)
                                                 (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

    gboolean file_saved = FALSE;
    if (msg) {
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
            // File saved successfully
            file_saved = TRUE;
        } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            // Log the error
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);

    // 6. Cleanup (ONLY after waiting for EOS/Error)
    if (file_saved) {
        // GuiInterface::Instance().PutLog(LogLevel::Info, "Frame saved successfully.");
    }

    // 5. Cleanup
    gst_element_set_state(capture_pipe, GST_STATE_NULL);
    gst_object_unref(appsrc);
    gst_object_unref(capture_pipe);
    g_free(pipe_str);

    return dir;
}

bool VideoPlayerGst::start_mp4_recording() {
    const auto dir = GuiInterface::GetCaptureDir();

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

    record_filename_ = filePath.str();

    return gst_decoder_->start_recording(record_filename_, GuiInterface::Instance().rtp_codec_);
}

std::string VideoPlayerGst::stop_mp4_recording() const {
    gst_decoder_->stop_recording();

    return record_filename_;
}

std::string VideoPlayerGst::stop_gif_recording() const {
    gst_decoder_->stop_recording();

    return record_filename_;
}

#endif
