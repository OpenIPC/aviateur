#pragma once

#include "../gui_interface.h"
#include <vecgui/app.h>

class SettingsContainer : public vecgui::MarginContainer {
public:
    void on_ready() override;

    void on_input(vecgui::InputEvent& event) override;

protected:
    std::shared_ptr<vecgui::ToggleButtonGroup> render_btn_group;
    std::shared_ptr<vecgui::ToggleButtonGroup> media_btn_group;

    std::shared_ptr<vecgui::Button> fullscreen_button_;
};
