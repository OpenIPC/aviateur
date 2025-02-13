#include "settings_tab.h"

void SettingsContainer::custom_ready() {
    set_margin_all(8);

    auto vbox_container = std::make_shared<Flint::VBoxContainer>();
    vbox_container->set_separation(8);
    add_child(vbox_container);

    {
        auto hbox_container = std::make_shared<Flint::HBoxContainer>();
        hbox_container->set_separation(8);
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<Flint::Label>();
        label->set_text(FTR("lang") + ":");
        hbox_container->add_child(label);

        auto lang_menu_button = std::make_shared<Flint::MenuButton>();

        lang_menu_button->container_sizing.expand_h = true;
        lang_menu_button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
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

        auto menu = lang_menu_button->get_popup_menu().lock();

        menu->create_item("English");
        menu->create_item("中文");
        menu->create_item("Русский");

        auto callback = [this](uint32_t item_index) {
            GuiInterface::Instance().set_locale("en");

            if (item_index == 1) {
                GuiInterface::Instance().set_locale("zh");
            }
            if (item_index == 2) {
                GuiInterface::Instance().set_locale("ru");
            }
        };
        lang_menu_button->connect_signal("item_selected", callback);
    }

    {
        auto open_capture_folder_button = std::make_shared<Flint::MenuButton>();

        open_capture_folder_button->container_sizing.expand_h = true;
        open_capture_folder_button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_capture_folder_button);
        open_capture_folder_button->set_text(FTR("open capture folder"));

        auto callback = [this]() {
            auto absolutePath = std::string(getenv("USERPROFILE")) + "/Videos/Aviateur Captures/";
            ShellExecuteA(NULL, "open", absolutePath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
        };
        open_capture_folder_button->connect_signal("pressed", callback);
    }
}
