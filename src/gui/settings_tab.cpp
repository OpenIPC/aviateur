#include "settings_tab.h"

#include "resources/default_resource.h"
#include "resources/theme.h"

const std::string AVIATEUR_VERSION_NUM = "v0.2.4";
const std::string AVIATEUR_REPO_URL = "https://github.com/OpenIPC/aviateur";

void open_explorer(const std::string& dir) {
    // Check if the directory exists
    if (!std::filesystem::exists(dir)) {
        // If it doesn't exist, create it
        if (std::filesystem::create_directory(dir)) {
            GuiInterface::Instance().PutLog(LogLevel::Info, "Directory '{}' created successfully.", dir);
        } else {
            GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to create directory '{}'!", dir);
        }
    }

#ifdef _WIN32
    ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + dir + "\"";
    system(cmd.c_str());
#else
    const std::string cmd = "xdg-open \"" + dir + "\"";
    std::system(cmd.c_str());
#endif
}

void open_url(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + url + "\"";
    system(cmd.c_str());
#else
    const std::string cmd = "xdg-open \"" + url + "\"";
    std::system(cmd.c_str());
#endif
}

void SettingsContainer::custom_ready() {
    set_margin_all(8);

    auto vbox_container = std::make_shared<vecgui::VBoxContainer>();
    vbox_container->set_separation(8);
    add_child(vbox_container);

    {
        auto hbox_container = std::make_shared<vecgui::HBoxContainer>();
        hbox_container->set_separation(8);
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<vecgui::Label>();
        label->set_text(FTR("lang"));
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
        if (GuiInterface::Instance().locale_ == "ru") {
            lang_menu_button->set_text("Русский");
        }
        if (GuiInterface::Instance().locale_ == "ja") {
            lang_menu_button->set_text("日本語");
        }

        auto menu = lang_menu_button->get_popup_menu().lock();

        menu->create_item("English");
        menu->create_item("中文");
        menu->create_item("Русский");
        menu->create_item("日本語");

        auto callback = [](const uint32_t item_index) {
            GuiInterface::Instance().set_locale("en");

            if (item_index == 1) {
                GuiInterface::Instance().set_locale("zh");
            }
            if (item_index == 2) {
                GuiInterface::Instance().set_locale("ru");
            }
            if (item_index == 3) {
                GuiInterface::Instance().set_locale("ja");
            }

            GuiInterface::Instance().ShowTip(FTR("restart app to take effect"));
        };
        lang_menu_button->connect_signal("item_selected", callback);
    }

    {
        fullscreen_button_ = std::make_shared<vecgui::CheckButton>();
        vbox_container->add_child(fullscreen_button_);
        fullscreen_button_->set_text(FTR("fullscreen") + " (F11)");

        auto on_fullscreen_toggled = [](bool toggled) {
            auto render_context = vecgui::RenderContext::get_singleton();
            render_context->get_window_builder()->set_fullscreen(toggled);
        };
        fullscreen_button_->connect_signal("toggled", on_fullscreen_toggled);
    }

    {
        auto dark_mode_btn = std::make_shared<vecgui::CheckButton>();
        dark_mode_btn->set_text(FTR("dark mode"));
        vbox_container->add_child(dark_mode_btn);
        dark_mode_btn->set_toggled_no_signal(GuiInterface::Instance().dark_mode_);
        auto callback = [](const bool toggled) {
            GuiInterface::Instance().dark_mode_ = toggled;
            const auto theme = toggled ? vecgui::Theme::default_dark() : vecgui::Theme::default_light();
            vecgui::DefaultResource::get_singleton()->set_default_theme(theme);
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
        hbox_container->theme_override_bg = radio_group_bg;
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<vecgui::Label>();
        label->set_text(FTR("render backend"));
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
            auto callback = [](bool toggled) {
                GuiInterface::Instance().use_vulkan_ = toggled;
                GuiInterface::Instance().ShowTip(FTR("restart app to take effect"));
            };
            vk_btn->connect_signal("toggled", callback);
            render_btn_group->add_button(vk_btn);
        }
#else
        {
            auto metal_btn = std::make_shared<vecgui::RadioButton>();
            metal_btn->set_text("Metal");
            metal_btn->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
            vbox_container2->add_child(metal_btn);
            metal_btn->set_toggled_no_signal(true);
            render_btn_group->add_button(metal_btn);
        }
#endif
    }

    {
        auto open_capture_folder_button = std::make_shared<vecgui::MenuButton>();

        open_capture_folder_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_capture_folder_button);
        open_capture_folder_button->set_text(FTR("capture folder"));

        auto callback = [] { open_explorer(GuiInterface::GetCaptureDir()); };
        open_capture_folder_button->connect_signal("triggered", callback);
    }

    {
        auto open_appdata_button = std::make_shared<vecgui::Button>();

        open_appdata_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_appdata_button);
        open_appdata_button->set_text(FTR("config folder"));

        auto callback = [] { open_explorer(GuiInterface::GetAppDataDir()); };
        open_appdata_button->connect_signal("triggered", callback);
    }

#ifdef _WIN32
    {
        auto open_crash_dumps_button = std::make_shared<vecgui::Button>();

        open_crash_dumps_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_crash_dumps_button);
        open_crash_dumps_button->set_text(FTR("crash dump folder"));

        auto callback = [] {
            auto dir = GuiInterface::GetAppDataDir();
            auto path = std::filesystem::path(dir).parent_path().parent_path().parent_path();
            auto appdata_local = path.string() + "\\Local";
            auto dumps_dir = appdata_local + "\\CrashDumps";
            open_explorer(dumps_dir);
        };
        open_crash_dumps_button->connect_signal("triggered", callback);
    }
#endif

    {
        auto button = std::make_shared<vecgui::Button>();
        button->container_sizing.flag_h = vecgui::ContainerSizingFlag::ShrinkCenter;
        button->container_sizing.flag_v = vecgui::ContainerSizingFlag::ShrinkEnd;
        vbox_container->add_child(button);
        auto icon = std::make_shared<vecgui::VectorImage>(vecgui::get_asset_dir("icon-github.svg"));
        button->set_icon_normal(icon);
        button->set_flat(true);
        button->set_text("");

        auto callback = [] { open_url(AVIATEUR_REPO_URL); };
        button->connect_signal("triggered", callback);
    }

    {
        auto version_label = std::make_shared<vecgui::Label>();
        version_label->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        vbox_container->add_child(version_label);
        version_label->set_text(AVIATEUR_VERSION_NUM);
    }

    {
        auto exit_button = std::make_shared<vecgui::MenuButton>();

        exit_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        vbox_container->add_child(exit_button);
        exit_button->set_text(FTR("exit"));

        auto callback = [this] { get_tree()->quit(); };
        exit_button->connect_signal("triggered", callback);
    }
}

void SettingsContainer::custom_input(vecgui::InputEvent& event) {
    if (event.type == vecgui::InputEventType::Key) {
        auto key_args = event.args.key;

        if (key_args.key == vecgui::KeyCode::F11) {
            if (key_args.pressed) {
                fullscreen_button_->set_toggled(!fullscreen_button_->get_toggled());
            }
        }
    }
}
