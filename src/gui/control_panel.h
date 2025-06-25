#pragma once

#include "../gui_interface.h"
#include "app.h"

class ControlPanel : public revector::Container {
public:
    std::shared_ptr<revector::MenuButton> dongle_menu_button_;
    std::shared_ptr<revector::MenuButton> channel_button_;
    std::shared_ptr<revector::MenuButton> channel_width_button_;
    std::shared_ptr<revector::Button> refresh_dongle_button_;

    std::shared_ptr<revector::MenuButton> tx_pwr_btn_;

    std::string dongle_name;
    std::optional<DeviceId> selected_dongle;
    uint32_t channel = 0;
    uint32_t channelWidthMode = 0;
    std::string keyPath;
    std::string codec;

    std::shared_ptr<revector::Button> play_button_;

    std::shared_ptr<revector::Button> play_port_button_;
    std::shared_ptr<revector::TextEdit> localhost_port_edit_;

    std::shared_ptr<revector::TabContainer> tab_container_;

    std::vector<DeviceId> devices_;

    void update_dongle_list();

    void update_adapter_start_button_looking(bool start_status) const;

    void update_url_start_button_looking(bool start_status) const;

    void custom_ready() override;

    void custom_input(revector::InputEvent &event) override;
};
