#pragma once

#include "../gui_interface.h"
#include "app.h"

class SettingsContainer : public vecgui::MarginContainer {
public:
    void custom_ready() override;

    void custom_input(vecgui::InputEvent& event) override;

protected:
    std::shared_ptr<vecgui::ToggleButtonGroup> render_btn_group;
    std::shared_ptr<vecgui::ToggleButtonGroup> media_btn_group;

    std::shared_ptr<vecgui::Button> fullscreen_button_;
};
