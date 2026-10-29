#include "settings_tab.h"

#include <vecgui/resources/default_resource.h>
#include <vecgui/resources/theme.h>

constexpr const char* AVIATEUR_VERSION_NUM = "0.4.1";
constexpr const char* AVIATEUR_REPO_URL = "https://github.com/OpenIPC/aviateur";

void SettingsContainer::on_ready() {
    set_margin_all(8);

    auto top_container = std::make_shared<vecgui::VBoxContainer>();
    top_container->set_separation(8);
    add_child(top_container);

    const int button_height = 48;

    auto grid = std::make_shared<vecgui::GridContainer>();
    grid->set_separation(8);
    grid->set_column_limit(3);
    top_container->add_child(grid);

    auto context = get_context();

    {
        auto hbox_container = std::make_shared<vecgui::HBoxContainer>();
        hbox_container->set_separation(8);
        hbox_container->set_custom_minimum_size({0, button_height});
        grid->add_child(hbox_container);

        auto label = std::make_shared<vecgui::Label>();
        label->set_text(context->translation_server->get_translation("lang"));
        hbox_container->add_child(label);

        auto lang_menu_button = std::make_shared<vecgui::MenuButton>();

        lang_menu_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        hbox_container->add_child(lang_menu_button);

        if (GuiInterface::Instance().locale_ == "en") {
            lang_menu_button->set_text("English");
        }
        if (GuiInterface::Instance().locale_ == "zh") {
            lang_menu_button->set_text("中文");
        }

        auto menu = lang_menu_button->get_popup_menu().lock();

        menu->create_item("English");
        menu->create_item("中文");

        auto callback = [context](const uint32_t item_index) {
            if (item_index == 1) {
                GuiInterface::Instance().set_locale("zh");
            } else {
                GuiInterface::Instance().set_locale("en");
            }

            GuiInterface::Instance().ShowTip(context->translation_server->get_translation("restart app to take effect"),
                                             false);
        };
        lang_menu_button->connect_signal("item_selected", callback);
    }

    {
        fullscreen_button_ = std::make_shared<vecgui::CheckButton>();
        fullscreen_button_->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        grid->add_child(fullscreen_button_);
        fullscreen_button_->set_text(context->translation_server->get_translation("fullscreen") + " (F11)");

        auto on_fullscreen_toggled = [context](bool toggled) {
            context->render_context->get_window_builder()->set_fullscreen(toggled);
        };
        fullscreen_button_->connect_signal("toggled", on_fullscreen_toggled);
    }

    {
        auto dark_mode_btn = std::make_shared<vecgui::CheckButton>();
        dark_mode_btn->set_text(context->translation_server->get_translation("dark mode"));
        dark_mode_btn->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        grid->add_child(dark_mode_btn);
        dark_mode_btn->set_toggled_no_signal(GuiInterface::Instance().dark_mode_);
        auto callback = [context](const bool toggled) {
            GuiInterface::Instance().dark_mode_ = toggled;
            const auto theme = toggled ? vecgui::Theme::default_dark() : vecgui::Theme::default_light();
            context->default_resource->set_default_theme(theme);

            GuiInterface::Instance().ShowTip(context->translation_server->get_translation("restart app to take effect"),
                                             false);
        };
        dark_mode_btn->connect_signal("toggled", callback);
    }

    vecgui::StyleBox radio_group_bg;
    if (GuiInterface::Instance().dark_mode_) {
        radio_group_bg.bg_color = vecgui::ColorU(0, 0, 0, 50);
    } else {
        radio_group_bg.bg_color = vecgui::ColorU(255, 255, 255, 50);
    }

    {
        auto hbox_container = std::make_shared<vecgui::HBoxContainer>();
        hbox_container->set_custom_minimum_size({0, button_height});
        hbox_container->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        hbox_container->theme_override_bg = radio_group_bg;
        grid->add_child(hbox_container);

        auto label = std::make_shared<vecgui::Label>();
        label->set_text(context->translation_server->get_translation("render backend"));
        label->container_sizing.flag_v = vecgui::ContainerSizingFlag::ShrinkCenter;
        hbox_container->add_child(label);

        auto vbox_container2 = std::make_shared<vecgui::VBoxContainer>();
        vbox_container2->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        hbox_container->add_child(vbox_container2);

        render_btn_group = std::make_shared<vecgui::ToggleButtonGroup>();

#ifndef __APPLE__
        {
            auto gl_btn = std::make_shared<vecgui::RadioButton>();
            gl_btn->set_text("OpenGL");
            vbox_container2->add_child(gl_btn);
            gl_btn->set_toggled_no_signal(!GuiInterface::Instance().use_vulkan_);
            auto callback = [](bool toggled) { GuiInterface::Instance().use_vulkan_ = toggled; };
            gl_btn->connect_signal("toggled", callback);
            gl_btn->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;

            render_btn_group->add_button(gl_btn);
        }
        {
            auto vk_btn = std::make_shared<vecgui::RadioButton>();
            vk_btn->set_text("Vulkan");
            vk_btn->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
            vbox_container2->add_child(vk_btn);
            vk_btn->set_toggled_no_signal(GuiInterface::Instance().use_vulkan_);
            auto callback = [context](bool toggled) {
                GuiInterface::Instance().use_vulkan_ = toggled;
                GuiInterface::Instance().ShowTip(
                    context->translation_server->get_translation("restart app to take effect"),
                    false);
            };
            vk_btn->connect_signal("toggled", callback);
            render_btn_group->add_button(vk_btn);
        }
#else
        {
            auto metal_btn = std::make_shared<vecgui::RadioButton>();
            metal_btn->set_text("Metal");
            metal_btn->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
            metal_btn->container_sizing.flag_v = vecgui::ContainerSizingFlag::Fill;
            vbox_container2->add_child(metal_btn);
            metal_btn->set_toggled_no_signal(true);
            render_btn_group->add_button(metal_btn);
        }
#endif
    }

    {
        auto open_appdata_button = std::make_shared<vecgui::Button>();
        open_appdata_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        grid->add_child(open_appdata_button);
        open_appdata_button->set_text(context->translation_server->get_translation("config folder"));

        auto callback = [] { open_explorer(GuiInterface::GetAppDataDir()); };
        open_appdata_button->connect_signal("triggered", callback);
    }

#ifdef _WIN32
    // {
    //     auto open_crash_dumps_button = std::make_shared<vecgui::Button>();
    //
    //     open_crash_dumps_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    //     grid->add_child(open_crash_dumps_button);
    //     open_crash_dumps_button->set_text(context->translation_server->get_translation("crash dump folder"));
    //
    //     auto callback = [] {
    //         auto dir = GuiInterface::GetAppDataDir();
    //         auto path = std::filesystem::path(dir).parent_path().parent_path().parent_path();
    //         auto appdata_local = path.string() + "\\Local";
    //         auto dumps_dir = appdata_local + "\\CrashDumps";
    //         open_explorer(dumps_dir);
    //     };
    //     open_crash_dumps_button->connect_signal("triggered", callback);
    // }
#endif

    auto about_container = std::make_shared<vecgui::HBoxContainer>();
    about_container->set_separation(8);
    about_container->set_custom_minimum_size({0, button_height});
    about_container->container_sizing.flag_v = vecgui::ContainerSizingFlag::ShrinkEnd;
    top_container->add_child(about_container);

    {
        auto version_label = std::make_shared<vecgui::Label>();
        version_label->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        about_container->add_child(version_label);
        version_label->set_text(context->translation_server->get_translation("version") +
                                std::string(AVIATEUR_VERSION_NUM));
    }

    {
        auto button = std::make_shared<vecgui::Button>();
        button->container_sizing.flag_h = vecgui::ContainerSizingFlag::ShrinkCenter;
        button->container_sizing.flag_v = vecgui::ContainerSizingFlag::ShrinkCenter;
        about_container->add_child(button);
        auto icon = std::make_shared<vecgui::VectorImage>(context, vecgui::get_asset_dir("icon-github.svg"));
        button->set_icon_normal(icon);
        button->set_flat(true);
        button->set_text("GitHub");

        auto callback = [] { open_url(AVIATEUR_REPO_URL); };
        button->connect_signal("triggered", callback);
    }

    {
        auto exit_button = std::make_shared<vecgui::MenuButton>();

        exit_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        about_container->add_child(exit_button);
        exit_button->set_text(context->translation_server->get_translation("exit"));

        auto callback = [this] { get_tree()->quit(); };
        exit_button->connect_signal("triggered", callback);
    }
}

void SettingsContainer::on_input(vecgui::InputEvent& event) {
    if (event.type == vecgui::InputEventType::Key) {
        auto key_args = event.args.key;

        if (key_args.key == vecgui::KeyCode::F11) {
            if (key_args.pressed) {
                fullscreen_button_->set_toggled(!fullscreen_button_->get_toggled());
            }
        }
    }
}
