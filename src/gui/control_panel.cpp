#include "control_panel.h"

#include <resources/default_resource.h>

#include "settings_tab.h"

void ControlPanel::update_dongle_list() {
    auto menu = dongle_menu_button_->get_popup_menu().lock();

    devices_ = GuiInterface::GetDeviceList();

    menu->clear_items();

    bool previous_device_exists = false;
    for (const auto &d : devices_) {
        if (dongle_name == d.display_name) {
            previous_device_exists = true;
            selected_dongle = d;
        }
        menu->create_item(d.display_name);
    }

    if (!previous_device_exists) {
        dongle_name = "";
        selected_dongle = {};
    }
}

void ControlPanel::update_adapter_start_button_looking(bool start_status) const {
    tab_container_->set_tab_disabled(!start_status);

    if (!start_status) {
        play_button_->theme_normal.bg_color = RED;
        play_button_->theme_hovered = {};
        play_button_->theme_pressed.bg_color = RED;
        play_button_->set_text(FTR("stop") + " (F5)");
        adapter_prop_block_->set_visibility(true);
    } else {
        play_button_->theme_normal.bg_color = GREEN;
        play_button_->theme_hovered = {};
        play_button_->theme_pressed.bg_color = GREEN;
        play_button_->set_text(FTR("start") + " (F5)");
        adapter_prop_block_->set_visibility(false);
    }
}

void ControlPanel::update_url_start_button_looking(bool start_status) const {
    tab_container_->set_tab_disabled(!start_status);

    if (!start_status) {
        play_port_button_->theme_normal.bg_color = RED;
        play_port_button_->theme_hovered = {};
        play_port_button_->theme_pressed.bg_color = RED;
        play_port_button_->set_text(FTR("stop") + " (F5)");
    } else {
        play_port_button_->theme_normal.bg_color = GREEN;
        play_port_button_->theme_hovered = {};
        play_port_button_->theme_pressed.bg_color = GREEN;
        play_port_button_->set_text(FTR("start") + " (F5)");
    }
}

void ControlPanel::custom_ready() {
    auto &ini = GuiInterface::Instance().ini_;
    dongle_name = ini[CONFIG_WIFI][WIFI_DEVICE];
    channel = std::stoi(ini[CONFIG_WIFI][WIFI_CHANNEL]);
    channelWidthMode = std::stoi(ini[CONFIG_WIFI][WIFI_CHANNEL_WIDTH_MODE]);
    keyPath = ini[CONFIG_WIFI][WIFI_GS_KEY];

    auto default_theme = revector::DefaultResource::get_singleton()->get_default_theme();
    theme_bg = std::make_optional(default_theme->panel.styles["background"]);
    theme_bg.value().corner_radius = 0;
    theme_bg.value().border_width = 0;
    theme_bg->border_width = 0;

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
        new_theme.border_color = revector::ColorU(0, 0, 0);
        adapter_prop_block_->set_theme_panel(new_theme);
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
            update_dongle_list();
            dongle_menu_button_->set_text(dongle_name);

            auto callback = [this](uint32_t) { dongle_name = dongle_menu_button_->get_selected_item_text(); };
            dongle_menu_button_->connect_signal("item_selected", callback);

            refresh_dongle_button_ = std::make_shared<revector::Button>();
            auto icon = std::make_shared<revector::VectorImage>(revector::get_asset_dir("Refresh.svg"), true);
            refresh_dongle_button_->set_icon_normal(icon);
            refresh_dongle_button_->set_text("");
            hbox_container->add_child(refresh_dongle_button_);

            auto callback2 = [this]() { update_dongle_list(); };
            refresh_dongle_button_->connect_signal("triggered", callback2);
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
            alink_con->set_color(revector::ColorU(210.0, 137, 94));
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

            tx_pwr_btn_ = std::make_shared<revector::MenuButton>();
            tx_pwr_btn_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            hbox_container->add_child(tx_pwr_btn_);

            auto tx_pwr_menu = tx_pwr_btn_->get_popup_menu();
            auto callback = [this](uint32_t) {
                // Set tx power
                auto selected = tx_pwr_btn_->get_selected_item_index();
                if (selected.has_value()) {
                    auto power = std::stoi(ALINK_TX_POWERS[selected.value()]);
                    GuiInterface::SetAlinkTxPower(power);
                }
            };
            tx_pwr_btn_->connect_signal("item_selected", callback);

            for (auto power : ALINK_TX_POWERS) {
                tx_pwr_menu.lock()->create_item(power);
            }
            tx_pwr_btn_->select_item(2);

            // Set UI according to config
            {
                bool enabled = GuiInterface::Instance().ini_[CONFIG_WIFI][WIFI_ALINK_ENABLED] == "true";
                GuiInterface::EnableAlink(enabled);
                alink_con->set_collapse(!enabled);

                std::string tx_power = GuiInterface::Instance().ini_[CONFIG_WIFI][WIFI_ALINK_TX_POWER];

                for (int idx = 0; idx < ALINK_TX_POWERS.size(); idx++) {
                    if (ALINK_TX_POWERS[idx] == tx_power) {
                        tx_pwr_btn_->select_item(idx);
                    }
                }
            }
        }
#endif

        {
            play_button_ = std::make_shared<revector::Button>();
            play_button_->set_custom_minimum_size({0, 48});
            play_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            update_adapter_start_button_looking(true);

            auto callback1 = [this] {
                bool start = play_button_->get_text() == FTR("start") + " (F5)";

                GuiInterface::Instance().is_using_wifi = true;

                if (start) {
                    std::optional<DeviceId> target_device_id;
                    for (auto &d : devices_) {
                        if (dongle_name == d.display_name) {
                            target_device_id = d;
                        }
                    }

                    if (target_device_id.has_value()) {
                        bool res = GuiInterface::Start(target_device_id.value(), channel, channelWidthMode, keyPath);
                        if (!res) {
                            start = false;
                        }
                    } else {
                        GuiInterface::Instance().ShowTip("Null device");
                        start = false;
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
        new_theme.border_color = revector::ColorU(0, 0, 0);
        udp_prop_block_->set_theme_panel(new_theme);
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
