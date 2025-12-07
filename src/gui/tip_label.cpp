#include "tip_label.h"

void TipLabel::custom_ready() {
    set_font_size(32);

    auto style_box = revector::StyleBox();
    style_box.bg_color = revector::ColorU(201, 79, 79);
    style_box.corner_radius = 0;
    theme_override_bg = style_box;

    display_timer = std::make_shared<revector::Timer>();
    fade_timer = std::make_shared<revector::Timer>();

    add_child(display_timer);
    add_child(fade_timer);

    auto callback = [this] { fade_timer->start_timer(fade_time); };
    display_timer->connect_signal("timeout", callback);

    auto callback2 = [this] { set_visibility(false); };
    fade_timer->connect_signal("timeout", callback2);
}

void TipLabel::custom_update(double dt) {
    if (!fade_timer->is_stopped()) {
        alpha = fade_timer->get_remaining_time() / fade_time;
    }
}

void TipLabel::show_tip(const std::string& tip) {
    if (!display_timer->is_stopped()) {
        display_timer->stop();
    }
    if (!fade_timer->is_stopped()) {
        fade_timer->stop();
    }
    set_text(tip);
    set_visibility(true);
    alpha = 1;
    display_timer->start_timer(display_time);
}
