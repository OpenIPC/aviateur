#pragma once

#include "app.h"

class TipLabel : public vecgui::Label {
public:
    float display_time = 1.5;
    float fade_time = 0.5;

    std::shared_ptr<vecgui::Timer> display_timer;
    std::shared_ptr<vecgui::Timer> fade_timer;

    void custom_ready() override;

    void custom_update(double dt) override;

    void show_tip(const std::string& tip);
};
