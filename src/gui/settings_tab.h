#pragma once

#include "../gui_interface.h"
#include "app.h"

class SettingsContainer : public revector::MarginContainer {
public:
    void custom_ready() override;

    void custom_input(revector::InputEvent& event) override;

protected:
    std::shared_ptr<revector::ToggleButtonGroup> render_btn_group;
    std::shared_ptr<revector::ToggleButtonGroup> media_btn_group;

    std::shared_ptr<revector::Button> fullscreen_button_;
};
