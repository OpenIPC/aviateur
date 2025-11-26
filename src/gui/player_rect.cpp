#include "player_rect.h"

#include "../gui_interface.h"
#include "src/player/ffmpeg/video_player.h"
#ifdef AVIATEUR_USE_GSTREAMER
    #include "src/player/gst/video_player.h"
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
        play_url_ = sdp_file;
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

    render_image_ = std::make_shared<revector::RenderImage>(Pathfinder::Vec2I{1920, 1080});

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
        auto vbox_container = std::make_shared<revector::VBoxContainer>();
        add_child(vbox_container);
        vbox_container->set_separation(2);
        vbox_container->set_size({0, 0});
        vbox_container->set_anchor_flag(revector::AnchorFlag::BottomWide);

        lq_bars_.push_back(std::make_shared<SignalBar>());
        lq_bars_.push_back(std::make_shared<SignalBar>());

        for (const auto &bar : lq_bars_) {
            vbox_container->add_child(bar);
            bar->set_custom_minimum_size({0, 4});
            bar->set_size({0, 4});
            bar->set_visibility(false);
        }
    }

    {
        video_info_label_ = std::make_shared<revector::Label>();
        hud_container_->add_child(video_info_label_);
        video_info_label_->set_text("");
        video_info_label_->set_visibility(false);

        auto on_deocder_ready = [this](uint32_t width, uint32_t height, float fps, std::string decoder_name) {
            std::stringstream ss;
            ss << width << "x" << height << "@" << int(round(fps));
            video_info_label_->set_text(ss.str());
            video_info_label_->set_visibility(true);

            hw_status_label_->set_text(FTR("hw decoder") + ": " + decoder_name);
            hw_status_label_->set_visibility(true);
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
    hw_status_label_->set_visibility(false);

#ifndef _WIN32
    pl_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(pl_label_);
    fec_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(fec_label_);
#endif

    rx_status_update_timer = std::make_shared<revector::Timer>();
    add_child(rx_status_update_timer);

    auto callback = [this] {
        for (int i = 0; i != GuiInterface::Instance().links_.size(); ++i) {
            lq_bars_[i]->set_visibility(true);
            lq_bars_[i]->set_value(GuiInterface::Instance().links_[i]->get_link_quality());
        }

#ifndef _WIN32
        if (GuiInterface::Instance().is_using_wifi) {
            pl_label_->set_visibility(true);
            fec_label_->set_visibility(true);

            std::string pl_text;
            for (const auto &link : GuiInterface::Instance().links_) {
                pl_text += std::format(" {:.1f}%", link->get_packet_loss());
            }
            pl_label_->set_text(FTR("packet loss") + ":" + pl_text);

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
        auto output_file = player_->capture_jpeg();
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
            is_recording = player_->start_mp4_recording();

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

            auto output_file = player_->stop_mp4_recording();

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
                player_->play(play_url_, force_software_decoding);
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
    if (player_) {
        player_->update(dt);
    }

    render_fps_label_->set_text(FTR("render fps") + ": " +
                                std::to_string(revector::Engine::get_singleton()->get_fps_int()));

    if (is_recording) {
        std::chrono::duration<double> duration = std::chrono::steady_clock::now() - record_start_time;

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
    if (player_) {
        auto render_image = (revector::RenderImage *)texture.get();
        player_->render(render_image->get_texture());
    }
}

template <class DstType, class SrcType>
bool IsType(const SrcType *src) {
    return dynamic_cast<const DstType *>(src) != nullptr;
}

void PlayerRect::start_playing(const std::string &url) {
    if (playing_) {
        return;
    }

    playing_ = true;

    auto render_server = revector::RenderServer::get_singleton();

    bool recreate_player = true;
    if (player_) {
#ifdef AVIATEUR_USE_GSTREAMER
        const bool player_is_gst = dynamic_cast<const VideoPlayerGst *>(player_.get()) != nullptr;

        if (player_is_gst && GuiInterface::Instance().use_gstreamer_) {
            recreate_player = false;
        }

        if (!player_is_gst && !GuiInterface::Instance().use_gstreamer_) {
            recreate_player = false;
        }
#else
        recreate_player = false;
#endif
    }

    if (recreate_player) {
#ifdef AVIATEUR_USE_GSTREAMER
        if (GuiInterface::Instance().use_gstreamer_) {
            GuiInterface::Instance().PutLog(LogLevel::Info, "Creating video player (GStreamer)");
            player_ = std::make_shared<VideoPlayerGst>(render_server->device_, render_server->queue_);
        } else
#endif
        {
            GuiInterface::Instance().PutLog(LogLevel::Info, "Creating video player (FFmpeg)");
            player_ = std::make_shared<VideoPlayerFfmpeg>(render_server->device_, render_server->queue_);
        }
    }

    player_->play(url, force_software_decoding);

    if (GuiInterface::Instance().use_gstreamer_) {
        collapse_panel_->set_visibility(false);
    } else {
        texture = render_image_;
        collapse_panel_->set_visibility(true);
    }

    hud_container_->set_visibility(true);
}

void PlayerRect::stop_playing() {
    playing_ = false;

    if (is_recording) {
        record_button_->trigger();
    }

    // Fix crash in WfbReceiver destructor.
    if (player_) {
        player_->stop();
    }

    if (!GuiInterface::Instance().use_gstreamer_) {
        texture = logo_;
        collapse_panel_->set_visibility(false);
    }

    hud_container_->set_visibility(false);
}
