#include <glad/gl.h>
#include <nodes/scene_tree.h>
#include <resources/default_resource.h>

#include "app.h"
#include "gui/control_panel.h"
#include "gui/player_rect.h"
#include "gui_interface.h"
#include "wifi/wfbng_link.h"

int main() {
    GuiInterface::Instance().init();
    GuiInterface::Instance().PutLog(LogLevel::Info, "App started");

    auto app = std::make_shared<revector::App>(revector::Vec2I{1280, 720},
                                               GuiInterface::Instance().dark_mode_,
                                               GuiInterface::Instance().use_vulkan_);
    app->set_window_title("Aviateur - OpenIPC FPV Ground Station");

    revector::TranslationServer::get_singleton()->load_translations(revector::get_asset_dir("translations.csv"));

    // Initialize the default libusb context.
    int rc = libusb_init(nullptr);

    {
        auto hbox_container = std::make_shared<revector::HBoxContainer>();
        hbox_container->set_anchor_flag(revector::AnchorFlag::FullRect);
        hbox_container->set_separation(0);
        app->get_tree_root()->add_child(hbox_container);

        auto player_rect = std::make_shared<PlayerRect>();
        player_rect->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        player_rect->container_sizing.flag_v = revector::ContainerSizingFlag::Fill;
        hbox_container->add_child(player_rect);

        auto control_panel = std::make_shared<ControlPanel>();
        control_panel->container_sizing.flag_v = revector::ContainerSizingFlag::Fill;
        hbox_container->add_child(control_panel);

        std::weak_ptr control_panel_weak = control_panel;
        std::weak_ptr player_rect_weak = player_rect;

        auto on_wifi_stopped = [control_panel_weak, player_rect_weak] {
            if (!control_panel_weak.expired() && !player_rect_weak.expired()) {
                player_rect_weak.lock()->stop_playing();
                player_rect_weak.lock()->show_red_tip(FTR("wi-fi stopped msg"));
                control_panel_weak.lock()->update_adapter_start_button_looking(true);
            }
        };
        GuiInterface::Instance().wifiStopCallbacks.emplace_back(on_wifi_stopped);

        {
            player_rect->top_control_container = std::make_shared<revector::HBoxContainer>();
            player_rect->top_control_container->set_anchor_flag(revector::AnchorFlag::TopLeft);
            player_rect->add_child(player_rect->top_control_container);

            player_rect->fullscreen_button_ = std::make_shared<revector::CheckButton>();
            player_rect->top_control_container->add_child(player_rect->fullscreen_button_);
            player_rect->fullscreen_button_->set_text(FTR("fullscreen") + " (F11)");

            std::weak_ptr app_weak = app;
            auto on_fullscreen_toggled = [app_weak](bool toggled) {
                if (!app_weak.expired()) {
                    app_weak.lock()->set_fullscreen(toggled);
                }
            };
            player_rect->fullscreen_button_->connect_signal("toggled", on_fullscreen_toggled);

            auto control_panel_button = std::make_shared<revector::CheckButton>();
            player_rect->top_control_container->add_child(control_panel_button);
            control_panel_button->set_text(FTR("control panel"));
            control_panel_button->set_toggled(true);

            auto on_control_panel_toggled = [control_panel_weak](bool toggled) {
                if (!control_panel_weak.expired()) {
                    control_panel_weak.lock()->set_visibility(toggled);
                }
            };
            control_panel_button->connect_signal("toggled", on_control_panel_toggled);
        }
    }

    GuiInterface::Instance().PutLog(LogLevel::Info, "Entering app main loop");

    app->main_loop();

    GuiInterface::SaveConfig();

    app.reset();

    libusb_exit(nullptr);

    return EXIT_SUCCESS;
}
