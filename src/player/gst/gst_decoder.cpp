#ifdef AVIATEUR_USE_GSTREAMER

    #include "gst_decoder.h"

    #include <gst/app/gstappsink.h>
    #include <gst/gstsample.h>

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

GstDecoder::GstDecoder() {
    bitrate_calculator_ = std::make_shared<BitrateCalculator>();

    bitrate_calculator_->bitrate_cb = [](const uint64_t bitrate) {
        GuiInterface::Instance().EmitBitrateUpdate(bitrate);
    };

    g_mutex_init(&sample_mutex_);
}

GstDecoder::~GstDecoder() {
    destroy();
}

static GstFlowReturn on_new_sample_cb(GstAppSink *appsink, gpointer user_data) {
    auto *dec = (GstDecoder *)user_data;

    GstSample *sample = gst_app_sink_pull_sample(appsink);
    g_assert_nonnull(sample);

    GstSample *prev_sample = nullptr;

    // Update client sample
    {
        g_mutex_lock(&dec->sample_mutex_);
        prev_sample = dec->sample_;
        dec->sample_ = sample;
        g_mutex_unlock(&dec->sample_mutex_);
    }

    // Previous client sample is not used.
    if (prev_sample) {
        GuiInterface::Instance().PutLog(LogLevel::Info, "Discarding unused, replaced sample");
        gst_sample_unref(prev_sample);
    }

    return GST_FLOW_OK;
}

GstSample *GstDecoder::try_pull_sample() {
    if (!appsink_) {
        // Not setup yet.
        return NULL;
    }

    // We actually pull the sample in the new-sample signal handler,
    // so here we're just receiving the sample already pulled.
    GstSample *sample = NULL;
    {
        g_mutex_lock(&sample_mutex_);
        sample = sample_;
        sample_ = NULL;
        g_mutex_unlock(&sample_mutex_);
    }

    if (sample == NULL) {
        if (gst_app_sink_is_eos(GST_APP_SINK(appsink_))) {
            // TODO trigger teardown?
        }
        return NULL;
    }

    return sample;
}

gboolean check_pipeline_dot_data(GstElement *pipeline) {
    if (!pipeline) {
        return G_SOURCE_CONTINUE;
    }

    gchar *dot_data = gst_debug_bin_to_dot_data(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
    // Put you breakpoint here.
    g_free(dot_data);

    return G_SOURCE_CONTINUE;
}

void GstDecoder::create_pipeline(const std::string &codec) {
    if (pipeline_) {
        return;
    }

    GError *error = NULL;

    std::string depay = "rtph264depay";
    std::string sw_dec = "avdec_h264";
    if (codec == "H265") {
        depay = "rtph265depay";
        sw_dec = "avdec_h265";
    }

    gchar *pipeline_str = g_strdup_printf(
        "udpsrc name=udpsrc "
        "caps=application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)%s ! "
        "rtpjitterbuffer latency=10 ! "
        "%s name=depay ! "
        "tee name=tee ! "
        // "%sw_dec name=decbin max-threads=1 lowres=0 skip-frame=0 ! "
        "decodebin3 name=decbin ! "
        "videoconvert ! "
        "video/x-raw,format=NV12 ! "
        "appsink name=mysink max-buffers=1 drop=true",
        // "autovideosink name=glsink sync=false",
        codec.c_str(),
        depay.c_str());

    pipeline_ = gst_parse_launch(pipeline_str, &error);

    g_assert_no_error(error);
    g_free(pipeline_str);

    GuiInterface::Instance().PutLog(LogLevel::Info, "GStreamer pipeline created successfully");

    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline_), "mysink");
    if (appsink) {
        appsink_ = appsink;

        // Lower overhead than new-sample signal.
        GstAppSinkCallbacks callbacks = {};
        callbacks.new_sample = on_new_sample_cb;
        gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, this, NULL);

        gst_object_unref(appsink);
    }

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

    timeout_src_id_dot_data_ = g_timeout_add_seconds(3, G_SOURCE_FUNC(check_pipeline_dot_data), pipeline_);
}

void GstDecoder::play_pipeline(const std::string &uri) {
    GstElement *udp_src = gst_bin_get_by_name(GST_BIN(pipeline_), "udpsrc");

    if (uri.empty()) {
        g_object_set(udp_src, "port", GuiInterface::Instance().playerPort, NULL);
    } else {
        g_object_set(udp_src, "uri", uri.c_str(), NULL);
    }

    gint buffer_size;
    g_object_get(G_OBJECT(udp_src), "buffer-size", &buffer_size, NULL);
    GuiInterface::Instance().PutLog(LogLevel::Info, "udpsrc buffer-size: {} bytes", buffer_size);

    gst_object_unref(udp_src);

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

    GuiInterface::Instance().PutLog(LogLevel::Info, "GStreamer pipeline stopped");
}

void GstDecoder::destroy() {
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
}

bool GstDecoder::start_recording(const std::string &filename, std::string codec) {
    std::lock_guard lock(mutex_);

    // 1. Get the tee element
    GstElement *tee = gst_bin_get_by_name(GST_BIN(pipeline_), "tee");
    if (!tee) {
        return false;
    }

    // --- Create recording elements ---
    GstElement *queue = gst_element_factory_make("queue", "queue_rec"); // Renamed queue
    g_object_set(G_OBJECT(queue),
                 "max-size-time",
                 5 * GST_SECOND, // 5 seconds max buffer time
                 "leaky",
                 TRUE, // Drop old buffers when full
                 NULL);

    GstElement *parser =
        gst_element_factory_make((codec == "H265") ? "h265parse" : "h264parse", // ADDED: The parser is crucial
                                 "parser_rec");
    GstElement *mp4mux = gst_element_factory_make("mp4mux", "mp4mux_rec");
    GstElement *file_sink = gst_element_factory_make("filesink", "filesink_rec");

    if (!queue || !parser || !mp4mux || !file_sink) {
        // Handle element creation failure
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to create recording elements.");

        gst_object_unref(queue);
        gst_object_unref(parser);
        gst_object_unref(mp4mux);
        gst_object_unref(file_sink);

        gst_object_unref(tee);

        return false;
    }

    // 2. Add all new elements to the pipeline bin
    gst_bin_add_many(GST_BIN(pipeline_), queue, parser, mp4mux, file_sink, NULL);

    g_object_set(G_OBJECT(file_sink), "location", filename.c_str(), NULL);

    // Fragmented MP4 (fMP4)
    g_object_set(G_OBJECT(mp4mux), "fragment-duration", 500, NULL); // e.g., 500ms fragments

    // 3. Link the new elements: queue -> caps_filter -> parser -> muxer -> filesink

    if (!gst_element_link_many(queue, parser, mp4mux, file_sink, NULL)) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to link recording elements.");
        // Clean up elements that were added
        gst_bin_remove(GST_BIN(pipeline_), queue);
        gst_bin_remove(GST_BIN(pipeline_), parser);
        gst_bin_remove(GST_BIN(pipeline_), mp4mux);
        gst_bin_remove(GST_BIN(pipeline_), file_sink);

        // ... remove others ...
        gst_object_unref(queue);
        gst_object_unref(parser);
        gst_object_unref(mp4mux);
        gst_object_unref(file_sink);

        gst_object_unref(tee);

        return false;
    }

    // 4. Request a src pad from the tee and link it to the recording branch's queue sink pad
    GstPad *tee_src_pad = gst_element_request_pad_simple(tee, "src_%u");
    GstPad *queue_sink_pad = gst_element_get_static_pad(queue, "sink");

    // The linking process should happen atomically
    if (gst_pad_link(tee_src_pad, queue_sink_pad) != GST_PAD_LINK_OK) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to link tee to recording branch.");

        gst_object_unref(tee_src_pad);
        gst_object_unref(queue_sink_pad);

        // Clean up elements that were added
        gst_bin_remove(GST_BIN(pipeline_), queue);
        gst_bin_remove(GST_BIN(pipeline_), parser);
        gst_bin_remove(GST_BIN(pipeline_), mp4mux);
        gst_bin_remove(GST_BIN(pipeline_), file_sink);

        // ... remove others ...
        gst_object_unref(queue);
        gst_object_unref(parser);
        gst_object_unref(mp4mux);
        gst_object_unref(file_sink);

        gst_object_unref(tee);

        return false;
    }

    // 5. Set new elements to the same state as the main pipeline (usually PLAYING)
    // NOTE: This MUST happen *before* linking to the TEE in some scenarios to avoid deadlocks.
    // // Setting to PLAYING handles both READY and PAUSED state transitions.
    // GstState current_state;
    // gst_element_get_state(pipeline_, &current_state, NULL, GST_CLOCK_TIME_NONE);

    GstElement *elements[] = {queue, parser, mp4mux, file_sink};
    for (int i = 0; i < G_N_ELEMENTS(elements); ++i) {
        if (gst_element_set_state(elements[i], GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            // Log the failing element and clean up
            GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to set recording element state.");

            gst_object_unref(tee_src_pad);
            gst_object_unref(queue_sink_pad);

            // Clean up elements that were added
            gst_bin_remove(GST_BIN(pipeline_), queue);
            gst_bin_remove(GST_BIN(pipeline_), parser);
            gst_bin_remove(GST_BIN(pipeline_), mp4mux);
            gst_bin_remove(GST_BIN(pipeline_), file_sink);

            // ... remove others ...
            gst_object_unref(queue);
            gst_object_unref(parser);
            gst_object_unref(mp4mux);
            gst_object_unref(file_sink);

            gst_object_unref(tee);

            return false;
        }
    }

    // Move reference
    recording_tee_src_pad_ = tee_src_pad;

    // 6. Clean up references
    gst_object_unref(queue_sink_pad);
    gst_object_unref(tee);

    return true;
}

void GstDecoder::stop_recording() {
    std::lock_guard lock(mutex_);

    GstElement *tee = gst_bin_get_by_name(GST_BIN(pipeline_), "tee");
    GstElement *queue = gst_bin_get_by_name(GST_BIN(pipeline_), "queue_rec"); // Use the correct name
    GstElement *filesink = gst_bin_get_by_name(GST_BIN(pipeline_), "filesink_rec");

    if (!tee || !queue) {
        if (tee) {
            gst_object_unref(tee);
        }
        return;
    }

    // 1. Find the pad connecting the tee to the queue
    GstPad *queue_sink_pad = gst_element_get_static_pad(queue, "sink");
    if (!queue_sink_pad) {
        gst_object_unref(tee);
        return;
    }

    // 2. Unlink the pads (Crucial: stops data flow immediately)
    gst_pad_unlink(recording_tee_src_pad_, queue_sink_pad);

    // 3. Release the TEE's request pad (makes it available for future use)
    gst_element_release_request_pad(tee, recording_tee_src_pad_);

    gst_object_unref(recording_tee_src_pad_);
    recording_tee_src_pad_ = nullptr;

    // 4. Send EOS to the recording branch's first element (the queue)
    // This tells the muxer and filesink to finalize the file.
    gst_element_send_event(queue, gst_event_new_eos());
    // --- CRITICAL STEP 3: Wait for EOS Confirmation ---
    // We listen on the pipeline bus for the EOS message that came from the filesink element.
    GstBus *bus = gst_element_get_bus(pipeline_);
    GstMessage *msg = NULL;
    gboolean done = FALSE;

    // Listen for up to 5 seconds
    gint timeout_ms = 5000;

    while (!done &&
           (msg = gst_bus_timed_pop_filtered(bus, timeout_ms, (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)))) {
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT_CAST(filesink)) {
            // Found the EOS from the filesink! The file should be finalized.
            GuiInterface::Instance().PutLog(LogLevel::Info, "Filesink confirmed EOS. File finalized.");
            done = TRUE;
        } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            // An error occurred, file is probably corrupted
            GError *err = NULL;
            gchar *debug_info = NULL;
            gst_message_parse_error(msg, &err, &debug_info);
            GuiInterface::Instance().PutLog(LogLevel::Error,
                                            g_strdup_printf("Error during EOS wait: %s", err->message));
            g_clear_error(&err);
            g_free(debug_info);
            done = TRUE; // Stop waiting on error
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);

    if (!done) {
        GuiInterface::Instance().PutLog(LogLevel::Warn,
                                        "Timed out waiting for EOS confirmation. File may be incomplete.");
    }

    // --- Post-EOS Cleanup ---
    // Instead of waiting synchronously (which can block the main thread),
    // you should monitor the bus for an EOS message from the entire pipeline
    // OR set a timeout. For simplicity in this example, we proceed with cleanup,
    // but in a multithreaded app, you should monitor the bus in your main loop.

    // 5. Set the recording branch to NULL and remove it
    // NOTE: It is best practice to set all elements in the branch to NULL individually
    // or rely on GStreamer's state change to NULL propagation after EOS.
    // For now, setting the first element to NULL might work, but it's risky.

    // Set all elements to NULL state
    GstElement *parser = gst_bin_get_by_name(GST_BIN(pipeline_), "parser_rec");
    GstElement *mp4mux = gst_bin_get_by_name(GST_BIN(pipeline_), "mp4mux_rec");

    // Setting the branch to NULL state
    GstElement *elements[] = {queue, parser, mp4mux, filesink};
    for (int i = 0; i < G_N_ELEMENTS(elements); ++i) {
        if (gst_element_set_state(elements[i], GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
            GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to set recording element state.");
            // Log the failing element and clean up
            return;
        }
    }

    // Remove all elements from the bin
    gst_bin_remove(GST_BIN(pipeline_), queue);
    gst_bin_remove(GST_BIN(pipeline_), parser);
    gst_bin_remove(GST_BIN(pipeline_), mp4mux);
    gst_bin_remove(GST_BIN(pipeline_), filesink);

    // 6. Clean up references
    gst_object_unref(queue_sink_pad);
    gst_object_unref(tee);
    gst_object_unref(queue);
    gst_object_unref(parser);
    gst_object_unref(mp4mux);
    gst_object_unref(filesink);

    GuiInterface::Instance().PutLog(LogLevel::Info, "Recording stopped and elements removed.");
}

#endif
