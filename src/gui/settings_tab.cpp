#include "settings_tab.h"

#include "resources/default_resource.h"
#include "resources/theme.h"

const std::string AVIATEUR_VERSION = "0.1.6";
const std::string AVIATEUR_REPO = "https://github.com/OpenIPC/aviateur";

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

    auto vbox_container = std::make_shared<revector::VBoxContainer>();
    vbox_container->set_separation(8);
    add_child(vbox_container);

    {
        auto hbox_container = std::make_shared<revector::HBoxContainer>();
        hbox_container->set_separation(8);
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<revector::Label>();
        label->set_text(FTR("lang"));
        hbox_container->add_child(label);

        auto lang_menu_button = std::make_shared<revector::MenuButton>();

        lang_menu_button->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
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

    revector::StyleBox radio_group_bg;
    if (GuiInterface::Instance().dark_mode_) {
        radio_group_bg.bg_color = revector::ColorU(0, 0, 0, 50);
    } else {
        radio_group_bg.bg_color = revector::ColorU(255, 255, 255, 50);
    }

    {
        auto hbox_container = std::make_shared<revector::HBoxContainer>();
        hbox_container->theme_override_bg = radio_group_bg;
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<revector::Label>();
        label->set_text(FTR("codec backend"));
        label->container_sizing.flag_v = revector::ContainerSizingFlag::ShrinkCenter;
        hbox_container->add_child(label);

        auto vbox_container2 = std::make_shared<revector::VBoxContainer>();
        vbox_container2->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        hbox_container->add_child(vbox_container2);

        media_btn_group = std::make_shared<revector::ToggleButtonGroup>();
        {
            auto ffmpeg_btn = std::make_shared<revector::RadioButton>();
            ffmpeg_btn->set_text("FFmpeg");
            ffmpeg_btn->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            vbox_container2->add_child(ffmpeg_btn);
            ffmpeg_btn->set_toggled_no_signal(!GuiInterface::Instance().use_gstreamer_);
            auto callback = [](bool toggled) { GuiInterface::Instance().use_gstreamer_ = toggled; };
            ffmpeg_btn->connect_signal("toggled", callback);

            media_btn_group->add_button(ffmpeg_btn);
        }

#ifdef AVIATEUR_USE_GSTREAMER
        {
            auto gst_btn = std::make_shared<revector::RadioButton>();
            gst_btn->set_text("GStreamer");
            gst_btn->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            vbox_container2->add_child(gst_btn);
            gst_btn->set_toggled_no_signal(GuiInterface::Instance().use_gstreamer_);
            auto callback = [](bool toggled) { GuiInterface::Instance().use_gstreamer_ = toggled; };
            gst_btn->connect_signal("toggled", callback);
            media_btn_group->add_button(gst_btn);
        }
#endif
    }

    {
        auto hbox_container = std::make_shared<revector::HBoxContainer>();
        hbox_container->theme_override_bg = radio_group_bg;
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<revector::Label>();
        label->set_text(FTR("render backend"));
        label->container_sizing.flag_v = revector::ContainerSizingFlag::ShrinkCenter;
        hbox_container->add_child(label);

        auto vbox_container2 = std::make_shared<revector::VBoxContainer>();
        vbox_container2->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        hbox_container->add_child(vbox_container2);

        render_btn_group = std::make_shared<revector::ToggleButtonGroup>();

#ifndef __APPLE__
        {
            auto gl_btn = std::make_shared<revector::RadioButton>();
            gl_btn->set_text("OpenGL");
            vbox_container2->add_child(gl_btn);
            gl_btn->set_toggled_no_signal(!GuiInterface::Instance().use_vulkan_);
            auto callback = [](bool toggled) { GuiInterface::Instance().use_vulkan_ = toggled; };
            gl_btn->connect_signal("toggled", callback);
            gl_btn->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;

            render_btn_group->add_button(gl_btn);
        }
#endif

        {
            auto vk_btn = std::make_shared<revector::RadioButton>();
            vk_btn->set_text("Vulkan");
            vk_btn->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            vbox_container2->add_child(vk_btn);
            vk_btn->set_toggled_no_signal(GuiInterface::Instance().use_vulkan_);
            auto callback = [](bool toggled) {
                GuiInterface::Instance().use_vulkan_ = toggled;
                GuiInterface::Instance().ShowTip(FTR("restart app to take effect"));
            };
            vk_btn->connect_signal("toggled", callback);
            render_btn_group->add_button(vk_btn);
        }
    }

    {
        auto dark_mode_btn = std::make_shared<revector::CheckButton>();
        dark_mode_btn->set_text(FTR("dark mode"));
        vbox_container->add_child(dark_mode_btn);
        dark_mode_btn->set_toggled_no_signal(GuiInterface::Instance().dark_mode_);
        auto callback = [](const bool toggled) {
            GuiInterface::Instance().dark_mode_ = toggled;
            const auto theme = toggled ? revector::Theme::default_dark() : revector::Theme::default_light();
            revector::DefaultResource::get_singleton()->set_default_theme(theme);
        };
        dark_mode_btn->connect_signal("toggled", callback);
    }

    {
        auto open_capture_folder_button = std::make_shared<revector::MenuButton>();

        open_capture_folder_button->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_capture_folder_button);
        open_capture_folder_button->set_text(FTR("capture folder"));

        auto callback = [] { open_explorer(GuiInterface::GetCaptureDir()); };
        open_capture_folder_button->connect_signal("triggered", callback);
    }

    {
        auto open_appdata_button = std::make_shared<revector::Button>();

        open_appdata_button->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_appdata_button);
        open_appdata_button->set_text(FTR("config folder"));

        auto callback = [] { open_explorer(GuiInterface::GetAppDataDir()); };
        open_appdata_button->connect_signal("triggered", callback);
    }

#ifdef _WIN32
    {
        auto open_crash_dumps_button = std::make_shared<revector::Button>();

        open_crash_dumps_button->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
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
        auto button = std::make_shared<revector::Button>();
        button->container_sizing.flag_h = revector::ContainerSizingFlag::ShrinkCenter;
        button->container_sizing.flag_v = revector::ContainerSizingFlag::ShrinkEnd;
        vbox_container->add_child(button);
        auto icon = std::make_shared<revector::VectorImage>(revector::get_asset_dir("icon-github.svg"));
        button->set_icon_normal(icon);
        button->set_flat(true);
        button->set_text("");

        auto callback = [] { open_url(AVIATEUR_REPO); };
        button->connect_signal("triggered", callback);
    }

    {
        auto version_label = std::make_shared<revector::Label>();
        version_label->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        vbox_container->add_child(version_label);
        version_label->set_text(FTR("version") + " " + AVIATEUR_VERSION);
    }
}
