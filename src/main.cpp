﻿#include <glad/gl.h>
#include <nodes/ui/menu_button.h>
#include <servers/render_server.h>

#include "app.h"
#include "gui_interface.h"
#include "player/RealTimePlayer.h"
#include "resources/render_image.h"
#include "wifi/WFBReceiver.h"

constexpr auto DEFAULT_KEY_NAME = "gs.key";

static Flint::App *app;

class TipLabel : public Flint::Label {
public:
    float display_time = 1;
    float fade_time = 0.5;

    std::shared_ptr<Flint::Timer> display_timer;
    std::shared_ptr<Flint::Timer> fade_timer;

    void custom_ready() override {
        set_font_size(48);

        auto style_box = Flint::StyleBox();
        style_box.bg_color = Flint::ColorU(50, 50, 50, 100);
        style_box.corner_radius = 8;
        theme_background = style_box;

        set_text_style(Flint::TextStyle{Flint::ColorU(201, 79, 79)});

        display_timer = std::make_shared<Flint::Timer>();
        fade_timer = std::make_shared<Flint::Timer>();

        add_child(display_timer);
        add_child(fade_timer);

        auto callback = [this] { fade_timer->start_timer(fade_time); };
        display_timer->connect_signal("timeout", callback);

        auto callback2 = [this] { set_visibility(false); };
        fade_timer->connect_signal("timeout", callback2);
    }

    void custom_update(double dt) override {
        if (!fade_timer->is_stopped()) {
            alpha = fade_timer->get_remaining_time() / fade_time;
        }
    }

    void show_tip(std::string tip) {
        if (!display_timer->is_stopped()) {
            display_timer->stop();
        }
        if (!fade_timer->is_stopped()) {
            fade_timer->stop();
        }
        set_text(tip);
        set_visibility(true);
        alpha = 1;
        display_timer->start_timer(display_time);
    }
};

class MyRenderRect : public Flint::TextureRect {
public:
    std::shared_ptr<RealTimePlayer> player_;
    std::string playing_file_;
    bool playing_ = false;

    bool force_software_decoding = false;

    std::shared_ptr<Flint::VectorImage> logo_;
    std::shared_ptr<Flint::RenderImage> render_image_;

    std::shared_ptr<TipLabel> tip_label_;

    bool is_recording = false;

    std::chrono::time_point<std::chrono::steady_clock> record_start_time;

    std::shared_ptr<Flint::CollapseContainer> collapse_panel_;

    std::shared_ptr<Flint::HBoxContainer> hud_container_;

    std::shared_ptr<Flint::Label> record_status_label_;

    std::shared_ptr<Flint::Label> hw_status_label_;

    std::shared_ptr<Flint::Label> video_fps_label_;

    std::shared_ptr<Flint::Label> display_fps_label_;

    std::shared_ptr<Flint::Button> video_stabilization_button_;

    std::shared_ptr<Flint::Button> fullscreen_button_;

    std::shared_ptr<Flint::Button> record_button_;

    // Record when the signal had been lost.
    std::chrono::time_point<std::chrono::steady_clock> signal_lost_time_;

    void show_red_tip(std::string tip) {
        auto red = Flint::ColorU(201, 79, 79);
        tip_label_->set_text_style(Flint::TextStyle{red});
        tip_label_->show_tip(tip);
    }

    void show_green_tip(std::string tip) {
        auto green = Flint::ColorU(78, 135, 82);
        tip_label_->set_text_style(Flint::TextStyle{green});
        tip_label_->show_tip(tip);
    }

    void custom_input(Flint::InputEvent &event) override {
        auto input_server = Flint::InputServer::get_singleton();

        if (event.type == Flint::InputEventType::Key) {
            auto key_args = event.args.key;

            if (key_args.key == Flint::KeyCode::F11) {
                if (key_args.pressed) {
                    fullscreen_button_->press();
                }
            }

            if (key_args.key == Flint::KeyCode::R && input_server->is_key_pressed(Flint::KeyCode::LeftControl)) {
                if (key_args.pressed) {
                    record_button_->press();
                }
            }
        }
    }

    void custom_ready() override {
        collapse_panel_ = std::make_shared<Flint::CollapseContainer>();
        collapse_panel_->set_title("Player Control");
        collapse_panel_->set_collapse(true);
        collapse_panel_->set_color(Flint::ColorU(84, 138, 247));
        collapse_panel_->set_anchor_flag(Flint::AnchorFlag::TopRight);
        collapse_panel_->set_visibility(false);
        add_child(collapse_panel_);

        auto vbox = std::make_shared<Flint::VBoxContainer>();
        collapse_panel_->add_child(vbox);

        logo_ = std::make_shared<Flint::VectorImage>("assets/openipc-logo-white.svg");
        texture = logo_;

        auto render_server = Flint::RenderServer::get_singleton();
        player_ = std::make_shared<RealTimePlayer>(render_server->device_, render_server->queue_);

        render_image_ = std::make_shared<Flint::RenderImage>(Pathfinder::Vec2I{1920, 1080});

        set_stretch_mode(StretchMode::KeepAspectCentered);

        tip_label_ = std::make_shared<TipLabel>();
        tip_label_->set_anchor_flag(Flint::AnchorFlag::Center);
        tip_label_->set_visibility(false);
        tip_label_->set_text_style(Flint::TextStyle{Flint::ColorU::red()});
        add_child(tip_label_);

        hud_container_ = std::make_shared<Flint::HBoxContainer>();
        add_child(hud_container_);
        hud_container_->set_size({0, 48});
        Flint::StyleBox box;
        box.bg_color = Flint::ColorU(27, 27, 27, 27);
        box.border_width = 0;
        box.corner_radius = 0;
        hud_container_->set_theme_bg(box);
        hud_container_->set_anchor_flag(Flint::AnchorFlag::BottomWide);
        hud_container_->set_visibility(false);
        hud_container_->set_separation(16);

        auto bitrate_label = std::make_shared<Flint::Label>();
        hud_container_->add_child(bitrate_label);
        bitrate_label->set_text("Bitrate: 0 Kbps");
        bitrate_label->set_text_style(Flint::TextStyle{Flint::ColorU::white()});

        {
            video_fps_label_ = std::make_shared<Flint::Label>();
            hud_container_->add_child(video_fps_label_);
            video_fps_label_->set_text_style(Flint::TextStyle{Flint::ColorU::white()});
            video_fps_label_->set_text("Video FPS: ");

            auto onFpsUpdate = [this](float fps) {
                std::string text = "Video FPS: " + std::to_string(int(round(fps)));

                video_fps_label_->set_text(text);
            };
            GuiInterface::Instance().decoderReadyCallbacks.emplace_back(onFpsUpdate);
        }

        {
            display_fps_label_ = std::make_shared<Flint::Label>();
            hud_container_->add_child(display_fps_label_);
            display_fps_label_->set_text_style(Flint::TextStyle{Flint::ColorU::white()});
            display_fps_label_->set_text("Display FPS: ");
        }

        hw_status_label_ = std::make_shared<Flint::Label>();
        hud_container_->add_child(hw_status_label_);
        hw_status_label_->set_text_style(Flint::TextStyle{Flint::ColorU::white()});

        record_status_label_ = std::make_shared<Flint::Label>();
        hud_container_->add_child(record_status_label_);
        record_status_label_->container_sizing.expand_h = true;
        record_status_label_->container_sizing.flag_h = Flint::ContainerSizingFlag::ShrinkEnd;
        record_status_label_->set_text("Not recording");
        record_status_label_->set_text_style(Flint::TextStyle{Flint::ColorU::white()});

        auto capture_button = std::make_shared<Flint::Button>();
        vbox->add_child(capture_button);
        capture_button->set_text("Capture Frame");
        auto icon = std::make_shared<Flint::VectorImage>("assets/CaptureImage.svg");
        capture_button->set_icon_normal(icon);
        auto capture_callback = [this] {
            auto output_file = player_->captureJpeg();
            if (output_file.empty()) {
                show_red_tip("Failed to capture frame!");
            } else {
                show_green_tip("Frame saved to: " + output_file);
            }
        };
        capture_button->connect_signal("pressed", capture_callback);

        record_button_ = std::make_shared<Flint::Button>();
        vbox->add_child(record_button_);
        auto icon2 = std::make_shared<Flint::VectorImage>("assets/RecordVideo.svg");
        record_button_->set_icon_normal(icon2);
        record_button_->set_text("Record MP4 (CTRL+R)");

        auto record_button_raw = record_button_.get();
        auto record_callback = [record_button_raw, this] {
            if (!is_recording) {
                is_recording = player_->startRecord();

                if (is_recording) {
                    record_button_raw->set_text("Stop Recording (CTRL+R)");

                    record_start_time = std::chrono::steady_clock::now();

                    record_status_label_->set_text("Recording 00:00");
                } else {
                    record_status_label_->set_text("Not recording");
                    show_red_tip("Recording failed!");
                }
            } else {
                is_recording = false;

                auto output_file = player_->stopRecord();

                record_button_raw->set_text("Record MP4 (CTRL+R)");
                record_status_label_->set_text("Not Recording");

                if (output_file.empty()) {
                    show_red_tip("Failed to save the record file!");
                } else {
                    show_green_tip("Video saved to: " + output_file);
                }
            }
        };
        record_button_->connect_signal("pressed", record_callback);

        {
            video_stabilization_button_ = std::make_shared<Flint::CheckButton>();
            video_stabilization_button_->set_text("Video Stabilization");
            vbox->add_child(video_stabilization_button_);

            auto callback = [this](bool toggled) {
                player_->m_yuv_renderer->stabilize = toggled;
                if (toggled) {
                    show_red_tip("Video stabilization is experimental!");
                }
            };
            video_stabilization_button_->connect_signal("toggled", callback);
        }

        {
            auto button = std::make_shared<Flint::CheckButton>();
            button->set_text("Force Software Decoder");
            vbox->add_child(button);

            auto callback = [this](bool toggled) {
                force_software_decoding = toggled;
                player_->stop();
                player_->play(playing_file_, force_software_decoding);
            };
            button->connect_signal("toggled", callback);
        }

        auto onBitrateUpdate = [bitrate_label](uint64_t bitrate) {
            std::string text = "Bitrate: ";
            if (bitrate > 1000 * 1000) {
                text += std::format("{:.1f}", bitrate / 1000.0 / 1000.0) + " Mbps";
            } else if (bitrate > 1000) {
                text += std::format("{:.1f}", bitrate / 1000.0) + " Kbps";
            } else {
                text += std::format("{:d}", bitrate) + " bps";
            }
            bitrate_label->set_text(text);
        };
        GuiInterface::Instance().bitrateUpdateCallbacks.emplace_back(onBitrateUpdate);

        auto onTipUpdate = [this](std::string msg) { show_red_tip(msg); };
        GuiInterface::Instance().tipCallbacks.emplace_back(onTipUpdate);
    }

    void custom_update(double delta) override {
        player_->update(delta);

        hw_status_label_->set_text("Hw Decoding: " + std::string(player_->isHardwareAccelerated() ? "ON" : "OFF"));

        display_fps_label_->set_text("Display FPS: " + std::to_string(Flint::Engine::get_singleton()->get_fps_int()));

        if (is_recording) {
            std::chrono::duration<double, std::chrono::seconds::period> duration =
                std::chrono::steady_clock::now() - record_start_time;

            int total_seconds = duration.count();
            int hours = total_seconds / 3600;
            int minutes = (total_seconds % 3600) / 60;
            int seconds = total_seconds % 60;

            std::ostringstream ss;
            ss << "Recording ";
            if (hours > 0) {
                ss << hours << ":";
            }
            ss << std::setw(2) << std::setfill('0') << minutes << ":";
            ss << std::setw(2) << std::setfill('0') << seconds;

            record_status_label_->set_text(ss.str());
        }
    }

    void custom_draw() override {
        if (!playing_) {
            return;
        }
        auto render_image = (Flint::RenderImage *)texture.get();
        player_->m_yuv_renderer->render(render_image->get_texture(), video_stabilization_button_->get_pressed());
    }

    // When connected.
    void start_playing(std::string url) {
        playing_ = true;
        player_->play(url, force_software_decoding);
        texture = render_image_;

        collapse_panel_->set_visibility(true);
        hud_container_->set_visibility(true);
    }

    // When disconnected.
    void stop_playing() {
        playing_ = false;
        // Fix crash in WFBReceiver destructor.
        if (player_) {
            player_->stop();
        }
        texture = logo_;

        collapse_panel_->set_visibility(false);
        hud_container_->set_visibility(false);
    }
};

class MyControlPanel : public Flint::Panel {
public:
    std::shared_ptr<Flint::MenuButton> dongle_menu_button_;
    std::shared_ptr<Flint::MenuButton> channel_button_;
    std::shared_ptr<Flint::MenuButton> channel_width_button_;
    std::shared_ptr<Flint::Button> refresh_dongle_button_;

    std::string vidPid;
    int channel = 173;
    int channelWidthMode = 0;
    std::string keyPath = DEFAULT_KEY_NAME;
    std::string codec = "AUTO";

    std::shared_ptr<Flint::Button> play_button_;

    void update_dongle_list() {
        auto menu = dongle_menu_button_->get_popup_menu().lock();

        auto dongles = GuiInterface::GetDongleList();

        menu->clear_items();

        bool previous_device_exists = false;
        for (const auto &dongle : dongles) {
            if (vidPid == dongle) {
                previous_device_exists = true;
            }
            menu->create_item(dongle);
        }

        if (!previous_device_exists) {
            vidPid = "";
        }
    }

    void update_start_button_looking(bool start_status) {
        if (!start_status) {
            auto red = Flint::ColorU(201, 79, 79);
            play_button_->theme_normal.bg_color = red;
            play_button_->theme_hovered.bg_color = red;
            play_button_->theme_pressed.bg_color = red;
            play_button_->set_text("Stop");
        } else {
            auto green = Flint::ColorU(78, 135, 82);
            play_button_->theme_normal.bg_color = green;
            play_button_->theme_hovered.bg_color = green;
            play_button_->theme_pressed.bg_color = green;
            play_button_->set_text("Start");
        }
    }

    void custom_ready() override {
        if (GuiInterface::Instance().config_file_exists) {
            vidPid = toolkit::mINI::Instance()[CONFIG_DEVICE];
            channel = toolkit::mINI::Instance()[CONFIG_CHANNEL];
            channelWidthMode = toolkit::mINI::Instance()[CONFIG_CHANNEL_WIDTH_MODE];
            keyPath = toolkit::mINI::Instance()[CONFIG_CHANNEL_KEY];
            codec = toolkit::mINI::Instance()[CONFIG_CHANNEL_CODEC];
        }

        auto margin_container = std::make_shared<Flint::MarginContainer>();
        margin_container->set_margin_all(8);
        margin_container->set_anchor_flag(Flint::AnchorFlag::FullRect);
        add_child(margin_container);

        auto vbox_container = std::make_shared<Flint::VBoxContainer>();
        vbox_container->set_separation(8);
        margin_container->add_child(vbox_container);

        {
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            hbox_container->set_separation(8);
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Adapter ID:");
            hbox_container->add_child(label);

            dongle_menu_button_ = std::make_shared<Flint::MenuButton>();

            dongle_menu_button_->container_sizing.expand_h = true;
            dongle_menu_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            hbox_container->add_child(dongle_menu_button_);

            // Do this before setting dongle button text.
            update_dongle_list();
            dongle_menu_button_->set_text(vidPid);

            auto callback = [this](uint32_t) { vidPid = dongle_menu_button_->get_selected_item_text(); };
            dongle_menu_button_->connect_signal("item_selected", callback);

            refresh_dongle_button_ = std::make_shared<Flint::Button>();
            auto icon = std::make_shared<Flint::VectorImage>("assets/Refresh.svg");
            refresh_dongle_button_->set_icon_normal(icon);
            refresh_dongle_button_->set_text("");
            hbox_container->add_child(refresh_dongle_button_);

            auto callback2 = [this]() { update_dongle_list(); };
            refresh_dongle_button_->connect_signal("pressed", callback2);
        }

        {
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Channel:");
            hbox_container->add_child(label);

            channel_button_ = std::make_shared<Flint::MenuButton>();
            channel_button_->container_sizing.expand_h = true;
            channel_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            hbox_container->add_child(channel_button_);

            {
                auto channel_menu = channel_button_->get_popup_menu();

                auto callback = [this](uint32_t) { channel = std::stoi(channel_button_->get_selected_item_text()); };
                channel_button_->connect_signal("item_selected", callback);

                uint32_t selected = 0;
                for (auto c : CHANNELS) {
                    channel_menu.lock()->create_item(std::to_string(c));
                    if (std::to_string(channel) == std::to_string(c)) {
                        selected = channel_menu.lock()->get_item_count() - 1;
                    }
                }

                channel_button_->select_item(selected);
            }
        }

        {
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Channel Width:");
            hbox_container->add_child(label);

            channel_width_button_ = std::make_shared<Flint::MenuButton>();
            channel_width_button_->container_sizing.expand_h = true;
            channel_width_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            hbox_container->add_child(channel_width_button_);

            {
                auto channel_width_menu = channel_width_button_->get_popup_menu();

                auto callback = [this](uint32_t) {
                    auto selected = channel_width_button_->get_selected_item_index();
                    if (selected.has_value()) {
                        channelWidthMode = selected.value();
                    }
                };
                channel_width_button_->connect_signal("item_selected", callback);

                uint32_t selected = 0;
                for (auto width : CHANNEL_WIDTHS) {
                    channel_width_menu.lock()->create_item(width);
                    int current_index = channel_width_menu.lock()->get_item_count() - 1;
                    if (channelWidthMode == current_index) {
                        selected = current_index;
                    }
                }
                channel_width_button_->select_item(selected);
            }
        }

        {
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Key:");
            hbox_container->add_child(label);

            auto text_edit = std::make_shared<Flint::TextEdit>();
            text_edit->set_editable(false);
            text_edit->set_text(DEFAULT_KEY_NAME);
            text_edit->container_sizing.expand_h = true;
            text_edit->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            hbox_container->add_child(text_edit);

            auto file_dialog = std::make_shared<Flint::FileDialog>();
            add_child(file_dialog);
            file_dialog->set_default_path(std::filesystem::absolute(DEFAULT_KEY_NAME).string());

            auto select_button = std::make_shared<Flint::Button>();
            select_button->set_text("Open");

            std::weak_ptr file_dialog_weak = file_dialog;
            std::weak_ptr text_edit_weak = text_edit;
            auto callback = [file_dialog_weak, text_edit_weak] {
                auto path = file_dialog_weak.lock()->show();
                if (path.has_value()) {
                    std::filesystem::path p(path.value());
                    text_edit_weak.lock()->set_text(p.filename().string());
                }
            };
            select_button->connect_signal("pressed", callback);
            hbox_container->add_child(select_button);
        }

        {
            play_button_ = std::make_shared<Flint::Button>();
            play_button_->container_sizing.expand_h = true;
            play_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            update_start_button_looking(true);

            auto callback1 = [this] {
                bool start = play_button_->get_text() == "Start";

                if (start) {
                    bool res = GuiInterface::Start(vidPid, channel, channelWidthMode, keyPath, codec);
                    if (!res) {
                        start = false;
                    }
                } else {
                    GuiInterface::Stop();
                }

                update_start_button_looking(!start);
            };
            play_button_->connect_signal("pressed", callback1);
            vbox_container->add_child(play_button_);
        }
    }
};

int main() {
    app = new Flint::App({1280, 720});
    // Flint::Logger::set_default_level(Flint::Logger::Level::Info);
    Flint::Logger::set_module_level("Flint", Flint::Logger::Level::Info);
    app->set_window_title("Aviateur - OpenIPC FPV Ground Station");

    // Redirect standard output to a file
    //    freopen("last_run_log.txt", "w", stdout);

    // Initialize the default libusb context.
    int rc = libusb_init(NULL);

    Flint::Logger::set_module_level("Aviateur", Flint::Logger::Level::Info);

    auto logCallback = [](LogLevel level, std::string msg) {
        switch (level) {
            case LogLevel::Info: {
                Flint::Logger::info(msg, "Aviateur");
            } break;
            case LogLevel::Debug: {
                Flint::Logger::debug(msg, "Aviateur");
            } break;
            case LogLevel::Warn: {
                Flint::Logger::warn(msg, "Aviateur");
            } break;
            case LogLevel::Error: {
                Flint::Logger::error(msg, "Aviateur");
            } break;
            default:;
        }
    };
    GuiInterface::Instance().logCallbacks.emplace_back(logCallback);

    auto hbox_container = std::make_shared<Flint::HBoxContainer>();
    hbox_container->set_separation(2);
    hbox_container->set_anchor_flag(Flint::AnchorFlag::FullRect);
    app->get_tree_root()->add_child(hbox_container);

    auto render_rect = std::make_shared<MyRenderRect>();
    render_rect->container_sizing.expand_h = true;
    render_rect->container_sizing.expand_v = true;
    render_rect->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
    render_rect->container_sizing.flag_v = Flint::ContainerSizingFlag::Fill;
    hbox_container->add_child(render_rect);

    auto control_panel = std::make_shared<MyControlPanel>();
    control_panel->set_custom_minimum_size({280, 0});
    control_panel->container_sizing.expand_v = true;
    control_panel->container_sizing.flag_v = Flint::ContainerSizingFlag::Fill;
    hbox_container->add_child(control_panel);

    auto render_rect_raw = render_rect.get();
    auto onRtpStream = [render_rect_raw](std::string sdp_file) {
        render_rect_raw->playing_file_ = sdp_file;
        render_rect_raw->start_playing(sdp_file);
    };
    GuiInterface::Instance().rtpStreamCallbacks.emplace_back(onRtpStream);

    auto control_panel_raw = control_panel.get();
    auto onWifiStop = [render_rect_raw, control_panel_raw] {
        render_rect_raw->stop_playing();
        render_rect_raw->show_red_tip("Wi-Fi stopped!");
        control_panel_raw->update_start_button_looking(true);
    };
    GuiInterface::Instance().wifiStopCallbacks.emplace_back(onWifiStop);

    {
        render_rect->fullscreen_button_ = std::make_shared<Flint::CheckButton>();
        render_rect->add_child(render_rect->fullscreen_button_);
        render_rect->fullscreen_button_->set_anchor_flag(Flint::AnchorFlag::TopLeft);

        render_rect->fullscreen_button_->set_text("Fullscreen (F11)");

        auto callback = [control_panel_raw](bool toggled) {
            app->set_fullscreen(toggled);
            control_panel_raw->set_visibility(!toggled);
        };
        render_rect->fullscreen_button_->connect_signal("toggled", callback);
    }

    app->main_loop();

    delete app;

    libusb_exit(NULL);

    return EXIT_SUCCESS;
}
