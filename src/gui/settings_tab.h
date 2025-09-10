#pragma once

#include "../gui_interface.h"
#include "app.h"

class SettingsContainer : public revector::MarginContainer {
    void custom_ready() override;

protected:
    std::shared_ptr<revector::ToggleButtonGroup> render_btn_group;
    std::shared_ptr<revector::ToggleButtonGroup> media_btn_group;
};
