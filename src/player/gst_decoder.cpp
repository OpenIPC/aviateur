#include "gst_decoder.h"

#ifdef AVIATEUR_ENABLE_GSTREAMER

    #include <gst/app/gstappsink.h>
    #include <gst/gl/gl.h>
    #include <gst/video/video.h>
    #include <pathfinder/gpu/gl/device.h>

    #include "src/gui_interface.h"

static gboolean gst_bus_cb(GstBus *bus, GstMessage *message, gpointer user_data) {
    GstBin *pipeline = GST_BIN(user_data);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *gerr;
            gchar *debug_msg;
            gst_message_parse_error(message, &gerr, &debug_msg);
            g_error("Error: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_WARNING: {
            GError *gerr;
            gchar *debug_msg;
            gst_message_parse_warning(message, &gerr, &debug_msg);
            g_warning("Warning: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_EOS: {
            g_error("Got EOS!");
        } break;
        default:
            break;
    }
    return TRUE;
}

struct MySample {
    uint32_t frame_texture_id;
    gint width;
    gint height;
    // uint32_t frame_texture_target;
    GstSample *sample;
};

void release_sample(MySample *sample) {
    gst_sample_unref(sample->sample);
    free(sample);
}

GstDecoder::~GstDecoder() {
    if (prev_sample_ != NULL) {
        release_sample(prev_sample_);
    }
}

void GstDecoder::init() {
    if (initialized_) {
        return;
    }

    initialized_ = true;

    // setenv("GST_DEBUG", "GST_TRACER:7", 1);
    // setenv("GST_TRACERS", "latency(flags=pipeline)", 1); // Latency
    // setenv("GST_DEBUG_FILE", "./latency.log", 1);

    gst_debug_set_default_threshold(GST_LEVEL_WARNING);

    gst_init(NULL, NULL);

    // display_ = gst_gl_display_new();

    // auto windows = revector::RenderServer::get_singleton()->window_builder_->get_window(0).lock();
    // windows->get_glfw_handle()
    // context_ = gst_gl_context_new_wrapped(display_, ::, gl_platform, gl_api);
    // gst_gl_context_activate(m_gl_context, TRUE);
    // gst_gl_context_fill_info(m_gl_context, nullptr);
}

static GstFlowReturn on_new_sample_cb(GstAppSink *appsink, gpointer user_data) {
    GstDecoder *decoder = (GstDecoder *)user_data;

    //	GstElement *webrtcbin = gst_bin_get_by_name(GST_BIN(sc->pipeline), "webrtc");
    //	GstPromise *promise = gst_promise_new_with_change_func((GstPromiseChangeFunc)on_stats, NULL, NULL);
    //	g_signal_emit_by_name(webrtcbin, "get-stats", NULL, promise);

    // TODO record the frame ID, get frame pose
    // struct timespec ts;
    // int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    // if (ret != 0) {
    //     // ALOGE("%s: clock_gettime failed, which is very bizarre.", __FUNCTION__);
    //     return GST_FLOW_ERROR;
    // }

    GstSample *prevSample = NULL;
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    g_assert_nonnull(sample);
    {
        GMutexLocker *locker = g_mutex_locker_new(&decoder->sample_mutex);
        prevSample = decoder->sample;
        decoder->sample = sample;
        // decoder->sample_decode_end_ts = ts;
        decoder->received_first_frame = true;
    }
    if (prevSample) {
        //		ALOGI("Discarding unused, replaced sample");
        gst_sample_unref(prevSample);
    }
    return GST_FLOW_OK;
}

// clang-format off
#define SINK_CAPS \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "              \
    "format = (string) RGBA, "                                          \
    "width = " GST_VIDEO_SIZE_RANGE ", "                                \
    "height = " GST_VIDEO_SIZE_RANGE ", "                               \
    "framerate = " GST_VIDEO_FPS_RANGE ", "                             \
    "texture-target = (string) { 2D, external-oes } "
// clang-format on

void GstDecoder::create_pipeline() {
    if (pipeline_) {
        return;
    }

    GError *error = NULL;

    std::string codec = "H264";
    std::string depay = "rtph264depay";
    if (!GuiInterface::Instance().playerCodec.empty()) {
        if (GuiInterface::Instance().playerCodec == "H265") {
            codec = "H265";
            depay = "rtph265depay";
        }
    }

    gchar *pipeline_str = g_strdup_printf(
        "udpsrc name=udpsrc "
        "caps=application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)%s ! "
        "rtpjitterbuffer latency=5 ! "
        "%s ! "
        "decodebin3 ! "
        "glsinkbin name=glsink sync=false",
        codec.c_str(),
        depay.c_str());

    pipeline_ = gst_parse_launch(pipeline_str, &error);

    g_assert_no_error(error);
    g_free(pipeline_str);

    GuiInterface::Instance().PutLog(LogLevel::Info, "GStreamer pipeline created successfully");

    {
        // We convert the string SINK_CAPS above into a GstCaps that elements below can understand.
        // the "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY ")," part of the caps is read :
        // video/x-raw(memory:GLMemory) and is really important for getting zero-copy gl textures.
        // It tells the pipeline (especially the decoder) that an internal android:Surface should
        // get created internally (using the provided gstgl contexts above) so that the appsink
        // can basically pull the samples out using an GLConsumer (this is just for context, as
        // all of those constructs will be hidden from you, but are turned on by that CAPS).
        GstCaps *caps = gst_caps_from_string(SINK_CAPS);

        // FRED: We create the appsink 'manually' here because glsink's ALREADY a sink and so if we stick
        //       glsinkbin ! appsink in our pipeline_string for automatic linking, gst_parse will NOT like this,
        //       as glsinkbin (a sink) cannot link to anything upstream (appsink being 'another' sink). So we
        //       manually link them below using glsinkbin's 'sink' pad -> appsink.
        appsink_ = gst_element_factory_make("appsink", NULL);
        g_object_set(appsink_,
                     // Set caps
                     "caps",
                     caps,
                     // Fixed size buffer
                     "max-buffers",
                     1,
                     // drop old buffers when queue is filled
                     "drop",
                     true,
                     // terminator
                     NULL);
        // Lower overhead than new-sample signal.
        GstAppSinkCallbacks callbacks{};
        callbacks.new_sample = on_new_sample_cb;
        gst_app_sink_set_callbacks(GST_APP_SINK(appsink_), &callbacks, this, NULL);
        received_first_frame = false;

        GstElement *glsinkbin = gst_bin_get_by_name(GST_BIN(pipeline_), "glsink");
        g_object_set(glsinkbin, "sink", appsink_, NULL);
        // Disable clock sync to reduce latency
        g_object_set(glsinkbin, "sync", FALSE, NULL);
    }

    GstBus *bus = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus, gst_bus_cb, pipeline_);
    gst_clear_object(&bus);
}

struct MySample *try_pull_sample(GstDecoder *dec, struct timespec *out_decode_end) {
    if (!dec->appsink_) {
        // Not setup yet.
        return NULL;
    }

    // We actually pull the sample in the new-sample signal handler,
    // so here we're just receiving the sample already pulled.
    GstSample *sample = NULL;
    struct timespec decode_end;
    {
        g_mutex_lock(&dec->sample_mutex);
        sample = dec->sample;
        dec->sample = NULL;
        decode_end = dec->sample_decode_end_ts;
        g_mutex_unlock(&dec->sample_mutex);
    }

    if (sample == NULL) {
        if (gst_app_sink_is_eos(GST_APP_SINK(dec->appsink_))) {
            // ALOGW("%s: EOS", __FUNCTION__);
            // TODO trigger teardown?
        }
        return NULL;
    }
    *out_decode_end = decode_end;

    // printf("GOT A SAMPLE!");
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);

    GstVideoInfo info;
    gst_video_info_from_caps(&info, caps);
    gint width = GST_VIDEO_INFO_WIDTH(&info);
    gint height = GST_VIDEO_INFO_HEIGHT(&info);
    //	ALOGI("%s: frame %d (w) x %d (h)", __FUNCTION__, width, height);

    // TODO: Handle resize?
    #if 0
	if (width != sc->width || height != sc->height) {
		sc->width = width;
		sc->height = height;
	}
    #endif

    auto *ret = (MySample *)calloc(1, sizeof(struct MySample));

    GstVideoFrame frame;
    GstMapFlags flags = (GstMapFlags)(GST_MAP_READ | GST_MAP_GL);
    gst_video_frame_map(&frame, &info, buffer, flags);
    ret->frame_texture_id = *(uint32_t *)frame.data[0];
    ret->width = width;
    ret->height = height;

    if (dec->context_ == NULL) {
        // ALOGI("%s: Retrieving the GStreamer EGL context", __FUNCTION__);
        /* Get GStreamer's gl context. */
        gst_gl_query_local_gl_context(dec->appsink_, GST_PAD_SINK, &dec->context_);

        /* Check if we have 2D or OES textures */
        GstStructure *s = gst_caps_get_structure(caps, 0);
        const gchar *texture_target_str = gst_structure_get_string(s, "texture-target");
        if (g_str_equal(texture_target_str, GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR)) {
            // dec->frame_texture_target = GL_TEXTURE_EXTERNAL_OES;
            printf("Got GL_TEXTURE_EXTERNAL_OES");
        } else if (g_str_equal(texture_target_str, GST_GL_TEXTURE_TARGET_2D_STR)) {
            // dec->frame_texture_target = GL_TEXTURE_2D;
            printf("Got GL_TEXTURE_2D instead of expected GL_TEXTURE_EXTERNAL_OES");
        } else {
            g_assert_not_reached();
        }
    }
    // ret->frame_texture_target = dec->frame_texture_target;

    GstGLSyncMeta *sync_meta = gst_buffer_get_gl_sync_meta(buffer);
    if (sync_meta) {
        /* MOSHI: the set_sync() seems to be needed for resizing */
        gst_gl_sync_meta_set_sync_point(sync_meta, dec->context_);
        gst_gl_sync_meta_wait(sync_meta, dec->context_);
    }

    gst_video_frame_unmap(&frame);
    // Move sample ownership into the return value
    ret->sample = sample;

    return ret;
}

void GstDecoder::play_pipeline(const std::string &uri) {
    GstElement *udpsrc = gst_bin_get_by_name(GST_BIN(pipeline_), "udpsrc");

    if (uri.empty()) {
        g_object_set(udpsrc, "port", GuiInterface::Instance().playerPort, NULL);
    } else {
        g_object_set(udpsrc, "uri", uri.c_str(), NULL);
    }

    gst_object_unref(udpsrc);

    g_assert(gst_element_set_state(pipeline_, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    GuiInterface::Instance().PutLog(LogLevel::Info, "GStreamer pipeline started playing");

    pipeline_is_running_ = true;
}

std::shared_ptr<Pathfinder::Texture> GstDecoder::pull_texture() {
    if (!pipeline_is_running_) {
        return nullptr;
    }

    timespec decode_end_time;
    MySample *sample = try_pull_sample(this, &decode_end_time);

    if (!sample) {
        return nullptr;
    }

    auto *device_gl = (Pathfinder::DeviceGl *)revector::RenderServer::get_singleton()->device_.get();

    Pathfinder::TextureDescriptor desc = {{sample->width, sample->height}, Pathfinder::TextureFormat::Rgba8Unorm};

    if (prev_sample_ != NULL) {
        release_sample(prev_sample_);
    }
    prev_sample_ = sample;

    return device_gl->wrap_texture(sample->frame_texture_id, desc);
}

void GstDecoder::stop_pipeline() {
    gst_element_send_event(pipeline_, gst_event_new_eos());

    // Wait for an EOS message on the pipeline bus.
    GstMessage *msg = gst_bus_timed_pop_filtered(GST_ELEMENT_BUS(pipeline_),
                                                 GST_SECOND * 1, // In case it's blocked forever
                                                 static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

    // TODO: should check if we got an error message here or an eos.
    (void)msg;
    if (msg) {
        gst_message_unref(msg);
    }

    // Completely stop the pipeline.
    gst_element_set_state(pipeline_, GST_STATE_NULL);

    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    pipeline_is_running_ = false;

    GuiInterface::Instance().PutLog(LogLevel::Info, "GStreamer pipeline stopped");
}

#endif
