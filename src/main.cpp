#include <vecgui/app.h>
#include <vecgui/resources/default_resource.h>
#include <vecgui/scene_tree.h>

#include "gui/control_panel.h"
#include "gui/player_rect.h"
#include "gui_interface.h"
#include "wifi/wfbng_link.h"

int main() {
    GuiInterface::Instance().init();
    GuiInterface::Instance().PutLog(LogLevel::Info, "App started");

    auto app = std::make_shared<vecgui::App>(vecgui::Vec2I{1280, 720}, GuiInterface::Instance().use_vulkan_);
    app->set_window_title("Aviateur - OpenIPC FPV Ground Station");

    const auto theme =
        GuiInterface::Instance().dark_mode_ ? vecgui::Theme::default_dark() : vecgui::Theme::default_light();

    auto context = app->get_context();
    context.default_resource->set_default_theme(theme);

    context.translation_server->load_translations(vecgui::get_asset_dir("translations.csv"));
    context.translation_server->set_locale(GuiInterface::Instance().locale_);

    // Initialize the default libusb context.
    int rc = libusb_init(nullptr);

    {
        auto player_rect = std::make_shared<PlayerRect>();
        player_rect->set_anchor_flag(vecgui::AnchorFlag::FullRect);
        app->get_tree_root()->add_child(player_rect);
    }

    GuiInterface::Instance().PutLog(LogLevel::Info, "Entering app main loop");

    app->main_loop();

    GuiInterface::SaveConfig();

    app.reset();

    libusb_exit(nullptr);

    return EXIT_SUCCESS;
}
