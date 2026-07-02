#pragma once

#include "../gui_interface.h"
#include "app.h"

class ControlPanel : public vecgui::Container {
public:
    std::shared_ptr<vecgui::MenuButton> dongle_menu_button_;
    std::shared_ptr<vecgui::MenuButton> channel_button_;
    std::shared_ptr<vecgui::MenuButton> channel_width_button_;
    std::shared_ptr<vecgui::Button> refresh_dongle_button_;

    std::shared_ptr<vecgui::Panel> adapter_prop_block_;
    std::shared_ptr<vecgui::Panel> udp_prop_block_;

    std::shared_ptr<vecgui::Label> tx_pwr_label_;
    std::shared_ptr<vecgui::Slider> tx_pwr_slider_;

    std::vector<std::optional<std::string>> dongle_names;
    uint32_t channel = 0;
    uint32_t channelWidthMode = 0;
    std::string keyPath;

    std::shared_ptr<vecgui::CollapseContainer> forward_con;
    std::shared_ptr<vecgui::TextEdit> forward_port_edit;

    std::shared_ptr<vecgui::Button> play_button_;

    std::shared_ptr<vecgui::Button> play_port_button_;
    std::shared_ptr<vecgui::TextEdit> local_listener_port_edit_;
    std::string local_listener_codec;

    std::shared_ptr<vecgui::TabContainer> tab_container_;

    std::vector<DeviceId> devices_;

    void update_dongle_list(const std::shared_ptr<vecgui::MenuButton>& menu_button, std::string& dongle_name);

    void update_adapter_start_button_looking(bool start_status) const;

    void update_url_start_button_looking(bool start_status) const;

    void custom_ready() override;

    void custom_input(vecgui::InputEvent& event) override;
};
