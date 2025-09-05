#include "gst_decoder.h"

#ifdef AVIATEUR_USE_GSTREAMER

    #include <gst/video/video.h>

    #include "src/gui_interface.h"

static gboolean gst_bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data) {
    GstBin *pipeline = GST_BIN(user_data);

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_QOS: {
        } break;
        case GST_MESSAGE_ERROR: {
            GError *gerr;
            gchar *debug_msg;
            gst_message_parse_error(msg, &gerr, &debug_msg);
            g_error("Error: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_WARNING: {
            GError *gerr;
            gchar *debug_msg;
            gst_message_parse_warning(msg, &gerr, &debug_msg);
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

/// This callback function is called when a new pad is created by decodebin3
static void on_decodebin3_pad_added(GstElement *decodebin, GstPad *pad, gpointer data) {
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) {
        return;
    }

    auto *self = (GstDecoder *)data;

    gchar *pad_name = gst_pad_get_name(pad);
    GuiInterface::Instance().PutLog(LogLevel::Info, "A new src pad with name '{}' was created on decodebin3", pad_name);
    g_free(pad_name);

    // Step 1: Get the target pad from the ghost pad
    GstPad *dec_pad = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));

    if (dec_pad) {
        const GstCaps *caps = NULL;

        // When using decodebin
        if (gst_pad_has_current_caps(pad)) {
            caps = gst_pad_get_current_caps(pad);
        }
        // When using decodebin3
        else {
            gst_print("Pad '%s' has no caps, use gst_pad_get_stream to get caps\n", GST_PAD_NAME(pad));

            GstStream *stream = gst_pad_get_stream(pad);
            caps = gst_stream_get_caps(stream);
            gst_clear_object(&stream);
        }

        const GstStructure *s = gst_caps_get_structure(caps, 0);

        gint width, height;
        gboolean res = gst_structure_get_int(s, "width", &width);
        if (!res) {
            g_print("Could not get width from caps.\n");
            return;
        }

        res = gst_structure_get_int(s, "height", &height);
        if (!res) {
            g_print("Could not get height from caps.\n");
            return;
        }

        gint numerator, denominator;
        res = gst_structure_get_fraction(s, "framerate", &numerator, &denominator);
        if (!res) {
            g_print("Could not get framerate from caps.\n");
            return;
        }

        // Step 2: Get the parent element from the target pad (the actual decoder)
        GstElement *decoder = gst_pad_get_parent_element(dec_pad);

        if (decoder) {
            gchar *decoder_name = gst_element_get_name(decoder);

            const gchar *type_name = G_OBJECT_TYPE_NAME(decoder);

            GuiInterface::Instance().PutLog(LogLevel::Info, "The actual decoder type is: {}", type_name);

            GuiInterface::Instance().EmitDecoderReady(width,
                                                      height,
                                                      (gdouble)numerator / denominator,
                                                      std::string(type_name));

            // Clean up references
            g_free(decoder_name);
            gst_object_unref(decoder);
        } else {
            g_print("Could not get the parent element of the target pad.\n");

            GuiInterface::Instance().EmitDecoderReady(width, height, (gdouble)numerator / denominator, "unknown");
        }

        gst_object_unref(dec_pad);
    } else {
        g_print("Could not get the target pad.\n");
    }
}

static GstPadProbeReturn bitrate_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);
    gsize bytes = gst_buffer_get_size(buffer);

    auto *calc = static_cast<BitrateCalculator *>(user_data);
    calc->feed_bytes(bytes);

    return GST_PAD_PROBE_OK;
}

BitrateCalculator::BitrateCalculator() {
    g_mutex_init(&mutex);
}

void BitrateCalculator::feed_bytes(guint64 bytes) {
    g_mutex_lock(&mutex);

    if (start_time == 0) {
        start_time = g_get_monotonic_time();
    }

    total_bytes += bytes;

    gint64 current_time = g_get_monotonic_time();
    gdouble elapsed_seconds = (gdouble)(current_time - start_time) / GST_MSECOND;

    if (elapsed_seconds >= 1) {
        const gdouble bitrate_bps = (gdouble)(total_bytes * 8) / elapsed_seconds;
        // g_print("Current Bitrate: %.2f kbps\n", bitrate_bps / 1000.0);

        total_bytes = 0;
        start_time = g_get_monotonic_time();

        if (bitrate_cb) {
            bitrate_cb(bitrate_bps);
        }
    }

    g_mutex_unlock(&mutex);
}

void GstDecoder::init() {
    if (initialized_) {
        return;
    }

    initialized_ = true;

    // setenv("GST_DEBUG", "GST_TRACER:7", 1);
    // setenv("GST_TRACERS", "latency(flags=pipeline)", 1); // Latency
    // setenv("GST_DEBUG_FILE", "./latency.log", 1);

    gst_init(NULL, NULL);

    gst_debug_set_default_threshold(GST_LEVEL_WARNING);

    bitrate_calculator_ = std::make_shared<BitrateCalculator>();

    bitrate_calculator_->bitrate_cb = [](const uint64_t bitrate) {
        GuiInterface::Instance().EmitBitrateUpdate(bitrate);
    };
}

void GstDecoder::create_pipeline(const std::string &codec) {
    if (pipeline_) {
        return;
    }

    GError *error = NULL;

    std::string depay = "rtph264depay";
    if (codec == "H265") {
        depay = "rtph265depay";
    }

    gchar *pipeline_str = g_strdup_printf(
        "udpsrc name=udpsrc "
        "caps=application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)%s ! "
        "rtpjitterbuffer latency=50 ! "
        "%s name=depay ! "
        "decodebin3 name=decbin ! "
        "autovideosink name=glsink sync=false",
        codec.c_str(),
        depay.c_str());

    pipeline_ = gst_parse_launch(pipeline_str, &error);

    g_assert_no_error(error);
    g_free(pipeline_str);

    GuiInterface::Instance().PutLog(LogLevel::Info, "GStreamer pipeline created successfully");

    GstBus *bus = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus, gst_bus_cb, pipeline_);
    gst_clear_object(&bus);

    {
        GstElement *decodebin3 = gst_bin_get_by_name(GST_BIN(pipeline_), "decbin");
        if (!decodebin3) {
            GuiInterface::Instance().PutLog(LogLevel::Error, "Could not find decodebin3 element");
            return;
        }

        g_signal_connect(decodebin3, "pad-added", G_CALLBACK(on_decodebin3_pad_added), this);

        gst_object_unref(decodebin3);
    }

    {
        GstPad *pad = gst_element_get_static_pad(gst_bin_get_by_name(GST_BIN(pipeline_), "depay"), "sink");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, bitrate_probe, bitrate_calculator_.get(), NULL);
        gst_object_unref(pad);
    }
}

void GstDecoder::play_pipeline(const std::string &uri) {
    GstElement *udpsrc = gst_bin_get_by_name(GST_BIN(pipeline_), "udpsrc");

    if (uri.empty()) {
        g_object_set(udpsrc, "port", GuiInterface::Instance().playerPort, NULL);
    } else {
        g_object_set(udpsrc, "uri", uri.c_str(), NULL);
    }

    gint buffer_size;
    g_object_get(G_OBJECT(udpsrc), "buffer-size", &buffer_size, NULL);
    GuiInterface::Instance().PutLog(LogLevel::Info, "udpsrc buffer-size: {} bytes", buffer_size);

    gst_object_unref(udpsrc);

    g_assert(gst_element_set_state(pipeline_, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    GuiInterface::Instance().PutLog(LogLevel::Info, "GStreamer pipeline started playing");
}

void GstDecoder::stop_pipeline() {
    if (!pipeline_) {
        return;
    }

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

    GuiInterface::Instance().PutLog(LogLevel::Info, "GStreamer pipeline stopped");
}

#endif
