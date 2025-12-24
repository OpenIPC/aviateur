#pragma once

#include "../gui_interface.h"
#include "app.h"

class ControlPanel : public revector::Container {
public:
    std::shared_ptr<revector::MenuButton> dongle_menu_button_;
    std::shared_ptr<revector::MenuButton> dongle_menu_button_b_;
    std::shared_ptr<revector::MenuButton> channel_button_;
    std::shared_ptr<revector::MenuButton> channel_width_button_;
    std::shared_ptr<revector::Button> refresh_dongle_button_;

    std::shared_ptr<revector::Panel> adapter_prop_block_;
    std::shared_ptr<revector::Panel> udp_prop_block_;

    std::shared_ptr<revector::Label> tx_pwr_label_;
    std::shared_ptr<revector::Slider> tx_pwr_slider_;

    std::vector<std::optional<std::string>> dongle_names;
    uint32_t channel = 0;
    uint32_t channelWidthMode = 0;
    std::string keyPath;

    std::shared_ptr<revector::CollapseContainer> device_b_con;

    std::shared_ptr<revector::CollapseContainer> forward_con;
    std::shared_ptr<revector::TextEdit> forward_port_edit;

    std::shared_ptr<revector::Button> play_button_;

    std::shared_ptr<revector::Button> play_port_button_;
    std::shared_ptr<revector::TextEdit> local_listener_port_edit_;
    std::string local_listener_codec;

    std::shared_ptr<revector::TabContainer> tab_container_;

    std::vector<DeviceId> devices_;

    void update_dongle_list(const std::shared_ptr<revector::MenuButton>& menu_button, std::string& dongle_name);

    void update_adapter_start_button_looking(bool start_status) const;

    void update_url_start_button_looking(bool start_status) const;

    void custom_ready() override;

    void custom_input(revector::InputEvent& event) override;

    void custom_update(double dt) override;
};
