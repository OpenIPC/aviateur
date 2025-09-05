#include "player_rect.h"

#include "../gui_interface.h"

#ifdef AVIATEUR_USE_GSTREAMER
    #include "src/player/gst_decoder.h"
#endif

class SignalBar : public revector::ProgressBar {
    void custom_ready() override {
        theme_fg = {};
        theme_bg = {};
        theme_progress->border_width = 0;
        theme_progress->corner_radius = 0;

        set_lerp_enabled(true);
        set_label_visibility(false);
        set_fill_mode(FillMode::CenterToSide);
    }

    void custom_update(double dt) override {
        if (value < 0.333 * max_value) {
            theme_progress->bg_color = RED;
        }
        if (value > 0.333 * max_value && value < 0.6667 * max_value) {
            theme_progress->bg_color = YELLOW;
        }
        if (value > 0.6667 * max_value) {
            theme_progress->bg_color = GREEN;
        }
    }
};

void PlayerRect::show_red_tip(std::string tip) {
    tip_label_->theme_background.bg_color = RED;
    tip_label_->show_tip(tip);
}

void PlayerRect::show_green_tip(std::string tip) {
    tip_label_->theme_background.bg_color = GREEN;
    tip_label_->show_tip(tip);
}

void PlayerRect::custom_input(revector::InputEvent &event) {
    if (event.type == revector::InputEventType::Key) {
        auto key_args = event.args.key;

        if (key_args.key == revector::KeyCode::F11) {
            if (key_args.pressed) {
                fullscreen_button_->set_toggled(!fullscreen_button_->get_toggled());
            }
        }

        if (playing_ && key_args.key == revector::KeyCode::F10) {
            if (key_args.pressed) {
                record_button_->trigger();
            }
        }
    }
}

void PlayerRect::custom_ready() {
    auto onRtpStream = [this](std::string sdp_file) {
        playing_file_ = sdp_file;
        start_playing(sdp_file);
    };
    GuiInterface::Instance().rtpStreamCallbacks.emplace_back(onRtpStream);

    collapse_panel_ = std::make_shared<revector::CollapseContainer>(revector::CollapseButtonType::Default);
    collapse_panel_->set_title(FTR("player control"));
    collapse_panel_->set_collapse(true);
    collapse_panel_->set_color(revector::ColorU(106, 171, 114));
    collapse_panel_->set_anchor_flag(revector::AnchorFlag::TopRight);
    collapse_panel_->set_visibility(false);
    add_child(collapse_panel_);

    auto vbox = std::make_shared<revector::VBoxContainer>();
    collapse_panel_->add_child(vbox);

    logo_ = std::make_shared<revector::VectorImage>(revector::get_asset_dir("openipc-logo-white.svg"));
    texture = logo_;

    auto render_server = revector::RenderServer::get_singleton();
    player_ = std::make_shared<RealTimePlayer>(render_server->device_, render_server->queue_);

    render_image_ = std::make_shared<revector::RenderImage>(Pathfinder::Vec2I{1920, 1080});

#ifdef AVIATEUR_USE_GSTREAMER
    gst_decoder_ = std::make_shared<GstDecoder>();
    gst_decoder_->init();
#endif

    set_stretch_mode(StretchMode::KeepAspectCentered);

    tip_label_ = std::make_shared<TipLabel>();
    tip_label_->set_anchor_flag(revector::AnchorFlag::VCenterWide);
    tip_label_->set_visibility(false);
    tip_label_->set_word_wrap(true);
    add_child(tip_label_);

    hud_container_ = std::make_shared<revector::HBoxContainer>();
    add_child(hud_container_);
    hud_container_->set_size({0, 48});
    revector::StyleBox box;
    box.bg_color =
        GuiInterface::Instance().dark_mode_ ? revector::ColorU(27, 27, 27, 100) : revector::ColorU(228, 228, 228, 100);
    box.border_width = 0;
    box.corner_radius = 0;
    hud_container_->set_theme_bg(box);
    hud_container_->set_anchor_flag(revector::AnchorFlag::BottomWide);
    hud_container_->set_visibility(false);
    hud_container_->set_separation(16);

    {
        lq_bar_ = std::make_shared<SignalBar>();
        add_child(lq_bar_);
        lq_bar_->set_custom_minimum_size({0, 8});
        lq_bar_->set_visibility(false);
        lq_bar_->set_anchor_flag(revector::AnchorFlag::BottomWide);
    }

    {
        video_info_label_ = std::make_shared<revector::Label>();
        hud_container_->add_child(video_info_label_);
        video_info_label_->set_text("");
        video_info_label_->set_visibility(false);

        auto on_deocder_ready = [this](uint32_t width, uint32_t height, float fps) {
            std::stringstream ss;
            ss << width << "x" << height << "@" << int(round(fps));
            video_info_label_->set_text(ss.str());
            video_info_label_->set_visibility(true);
        };
        GuiInterface::Instance().decoderReadyCallbacks.emplace_back(on_deocder_ready);
    }

    bitrate_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(bitrate_label_);
    bitrate_label_->set_text(FTR("bit rate") + ": 0 bps");

    render_fps_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(render_fps_label_);

    hw_status_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(hw_status_label_);

#ifdef __linux__
    pl_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(pl_label_);
    fec_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(fec_label_);
#endif

    rx_status_update_timer = std::make_shared<revector::Timer>();
    add_child(rx_status_update_timer);

    auto callback = [this] {
        lq_bar_->set_value(GuiInterface::Instance().link_quality_);

#ifdef __linux__
        if (GuiInterface::Instance().is_using_wifi) {
            pl_label_->set_visibility(true);
            fec_label_->set_visibility(true);

            pl_label_->set_text(FTR("packet loss") + ": " +
                                std::format("{:.1f}", GuiInterface::Instance().packet_loss_) + "%");
            fec_label_->set_text("FEC: " + std::to_string(GuiInterface::Instance().drone_fec_level_));
        } else {
            pl_label_->set_visibility(false);
            fec_label_->set_visibility(false);
        }
#endif

        rx_status_update_timer->start_timer(0.1);
    };
    rx_status_update_timer->connect_signal("timeout", callback);
    rx_status_update_timer->start_timer(0.1);

    record_status_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(record_status_label_);
    record_status_label_->container_sizing.flag_h = revector::ContainerSizingFlag::ShrinkEnd;
    record_status_label_->set_text("");

    auto capture_button = std::make_shared<revector::Button>();
    vbox->add_child(capture_button);
    capture_button->set_text(FTR("capture frame"));
    auto icon = std::make_shared<revector::VectorImage>(revector::get_asset_dir("CaptureImage.svg"), true);
    capture_button->set_icon_normal(icon);
    auto capture_callback = [this] {
        auto output_file = player_->captureJpeg();
        if (output_file.empty()) {
            show_red_tip(FTR("capture fail"));
        } else {
            show_green_tip(FTR("frame saved") + output_file);
        }
    };
    capture_button->connect_signal("triggered", capture_callback);

    record_button_ = std::make_shared<revector::Button>();
    vbox->add_child(record_button_);
    auto icon2 = std::make_shared<revector::VectorImage>(revector::get_asset_dir("RecordVideo.svg"), true);
    record_button_->set_icon_normal(icon2);
    record_button_->set_text(FTR("record mp4") + " (F10)");

    auto record_button_raw = record_button_.get();
    auto record_callback = [record_button_raw, this] {
        if (!is_recording) {
            is_recording = player_->startRecord();

            if (is_recording) {
                record_button_raw->set_text(FTR("stop recording") + " (F10)");

                record_start_time = std::chrono::steady_clock::now();

                record_status_label_->set_text(FTR("recording") + ": 00:00");
            } else {
                record_status_label_->set_text("");
                show_red_tip(FTR("record fail"));
            }
        } else {
            is_recording = false;

            auto output_file = player_->stopRecord();

            record_button_raw->set_text(FTR("record mp4") + " (F10)");
            record_status_label_->set_text("");

            if (output_file.empty()) {
                show_red_tip(FTR("save record fail"));
            } else {
                show_green_tip(FTR("video saved") + output_file);
            }
        }
    };
    record_button_->connect_signal("triggered", record_callback);

    {
        auto button = std::make_shared<revector::CheckButton>();
        button->set_text(FTR("force sw decoding"));
        vbox->add_child(button);

        auto callback = [this](bool toggled) {
            force_software_decoding = toggled;
            if (playing_) {
                player_->stop();
                player_->play(playing_file_, force_software_decoding);
            }
        };
        button->connect_signal("toggled", callback);
    }

    // {
    //     video_stabilization_button_ = std::make_shared<revector::CheckButton>();
    //     video_stabilization_button_->set_text(FTR("video stab"));
    //     vbox->add_child(video_stabilization_button_);
    //
    //     auto callback = [this](bool toggled) {
    //         player_->yuvRenderer_->mStabilize = toggled;
    //         if (toggled) {
    //             show_red_tip(FTR("video stab warning"));
    //         }
    //     };
    //     video_stabilization_button_->connect_signal("toggled", callback);
    // }
    //
    // {
    //     low_light_enhancement_button_ = std::make_shared<revector::CheckButton>();
    //     low_light_enhancement_button_->set_text(FTR("low light enhancement"));
    //     vbox->add_child(low_light_enhancement_button_);
    //
    //     auto callback = [this](bool toggled) { player_->yuvRenderer_->mLowLightEnhancement = toggled; };
    //     low_light_enhancement_button_->connect_signal("toggled", callback);
    // }

    auto onBitrateUpdate = [this](uint64_t bitrate) {
        std::string text = FTR("bit rate") + ": ";
        if (bitrate > 1024 * 1024) {
            text += std::format("{:.1f}", bitrate / 1024.0 / 1024.0) + " Mbps";
        } else if (bitrate > 1024) {
            text += std::format("{:.1f}", bitrate / 1024.0) + " Kbps";
        } else {
            text += std::format("{:d}", bitrate) + " bps";
        }
        bitrate_label_->set_text(text);
    };
    GuiInterface::Instance().bitrateUpdateCallbacks.emplace_back(onBitrateUpdate);

    auto onTipUpdate = [this](std::string msg) { show_red_tip(msg); };
    GuiInterface::Instance().tipCallbacks.emplace_back(onTipUpdate);

    auto onUrlStreamShouldStop = [this] { stop_playing(); };
    GuiInterface::Instance().urlStreamShouldStopCallbacks.emplace_back(onUrlStreamShouldStop);
}

void PlayerRect::custom_update(double dt) {
    player_->update(dt);

    std::string decoder_name;
#ifdef AVIATEUR_USE_GSTREAMER
    if (GuiInterface::Instance().use_gstreamer_) {
        decoder_name = gst_decoder_->decoder_name_;
    } else
#endif
    {
        auto hw_decoder_name = player_->getHwDecoderName();
        decoder_name = hw_decoder_name.has_value() ? hw_decoder_name.value() : std::string(FTR("off"));
    }

    hw_status_label_->set_text(FTR("hw decoder") + ": " + decoder_name);

    if (!GuiInterface::Instance().use_gstreamer_) {
        bitrate_label_->set_visibility(true);
    } else {
        bitrate_label_->set_visibility(false);
    }

    render_fps_label_->set_text(FTR("render fps") + ": " +
                                std::to_string(revector::Engine::get_singleton()->get_fps_int()));

    if (is_recording) {
        std::chrono::duration<double, std::chrono::seconds::period> duration =
            std::chrono::steady_clock::now() - record_start_time;

        int total_seconds = duration.count();
        int hours = total_seconds / 3600;
        int minutes = (total_seconds % 3600) / 60;
        int seconds = total_seconds % 60;

        std::ostringstream ss;
        ss << FTR("recording") << ": ";
        if (hours > 0) {
            ss << hours << ":";
        }
        ss << std::setw(2) << std::setfill('0') << minutes << ":";
        ss << std::setw(2) << std::setfill('0') << seconds;

        record_status_label_->set_text(ss.str());
    }
}

void PlayerRect::custom_draw() {
    if (!playing_) {
        return;
    }
    auto render_image = (revector::RenderImage *)texture.get();

    if (!GuiInterface::Instance().use_gstreamer_) {
        player_->yuvRenderer_->render(render_image->get_texture());
    }
}

void PlayerRect::start_playing(const std::string &url) {
    playing_ = true;

#ifdef AVIATEUR_USE_GSTREAMER
    if (GuiInterface::Instance().use_gstreamer_) {
        if (url.starts_with("udp://")) {
            gst_decoder_->create_pipeline(GuiInterface::Instance().rtp_codec_);
            gst_decoder_->play_pipeline(url);
        } else {
            gst_decoder_->create_pipeline(GuiInterface::Instance().playerCodec);
            gst_decoder_->play_pipeline("");
        }

        collapse_panel_->set_visibility(false);

    } else
#endif
    {
        player_->play(url, force_software_decoding);
        texture = render_image_;
        collapse_panel_->set_visibility(true);
    }

    hud_container_->set_visibility(true);
    lq_bar_->set_visibility(true);
}

void PlayerRect::stop_playing() {
    playing_ = false;

    if (is_recording) {
        record_button_->trigger();
    }

#ifdef AVIATEUR_USE_GSTREAMER
    if (GuiInterface::Instance().use_gstreamer_) {
        gst_decoder_->stop_pipeline();
    } else
#endif
    {
        // Fix crash in WfbReceiver destructor.
        if (player_) {
            player_->stop();
        }
        texture = logo_;
        collapse_panel_->set_visibility(false);
    }

    hud_container_->set_visibility(false);
    lq_bar_->set_visibility(false);
}
