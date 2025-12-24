#include "control_panel.h"

#include <resources/default_resource.h>

#include "settings_tab.h"

void ControlPanel::update_dongle_list(const std::shared_ptr<revector::MenuButton> &menu_button,
                                      std::string &dongle_name) {
    auto menu = menu_button->get_popup_menu().lock();

    devices_ = GuiInterface::GetDeviceList();

    menu->clear_items();

    bool previous_device_exists = false;
    for (const auto &d : devices_) {
        if (dongle_name == d.display_name) {
            previous_device_exists = true;
        }
        menu->create_item(d.display_name);
    }

    if (!previous_device_exists) {
        dongle_name = "";
    }
}

void ControlPanel::update_adapter_start_button_looking(bool start_status) const {
    tab_container_->set_tab_disabled(!start_status);

    play_button_->theme_override_normal = revector::StyleBox();
    play_button_->theme_override_pressed = revector::StyleBox();

    if (!start_status) {
        play_button_->theme_override_normal.value().bg_color = RED;
        play_button_->theme_override_pressed.value().bg_color = RED;
        play_button_->set_text(FTR("stop") + " (F5)");
        adapter_prop_block_->set_visibility(true);
    } else {
        play_button_->theme_override_normal.value().bg_color = GREEN;
        play_button_->theme_override_pressed.value().bg_color = GREEN;
        play_button_->set_text(FTR("start") + " (F5)");
        adapter_prop_block_->set_visibility(false);
    }
}

void ControlPanel::update_url_start_button_looking(bool start_status) const {
    tab_container_->set_tab_disabled(!start_status);

    play_port_button_->theme_override_normal = revector::StyleBox();
    play_port_button_->theme_override_pressed = revector::StyleBox();

    if (!start_status) {
        play_port_button_->theme_override_normal.value().bg_color = RED;
        play_port_button_->theme_override_pressed.value().bg_color = RED;
        play_port_button_->set_text(FTR("stop") + " (F5)");
    } else {
        play_port_button_->theme_override_normal.value().bg_color = GREEN;
        play_port_button_->theme_override_pressed.value().bg_color = GREEN;
        play_port_button_->set_text(FTR("start") + " (F5)");
    }
}

void ControlPanel::custom_ready() {
    auto &ini = GuiInterface::Instance().ini_;
    dongle_names.resize(2);
    dongle_names[0] = ini[CONFIG_WIFI][WIFI_DEVICE];
    dongle_names[1] = {};
    channel = std::stoi(ini[CONFIG_WIFI][WIFI_CHANNEL]);
    channelWidthMode = std::stoi(ini[CONFIG_WIFI][WIFI_CHANNEL_WIDTH_MODE]);
    keyPath = ini[CONFIG_WIFI][WIFI_GS_KEY];

    set_anchor_flag(revector::AnchorFlag::RightWide);

    tab_container_ = std::make_shared<revector::TabContainer>();
    add_child(tab_container_);
    tab_container_->set_anchor_flag(revector::AnchorFlag::FullRect);

    // Wi-Fi adapter tab
    {
        auto margin_container = std::make_shared<revector::MarginContainer>();
        margin_container->set_margin_all(8);
        tab_container_->add_child(margin_container);
        tab_container_->set_tab_title(0, "Wi-Fi");

        auto vbox = std::make_shared<revector::VBoxContainer>();
        vbox->set_separation(8);
        margin_container->add_child(vbox);

        auto con = std::make_shared<revector::Container>();
        vbox->add_child(con);

        auto vbox_blockable = std::make_shared<revector::VBoxContainer>();
        con->add_child(vbox_blockable);

        adapter_prop_block_ = std::make_shared<revector::Panel>();
        revector::StyleBox new_theme;
        new_theme.bg_color = revector::ColorU(0, 0, 0, 150);
        new_theme.border_width = 0;
        new_theme.corner_radius = 0;
        new_theme.border_color = revector::ColorU(0, 0, 0);
        adapter_prop_block_->theme_override_bg_ = new_theme;
        con->add_child(adapter_prop_block_);

        auto vbox_unblockable = std::make_shared<revector::VBoxContainer>();
        vbox->add_child(vbox_unblockable);

        {
            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            hbox_container->set_separation(8);
            vbox_blockable->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("device"));
            hbox_container->add_child(label);

            dongle_menu_button_ = std::make_shared<revector::MenuButton>();
            dongle_menu_button_->set_custom_minimum_size({0, 32});
            dongle_menu_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            hbox_container->add_child(dongle_menu_button_);

            // Do this before setting dongle button text.
            update_dongle_list(dongle_menu_button_, dongle_names[0].value());
            dongle_menu_button_->set_text(dongle_names[0].value());

            auto callback = [this](uint32_t) { dongle_names[0] = dongle_menu_button_->get_selected_item_text(); };
            dongle_menu_button_->connect_signal("item_selected", callback);

            refresh_dongle_button_ = std::make_shared<revector::Button>();
            auto icon = std::make_shared<revector::VectorImage>(revector::get_asset_dir("Refresh.svg"), true);
            refresh_dongle_button_->set_icon_normal(icon);
            refresh_dongle_button_->set_text("");
            hbox_container->add_child(refresh_dongle_button_);

            auto callback2 = [this] {
                update_dongle_list(dongle_menu_button_, dongle_names[0].value());

                if (dongle_names[1].has_value()) {
                    update_dongle_list(dongle_menu_button_b_, dongle_names[1].value());
                }
            };
            refresh_dongle_button_->connect_signal("triggered", callback2);
        }

        {
            device_b_con = std::make_shared<revector::CollapseContainer>(revector::CollapseButtonType::Check);
            device_b_con->set_title(FTR("dual adapter"));
            device_b_con->set_collapse(true);
            device_b_con->set_color(revector::ColorU(110, 137, 94));
            vbox_blockable->add_child(device_b_con);

            auto callback2 = [this](bool collapsed) {
                if (collapsed) {
                    // GuiInterface::Instance().links_.resize(1);
                    dongle_names[1] = {};
                } else {
                    // GuiInterface::Instance().links_.resize(2);
                    // GuiInterface::Instance().links_.back() = std::make_shared<WfbngLink>();
                    dongle_names[1] = "";

                    update_dongle_list(dongle_menu_button_b_, dongle_names[1].value());
                }
            };
            device_b_con->connect_signal("collapsed", callback2);

            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            hbox_container->set_separation(8);
            device_b_con->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("device"));
            hbox_container->add_child(label);

            dongle_menu_button_b_ = std::make_shared<revector::MenuButton>();
            dongle_menu_button_b_->set_custom_minimum_size({0, 32});
            dongle_menu_button_b_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            dongle_menu_button_b_->set_text("");
            hbox_container->add_child(dongle_menu_button_b_);

            // Do this before setting dongle button text.
            if (dongle_names[1].has_value()) {
                update_dongle_list(dongle_menu_button_b_, dongle_names[1].value());
                dongle_menu_button_b_->set_text(dongle_names[1].value());
            }

            auto callback = [this](uint32_t) { dongle_names[1] = dongle_menu_button_b_->get_selected_item_text(); };
            dongle_menu_button_b_->connect_signal("item_selected", callback);
        }

        {
            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            vbox_blockable->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("channel"));
            hbox_container->add_child(label);

            channel_button_ = std::make_shared<revector::MenuButton>();
            channel_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            hbox_container->add_child(channel_button_);

            {
                auto channel_menu = channel_button_->get_popup_menu();

                auto callback = [this](uint32_t) { channel = std::stoi(channel_button_->get_selected_item_meta()); };
                channel_button_->connect_signal("item_selected", callback);

                uint32_t selected = 0;
                for (const auto &pair : CHANNELS) {
                    channel_menu.lock()->create_item(pair.second);
                    int item_index = channel_menu.lock()->get_item_count() - 1;
                    channel_menu.lock()->set_item_meta(item_index, std::to_string(pair.first));
                    if (channel == pair.first) {
                        selected = item_index;
                    }
                }

                channel_button_->select_item(selected);
            }
        }

        {
            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            vbox_blockable->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("channel width"));
            hbox_container->add_child(label);

            channel_width_button_ = std::make_shared<revector::MenuButton>();
            channel_width_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
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
            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            vbox_blockable->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("key"));
            hbox_container->add_child(label);

            auto text_edit = std::make_shared<revector::TextEdit>();
            text_edit->set_editable(false);
            if (keyPath.empty()) {
                text_edit->set_text(FTR("default"));
            } else {
                text_edit->set_text(std::filesystem::path(keyPath).filename().string());
            }
            text_edit->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            hbox_container->add_child(text_edit);

            auto file_dialog = std::make_shared<revector::FileDialog>();
            add_child(file_dialog);

            if (!keyPath.empty()) {
                auto defaultKeyPath = std::filesystem::absolute(keyPath).string();
                file_dialog->set_default_path(defaultKeyPath);
            }

            auto select_button = std::make_shared<revector::Button>();
            select_button->set_text(FTR("open"));

            std::weak_ptr file_dialog_weak = file_dialog;
            std::weak_ptr text_edit_weak = text_edit;
            auto callback = [this, file_dialog_weak, text_edit_weak] {
                auto path = file_dialog_weak.lock()->show();
                if (path.has_value()) {
                    std::filesystem::path p(path.value());
                    text_edit_weak.lock()->set_text(p.filename().string());
                    keyPath = path.value();
                }
            };
            select_button->connect_signal("triggered", callback);
            hbox_container->add_child(select_button);
        }

#ifndef _WIN32
        {
            auto alink_con = std::make_shared<revector::CollapseContainer>(revector::CollapseButtonType::Check);
            alink_con->set_title(FTR("alink"));
            alink_con->set_collapse(false);
            alink_con->set_color(revector::ColorU(210, 137, 94));
            vbox_unblockable->add_child(alink_con);

            auto callback2 = [](bool collapsed) { GuiInterface::EnableAlink(!collapsed); };
            alink_con->connect_signal("collapsed", callback2);

            auto vbox_container2 = std::make_shared<revector::HBoxContainer>();
            alink_con->add_child(vbox_container2);

            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            hbox_container->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            vbox_container2->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("tx power"));
            hbox_container->add_child(label);

            tx_pwr_label_ = std::make_shared<revector::Label>();
            tx_pwr_label_->set_custom_minimum_size({64, 0});
            hbox_container->add_child(tx_pwr_label_);

            tx_pwr_slider_ = std::make_shared<revector::Slider>();
            tx_pwr_slider_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            tx_pwr_slider_->set_integer_mode(true);
            tx_pwr_slider_->set_range(1, 40);
            hbox_container->add_child(tx_pwr_slider_);

            auto callback = [this](float new_value) {
                GuiInterface::SetAlinkTxPower(new_value);
                tx_pwr_label_->set_text(std::to_string(int(round(new_value))) + " mW");
            };
            tx_pwr_slider_->connect_signal("value_changed", callback);

            // Set UI according to config
            {
                bool enabled = GuiInterface::Instance().ini_[CONFIG_WIFI][WIFI_ALINK_ENABLED] == "true";
                GuiInterface::EnableAlink(enabled);
                alink_con->set_collapse(!enabled);

                std::string tx_power = GuiInterface::Instance().ini_[CONFIG_WIFI][WIFI_ALINK_TX_POWER];
                tx_pwr_slider_->set_value(std::stoi(tx_power));
            }
        }
#endif

        {
            forward_con = std::make_shared<revector::CollapseContainer>(revector::CollapseButtonType::Check);
            forward_con->set_title(FTR("forward"));
            forward_con->set_collapse(true);
            forward_con->set_color(revector::ColorU(147, 115, 165));
            vbox_blockable->add_child(forward_con);

            auto on_collapsed = [](bool collapsed) {
                // if (collapsed) {
                //     GuiInterface::Instance().forward_port_.reset();
                // }
            };
            forward_con->connect_signal("collapsed", on_collapsed);

            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            hbox_container->set_separation(8);
            forward_con->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("target port"));
            hbox_container->add_child(label);

            forward_port_edit = std::make_shared<revector::TextEdit>();
            forward_port_edit->set_custom_minimum_size({0, 32});
            forward_port_edit->set_numbers_only(true);
            forward_port_edit->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            forward_port_edit->set_text("");
            hbox_container->add_child(forward_port_edit);

            // auto callback = [this](uint32_t) { dongle_names[1] = dongle_menu_button_b_->get_selected_item_text(); };
            // dongle_menu_button_b_->connect_signal("item_selected", callback);
        }

        {
            play_button_ = std::make_shared<revector::Button>();
            play_button_->set_custom_minimum_size({0, 48});
            play_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            update_adapter_start_button_looking(true);

            auto callback1 = [this] {
                bool start = play_button_->get_text() == FTR("start") + " (F5)";

                GuiInterface::Instance().is_using_wifi = true;
                GuiInterface::Instance().links_.clear();

                if (start) {
                    bool all_started = true;

                    if (dongle_names[1].has_value() && dongle_names[1] == dongle_names[0]) {
                        GuiInterface::Instance().ShowTip("Same device for dual adapter mode");
                        all_started = false;
                    } else {
                        for (const auto &dongle_name : dongle_names) {
                            if (!dongle_name.has_value()) {
                                continue;
                            }

                            // Check if the device is available.
                            std::optional<DeviceId> target_device_id;
                            for (auto &d : devices_) {
                                if (dongle_name == d.display_name) {
                                    target_device_id = d;
                                }
                            }

                            if (target_device_id.has_value()) {
                                bool res = false;

                                std::optional<std::string> forward_port;
                                if (!forward_con->get_collapse()) {
                                    if (forward_port_edit->get_text().empty()) {
                                        GuiInterface::Instance().ShowTip("Invalid port for RTP forwarding");
                                        all_started = false;
                                        break;
                                    }
                                    forward_port = forward_port_edit->get_text();
                                }

                                res = GuiInterface::Start(target_device_id.value(),
                                                          channel,
                                                          channelWidthMode,
                                                          keyPath,
                                                          forward_port);

                                if (!res) {
                                    GuiInterface::Instance().ShowTip("Device failed to start");
                                }

                                all_started &= res;
                            } else {
                                all_started = false;
                                GuiInterface::Instance().ShowTip("Null device");
                            }
                        }
                    }

                    if (!all_started) {
                        start = false;
                        GuiInterface::Stop();
                    }
                } else {
                    GuiInterface::Stop();
                }

                update_adapter_start_button_looking(!start);
            };
            play_button_->connect_signal("triggered", callback1);
            vbox_unblockable->add_child(play_button_);
        }
    }

    // Local tab
    {
        auto margin_container = std::make_shared<revector::MarginContainer>();
        margin_container->set_margin_all(8);
        tab_container_->add_child(margin_container);
        tab_container_->set_tab_title(1, FTR("local"));

        auto vbox = std::make_shared<revector::VBoxContainer>();
        vbox->set_separation(8);
        margin_container->add_child(vbox);

        auto con = std::make_shared<revector::Container>();
        vbox->add_child(con);

        auto vbox_blockable = std::make_shared<revector::VBoxContainer>();
        con->add_child(vbox_blockable);

        udp_prop_block_ = std::make_shared<revector::Panel>();
        revector::StyleBox new_theme;
        new_theme.bg_color = revector::ColorU(0, 0, 0, 150);
        new_theme.border_width = 0;
        new_theme.corner_radius = 0;
        new_theme.border_color = revector::ColorU(0, 0, 0);
        udp_prop_block_->theme_override_bg_ = new_theme;
        udp_prop_block_->set_visibility(false);
        con->add_child(udp_prop_block_);

        auto hbox_container = std::make_shared<revector::HBoxContainer>();
        vbox_blockable->add_child(hbox_container);

        auto label = std::make_shared<revector::Label>();
        label->set_text(FTR("port"));
        hbox_container->add_child(label);

        local_listener_port_edit_ = std::make_shared<revector::TextEdit>();
        local_listener_port_edit_->set_editable(true);
        local_listener_port_edit_->set_numbers_only(true);
        local_listener_port_edit_->set_text(GuiInterface::Instance().ini_[CONFIG_LOCALHOST][CONFIG_LOCALHOST_PORT]);
        local_listener_port_edit_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        hbox_container->add_child(local_listener_port_edit_);

        {
            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            hbox_container->set_separation(8);
            vbox_blockable->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("codec"));
            hbox_container->add_child(label);

            auto codec_menu_button = std::make_shared<revector::MenuButton>();
            codec_menu_button->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            codec_menu_button->set_text(GuiInterface::Instance().rtp_codec_);
            hbox_container->add_child(codec_menu_button);

            auto menu = codec_menu_button->get_popup_menu().lock();

            menu->create_item("H264");
            menu->create_item("H265");

            auto callback = [this](uint32_t item_index) {
                if (item_index == 0) {
                    GuiInterface::Instance().rtp_codec_ = "H264";
                }
                if (item_index == 1) {
                    GuiInterface::Instance().rtp_codec_ = "H265";
                }
            };
            codec_menu_button->connect_signal("item_selected", callback);

            if (GuiInterface::Instance().rtp_codec_ == "H264") {
                codec_menu_button->select_item(0);
            } else {
                codec_menu_button->select_item(1);
            }
        }

        {
            play_port_button_ = std::make_shared<revector::Button>();
            play_port_button_->set_custom_minimum_size({0, 48});
            play_port_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            update_url_start_button_looking(true);

            auto callback1 = [this] {
                bool start = play_port_button_->get_text() == FTR("start") + " (F5)";

                GuiInterface::Instance().is_using_wifi = false;

                if (start) {
                    std::string port = local_listener_port_edit_->get_text();

                    if (GuiInterface::Instance().use_gstreamer_) {
                        GuiInterface::Instance().EmitRtpStream("udp://0.0.0.0:" + port);
                    } else {
                        GuiInterface::Instance().NotifyRtpStream(96,
                                                                 0,
                                                                 std::stoi(port),
                                                                 GuiInterface::Instance().rtp_codec_);
                    }

                    GuiInterface::Instance().ini_[CONFIG_LOCALHOST][CONFIG_LOCALHOST_PORT] = port;

                    udp_prop_block_->set_visibility(true);
                } else {
                    GuiInterface::Instance().EmitUrlStreamShouldStop();

                    udp_prop_block_->set_visibility(false);
                }

                update_url_start_button_looking(!start);
            };

            play_port_button_->connect_signal("triggered", callback1);
            vbox->add_child(play_port_button_);
        }
    }

    // Settings tab
    {
        auto margin_container = std::make_shared<SettingsContainer>();
        tab_container_->add_child(margin_container);
        tab_container_->set_tab_title(2, FTR("settings"));
    }
}

void ControlPanel::custom_input(revector::InputEvent &event) {
    if (event.type == revector::InputEventType::Key) {
        auto key_args = event.args.key;

        if (key_args.key == revector::KeyCode::F5) {
            if (key_args.pressed) {
                if (tab_container_->get_current_tab().has_value()) {
                    if (tab_container_->get_current_tab().value() == 0) {
                        play_button_->trigger();
                    } else if (tab_container_->get_current_tab().value() == 1) {
                        play_port_button_->trigger();
                    }
                }
            }
        }
    }
}

void ControlPanel::custom_update(double dt) {
    if (device_b_con) {
        if (GuiInterface::Instance().use_gstreamer_) {
            if (!device_b_con->get_visibility()) {
                device_b_con->show();
            }
        } else {
            if (device_b_con->get_visibility()) {
                device_b_con->set_collapse(true);
                device_b_con->hide();
            }
        }
    }
}
