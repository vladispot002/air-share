#include "landrop/ui/GtkDesktopApp.hpp"
#include <iostream>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace landrop::ui {

GtkDesktopApp::GtkDesktopApp(bool isMobileMode)
    : m_discoveryPresenter(this),
      m_transferPresenter(this),
      m_storagePresenter(this),
      m_settingsPresenter(this),
      m_isMobileMode(isMobileMode) {}

GtkDesktopApp::~GtkDesktopApp() {
    if (m_cssProvider) {
        g_object_unref(m_cssProvider);
        m_cssProvider = nullptr;
    }
}

int GtkDesktopApp::run(int argc, char* argv[]) {
    if (!gtk_init_check(&argc, &argv)) {
        std::cerr << "Error: Unable to initialize GTK display.\n";
        return 1;
    }

    buildUi();
    applyAppleTheme();

    m_discoveryPresenter.initialize();
    m_transferPresenter.initialize();
    m_storagePresenter.initialize();
    m_settingsPresenter.initialize();

    g_timeout_add(300, [](gpointer data) -> gboolean {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().tick();
        return TRUE;
    }, this);

    gtk_widget_show_all(m_mainWindow);
    gtk_main();

    return 0;
}

void GtkDesktopApp::applyAppleTheme() {
    if (!m_cssProvider) {
        m_cssProvider = gtk_css_provider_new();
        GdkScreen* screen = gdk_screen_get_default();
        gtk_style_context_add_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(m_cssProvider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }

    std::string css;
    if (m_isDarkMode) {
        css = R"(
            window {
                background-color: #1E1E1E;
                color: #F5F5F7;
                font-family: -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            }
            headerbar {
                background-color: #252528;
                border-bottom: 1px solid #3A3A3C;
                color: #FFFFFF;
            }
            .brand-label {
                font-size: 14px;
                font-weight: bold;
                color: #F5F5F7;
                margin-left: 8px;
                margin-right: 8px;
            }
            .apple-card {
                background-color: #2C2C2E;
                border: 1px solid #3A3A3C;
                border-radius: 10px;
                padding: 12px;
            }
            .device-card-selected {
                border: 2px solid #0A84FF;
                background-color: #243347;
            }
            button {
                background-color: #3A3A3C;
                color: #F5F5F7;
                border: 1px solid #48484A;
                border-radius: 8px;
                padding: 6px 14px;
                font-weight: 500;
                font-size: 12px;
            }
            button:hover {
                background-color: #48484A;
            }
            button:disabled {
                opacity: 0.4;
            }
            .apple-accent-btn {
                background-color: #0A84FF;
                color: #FFFFFF;
                border: none;
                font-weight: bold;
                border-radius: 7px;
                padding: 6px 14px;
            }
            .apple-accent-btn:hover {
                background-color: #0071E3;
            }
            .apple-danger-btn {
                background-color: #3A1515;
                color: #FF453A;
                border: 1px solid #FF453A;
                border-radius: 7px;
                padding: 6px 14px;
            }
            .apple-danger-btn:hover {
                background-color: #551D1D;
            }
            .segmented-box {
                background-color: #3A3A3C;
                border-radius: 8px;
                padding: 2px;
            }
            .segmented-btn {
                background: transparent;
                color: #98989D;
                border: none;
                border-radius: 6px;
                padding: 5px 14px;
                font-weight: 500;
                font-size: 13px;
            }
            .segmented-btn:hover {
                color: #FFFFFF;
            }
            .segmented-btn-active {
                background-color: #1C1C1E;
                color: #FFFFFF;
                font-weight: bold;
                box-shadow: 0 1px 4px rgba(0,0,0,0.55);
            }
            .pill-active {
                background-color: #0D2B18;
                color: #30D158;
                border-radius: 10px;
                padding: 2px 9px;
                font-size: 11px;
                font-weight: bold;
            }
            .pill-cloud {
                background-color: #0D2040;
                color: #0A84FF;
                border-radius: 10px;
                padding: 2px 9px;
                font-size: 11px;
                font-weight: 500;
            }
            .pill-os {
                background-color: #3A3A3C;
                color: #98989D;
                border-radius: 10px;
                padding: 2px 9px;
                font-size: 11px;
                font-weight: 500;
            }
            .theme-btn {
                background-color: #1C1C1E;
                border: 1px solid #3A3A3C;
                border-radius: 12px;
                color: #98989D;
                padding: 3px 8px;
                font-size: 11px;
            }
            .theme-btn:hover {
                color: #FFFFFF;
            }
            .status-badge {
                background-color: #0D2B18;
                color: #30D158;
                border-radius: 10px;
                padding: 4px 9px;
                font-size: 11px;
                font-weight: bold;
            }
            .device-icon-box {
                background-color: #3A3A3C;
                border-radius: 10px;
                min-width: 42px;
                min-height: 42px;
                font-size: 20px;
            }
            .rssi-bar-active {
                background-color: #30D158;
                border-radius: 2px;
                min-width: 4px;
            }
            .rssi-bar-dim {
                background-color: #636366;
                border-radius: 2px;
                min-width: 4px;
            }
            .mac-terminal-card {
                background-color: #0D0D0F;
                border: 1px solid #2A2A2C;
                border-radius: 10px;
            }
            .mac-terminal-header {
                background-color: #151518;
                border-bottom: 1px solid #2A2A2C;
                padding: 6px 12px;
                border-radius: 10px 10px 0 0;
            }
            .mac-terminal-title {
                color: #98989D;
                font-family: monospace;
                font-size: 11px;
            }
            .mac-terminal-view, .mac-terminal-view text {
                background-color: #0D0D0F;
                color: #30D158;
                font-family: monospace;
                font-size: 11px;
                padding: 8px;
            }
            .mobile-bottom-bar {
                background-color: #2C2C2E;
                border-top: 1px solid #3A3A3C;
                padding: 6px;
            }
            entry {
                background-color: #1C1C1E;
                color: #FFFFFF;
                border: 1px solid #3A3A3C;
                border-radius: 6px;
                padding: 6px 8px;
            }
            entry:focus {
                border-color: #0A84FF;
            }
            spinbutton {
                background-color: #1C1C1E;
                color: #FFFFFF;
                border: 1px solid #3A3A3C;
                border-radius: 6px;
                padding: 0;
            }
            spinbutton entry {
                background-color: transparent;
                border: none;
                color: #FFFFFF;
            }
            spinbutton button {
                background-color: #2C2C2E;
                color: #F5F5F7;
                border: none;
                border-radius: 0;
                padding: 4px;
            }
            spinbutton button:hover {
                background-color: #3A3A3C;
            }
            switch {
                background-color: #3A3A3C;
                border-radius: 14px;
                border: 1px solid #48484A;
            }
            switch:checked {
                background-color: #30D158;
                border-color: #30D158;
            }
            switch slider {
                background-color: #FFFFFF;
                border-radius: 50%;
                margin: 2px;
                min-width: 20px;
                min-height: 20px;
            }
            scale trough {
                background-color: #3A3A3C;
                border-radius: 4px;
                min-height: 6px;
            }
            scale highlight {
                background-color: #0A84FF;
                border-radius: 4px;
                min-height: 6px;
            }
            scale slider {
                background-color: #FFFFFF;
                border-radius: 50%;
                min-width: 18px;
                min-height: 18px;
            }
            treeview {
                background-color: #2C2C2E;
                color: #F5F5F7;
                border: 1px solid #3A3A3C;
                border-radius: 8px;
            }
            treeview header button {
                background-color: #252528;
                color: #98989D;
                font-weight: bold;
                border: none;
                border-bottom: 1px solid #3A3A3C;
                padding: 4px 8px;
            }
            treeview:selected {
                background-color: #0A84FF;
                color: #FFFFFF;
            }
            progressbar progress {
                background-color: #30D158;
                border-radius: 4px;
            }
            progressbar trough {
                background-color: #3A3A3C;
                border-radius: 4px;
            }
            scrollbar trough {
                background-color: transparent;
            }
            scrollbar slider {
                background-color: #48484A;
                border-radius: 4px;
                min-width: 6px;
                min-height: 6px;
            }
            scrollbar slider:hover {
                background-color: #636366;
            }
        )";
    } else {
        css = R"(
            window {
                background-color: #F5F5F7;
                color: #1D1D1F;
                font-family: -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            }
            headerbar {
                background-color: #ECECEE;
                border-bottom: 1px solid #D2D2D7;
                color: #1D1D1F;
            }
            .brand-label {
                font-size: 14px;
                font-weight: bold;
                color: #1D1D1F;
                margin-left: 8px;
                margin-right: 8px;
            }
            .apple-card {
                background-color: #FFFFFF;
                border: 1px solid #D2D2D7;
                border-radius: 10px;
                padding: 12px;
            }
            .device-card-selected {
                border: 2px solid #007AFF;
                background-color: #E8F2FF;
            }
            button {
                background-color: #E5E5EA;
                color: #1D1D1F;
                border: 1px solid #D1D1D6;
                border-radius: 8px;
                padding: 6px 14px;
                font-weight: 500;
                font-size: 12px;
            }
            button:hover {
                background-color: #D1D1D6;
            }
            button:disabled {
                opacity: 0.4;
            }
            .apple-accent-btn {
                background-color: #007AFF;
                color: #FFFFFF;
                border: none;
                font-weight: bold;
                border-radius: 7px;
                padding: 6px 14px;
            }
            .apple-accent-btn:hover {
                background-color: #0062CC;
            }
            .apple-danger-btn {
                background-color: #FFE5E5;
                color: #FF3B30;
                border: 1px solid #FF3B30;
                border-radius: 7px;
                padding: 6px 14px;
            }
            .apple-danger-btn:hover {
                background-color: #FFD1D1;
            }
            .segmented-box {
                background-color: #E0E0E5;
                border-radius: 8px;
                padding: 2px;
            }
            .segmented-btn {
                background: transparent;
                color: #6E6E73;
                border: none;
                border-radius: 6px;
                padding: 5px 14px;
                font-weight: 500;
                font-size: 13px;
            }
            .segmented-btn:hover {
                color: #1D1D1F;
            }
            .segmented-btn-active {
                background-color: #FFFFFF;
                color: #1D1D1F;
                font-weight: bold;
                box-shadow: 0 1px 3px rgba(0,0,0,0.12);
            }
            .pill-active {
                background-color: #D4F0DA;
                color: #1A7A36;
                border-radius: 10px;
                padding: 2px 9px;
                font-size: 11px;
                font-weight: bold;
            }
            .pill-cloud {
                background-color: #E5F0FF;
                color: #007AFF;
                border-radius: 10px;
                padding: 2px 9px;
                font-size: 11px;
                font-weight: 500;
            }
            .pill-os {
                background-color: #F0F0F2;
                color: #6E6E73;
                border-radius: 10px;
                padding: 2px 9px;
                font-size: 11px;
                font-weight: 500;
            }
            .theme-btn {
                background-color: #FFFFFF;
                border: 1px solid #D2D2D7;
                border-radius: 12px;
                color: #6E6E73;
                padding: 3px 8px;
                font-size: 11px;
            }
            .theme-btn:hover {
                color: #1D1D1F;
            }
            .status-badge {
                background-color: #D4F0DA;
                color: #1A7A36;
                border-radius: 10px;
                padding: 4px 9px;
                font-size: 11px;
                font-weight: bold;
            }
            .device-icon-box {
                background-color: #E5E5EA;
                border-radius: 10px;
                min-width: 42px;
                min-height: 42px;
                font-size: 20px;
            }
            .rssi-bar-active {
                background-color: #34C759;
                border-radius: 2px;
                min-width: 4px;
            }
            .rssi-bar-dim {
                background-color: #C7C7CC;
                border-radius: 2px;
                min-width: 4px;
            }
            .mac-terminal-card {
                background-color: #1C1D22;
                border: 1px solid #D2D2D7;
                border-radius: 10px;
            }
            .mac-terminal-header {
                background-color: #26282E;
                border-bottom: 1px solid #363840;
                padding: 6px 12px;
                border-radius: 10px 10px 0 0;
            }
            .mac-terminal-title {
                color: #A0A0A5;
                font-family: monospace;
                font-size: 11px;
            }
            .mac-terminal-view, .mac-terminal-view text {
                background-color: #1C1D22;
                color: #30D158;
                font-family: monospace;
                font-size: 11px;
                padding: 8px;
            }
            .mobile-bottom-bar {
                background-color: #FFFFFF;
                border-top: 1px solid #D2D2D7;
                padding: 6px;
            }
            entry {
                background-color: #FFFFFF;
                color: #1D1D1F;
                border: 1px solid #D2D2D7;
                border-radius: 6px;
                padding: 6px 8px;
            }
            entry:focus {
                border-color: #007AFF;
            }
            spinbutton {
                background-color: #FFFFFF;
                color: #1D1D1F;
                border: 1px solid #D2D2D7;
                border-radius: 6px;
                padding: 0;
            }
            spinbutton entry {
                background-color: transparent;
                border: none;
                color: #1D1D1F;
            }
            spinbutton button {
                background-color: #E5E5EA;
                color: #1D1D1F;
                border: none;
                border-radius: 0;
                padding: 4px;
            }
            spinbutton button:hover {
                background-color: #D1D1D6;
            }
            switch {
                background-color: #D2D2D7;
                border-radius: 14px;
                border: 1px solid #C7C7CC;
            }
            switch:checked {
                background-color: #34C759;
                border-color: #34C759;
            }
            switch slider {
                background-color: #FFFFFF;
                border-radius: 50%;
                margin: 2px;
                min-width: 20px;
                min-height: 20px;
            }
            scale trough {
                background-color: #E0E0E5;
                border-radius: 4px;
                min-height: 6px;
            }
            scale highlight {
                background-color: #007AFF;
                border-radius: 4px;
                min-height: 6px;
            }
            scale slider {
                background-color: #FFFFFF;
                border-radius: 50%;
                min-width: 18px;
                min-height: 18px;
            }
            treeview {
                background-color: #FFFFFF;
                color: #1D1D1F;
                border: 1px solid #D2D2D7;
                border-radius: 8px;
            }
            treeview header button {
                background-color: #F0F0F2;
                color: #6E6E73;
                font-weight: bold;
                border: none;
                border-bottom: 1px solid #D2D2D7;
                padding: 4px 8px;
            }
            treeview:selected {
                background-color: #007AFF;
                color: #FFFFFF;
            }
            progressbar progress {
                background-color: #34C759;
                border-radius: 4px;
            }
            progressbar trough {
                background-color: #E5E5EA;
                border-radius: 4px;
            }
            scrollbar trough {
                background-color: transparent;
            }
            scrollbar slider {
                background-color: #C7C7CC;
                border-radius: 4px;
                min-width: 6px;
                min-height: 6px;
            }
            scrollbar slider:hover {
                background-color: #8E8E93;
            }
        )";
    }

    gtk_css_provider_load_from_data(m_cssProvider, css.c_str(), -1, nullptr);
}

void GtkDesktopApp::toggleTheme(bool isDark) {
    m_isDarkMode = isDark;
    applyAppleTheme();
    if (m_themeBtn) {
        gtk_button_set_label(GTK_BUTTON(m_themeBtn), m_isDarkMode ? "● Dark" : "○ Light");
    }
    showNotification(m_isDarkMode ? "Theme: Dark Mode" : "Theme: Light Mode");
}

GtkWidget* GtkDesktopApp::createSegmentedControl() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "segmented-box");

    struct TabDef {
        const char* id;
        const char* label;
    };
    TabDef tabs[] = {
        {"discovery", "Radar"},
        {"transfers", "Transfers"},
        {"storage", "Storage"},
        {"settings", "Settings"}
    };

    m_segmentedButtons.clear();

    for (size_t i = 0; i < 4; ++i) {
        GtkWidget* btn = gtk_button_new_with_label(tabs[i].label);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "segmented-btn");
        if (i == 0) {
            gtk_style_context_add_class(gtk_widget_get_style_context(btn), "segmented-btn-active");
        }
        g_object_set_data_full(G_OBJECT(btn), "tab_id", g_strdup(tabs[i].id), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer appPtr) {
            const char* tid = static_cast<const char*>(g_object_get_data(G_OBJECT(b), "tab_id"));
            if (tid) {
                static_cast<GtkDesktopApp*>(appPtr)->switchToTab(tid);
            }
        }), this);

        m_segmentedButtons.push_back(btn);
        gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);
    }

    return box;
}

void GtkDesktopApp::switchToTab(const std::string& tabName) {
    if (!m_stack) return;
    gtk_stack_set_visible_child_name(GTK_STACK(m_stack), tabName.c_str());

    const char* names[] = {"discovery", "transfers", "storage", "settings"};
    for (size_t i = 0; i < m_segmentedButtons.size(); ++i) {
        auto* ctx = gtk_widget_get_style_context(m_segmentedButtons[i]);
        if (names[i % 4] == tabName) {
            gtk_style_context_add_class(ctx, "segmented-btn-active");
        } else {
            gtk_style_context_remove_class(ctx, "segmented-btn-active");
        }
    }
}

GtkWidget* GtkDesktopApp::createMobileBottomBar() {
    m_segmentedButtons.clear();
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "mobile-bottom-bar");

    struct TabInfo {
        const char* id;
        const char* label;
    };
    TabInfo items[] = {
        {"discovery", "📡 Radar"},
        {"transfers", "⇄ Transfers"},
        {"storage", "📁 Storage"},
        {"settings", "⚙️ Settings"}
    };

    for (int i = 0; i < 4; ++i) {
        GtkWidget* btn = gtk_button_new_with_label(items[i].label);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "segmented-btn");
        if (i == 0) {
            gtk_style_context_add_class(gtk_widget_get_style_context(btn), "segmented-btn-active");
        }
        g_object_set_data_full(G_OBJECT(btn), "tab_id", g_strdup(items[i].id), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer appPtr) {
            const char* tid = static_cast<const char*>(g_object_get_data(G_OBJECT(b), "tab_id"));
            if (tid) {
                static_cast<GtkDesktopApp*>(appPtr)->switchToTab(tid);
            }
        }), this);

        m_segmentedButtons.push_back(btn);
        gtk_box_pack_start(GTK_BOX(bar), btn, TRUE, TRUE, 0);
    }

    return bar;
}

void GtkDesktopApp::buildUi() {
    m_mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (m_isMobileMode) {
        gtk_window_set_title(GTK_WINDOW(m_mainWindow), "LAN-Drop Mobile");
        gtk_window_set_default_size(GTK_WINDOW(m_mainWindow), 390, 812);
    } else {
        gtk_window_set_title(GTK_WINDOW(m_mainWindow), "LAN-Drop — AirShare");
        gtk_window_set_default_size(GTK_WINDOW(m_mainWindow), 980, 700);
    }
    const char* gtkIconCandidates[] = {
        "/usr/share/icons/hicolor/scalable/apps/lan-drop.svg",
        "/usr/share/landrop/lan-drop.svg",
        "resources/lan-drop.svg",
        "../resources/lan-drop.svg"
    };
    for (const char* iconPath : gtkIconCandidates) {
        if (gtk_window_set_icon_from_file(GTK_WINDOW(m_mainWindow), iconPath, nullptr)) {
            break;
        }
    }

    g_signal_connect(m_mainWindow, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    m_headerBar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(m_headerBar), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(m_mainWindow), m_headerBar);

    if (!m_isMobileMode) {
        GtkWidget* brandLabel = gtk_label_new("LAN-Drop");
        gtk_style_context_add_class(gtk_widget_get_style_context(brandLabel), "brand-label");
        gtk_header_bar_pack_start(GTK_HEADER_BAR(m_headerBar), brandLabel);
        gtk_header_bar_set_custom_title(GTK_HEADER_BAR(m_headerBar), createSegmentedControl());
    } else {
        GtkWidget* mobileTitle = gtk_label_new(nullptr);
        gtk_label_set_markup(GTK_LABEL(mobileTitle), "<b>LAN-Drop Mobile</b>");
        gtk_header_bar_set_custom_title(GTK_HEADER_BAR(m_headerBar), mobileTitle);
    }

    GtkWidget* rightBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    m_themeBtn = gtk_button_new_with_label(m_isDarkMode ? "● Dark" : "○ Light");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_themeBtn), "theme-btn");
    g_signal_connect(m_themeBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->toggleTheme(!app->m_isDarkMode);
    }), this);
    gtk_box_pack_start(GTK_BOX(rightBox), m_themeBtn, FALSE, FALSE, 0);

    GtkWidget* onlineBadge = gtk_label_new("● Online");
    gtk_style_context_add_class(gtk_widget_get_style_context(onlineBadge), "status-badge");
    gtk_box_pack_start(GTK_BOX(rightBox), onlineBadge, FALSE, FALSE, 0);

    gtk_header_bar_pack_end(GTK_HEADER_BAR(m_headerBar), rightBox);

    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(m_mainWindow), mainBox);

    m_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(m_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(m_stack), 200);

    gtk_stack_add_named(GTK_STACK(m_stack), createDiscoveryScreen(), "discovery");
    gtk_stack_add_named(GTK_STACK(m_stack), createTransfersScreen(), "transfers");
    gtk_stack_add_named(GTK_STACK(m_stack), createStorageScreen(), "storage");
    gtk_stack_add_named(GTK_STACK(m_stack), createSettingsScreen(), "settings");

    gtk_box_pack_start(GTK_BOX(mainBox), m_stack, TRUE, TRUE, 0);

    if (m_isMobileMode) {
        m_statusLabel = gtk_label_new("Status: Ready.");
        gtk_label_set_ellipsize(GTK_LABEL(m_statusLabel), PANGO_ELLIPSIZE_END);
        gtk_widget_set_opacity(m_statusLabel, 0.7);
        gtk_widget_set_margin_start(m_statusLabel, 12);
        gtk_widget_set_margin_end(m_statusLabel, 12);
        gtk_widget_set_margin_bottom(m_statusLabel, 4);
        gtk_box_pack_start(GTK_BOX(mainBox), m_statusLabel, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mainBox), createMobileBottomBar(), FALSE, FALSE, 0);
    } else {
        GtkWidget* footerBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(footerBar), 6);
        m_statusLabel = gtk_label_new("Status: Ready. Listening for LAN peers.");
        gtk_box_pack_start(GTK_BOX(footerBar), m_statusLabel, FALSE, FALSE, 8);

        GtkWidget* verLabel = gtk_label_new("Port: 8848 | Local Discovery");
        gtk_widget_set_opacity(verLabel, 0.6);
        gtk_box_pack_end(GTK_BOX(footerBar), verLabel, FALSE, FALSE, 8);
        gtk_box_pack_start(GTK_BOX(mainBox), footerBar, FALSE, FALSE, 0);
    }
}

void GtkDesktopApp::chooseAndSendFile(const std::string& peerId) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Select File to Send via LAN-Drop",
        GTK_WINDOW(m_mainWindow),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Send", GTK_RESPONSE_ACCEPT,
        nullptr
    );

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            std::string target = peerId;
            if (target.empty()) {
                auto selected = m_discoveryPresenter.getSelectedPeer();
                if (selected) {
                    target = selected->getId();
                } else if (!m_selectedPeerId.empty()) {
                    target = m_selectedPeerId;
                } else if (!m_discoveryPresenter.getPeers().empty()) {
                    target = m_discoveryPresenter.getPeers().front()->getId();
                } else {
                    target = "peer-direct-127.0.0.1";
                }
            }
            m_transferPresenter.onSendFileClicked(target, filename);
            switchToTab("transfers");
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

void GtkDesktopApp::applyStoragePathFromEntry() {
    if (!m_storagePathEntry) return;
    const char* text = gtk_entry_get_text(GTK_ENTRY(m_storagePathEntry));
    if (text && strlen(text) > 0) {
        m_storagePresenter.onSharedPathChanged(text);
    }
}

GtkWidget* GtkDesktopApp::createDiscoveryScreen() {
    GtkWidget* rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 16);

    GtkWidget* topCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(topCard), "apple-card");

    GtkWidget* dotLabel = gtk_label_new("●");
    gtk_style_context_add_class(gtk_widget_get_style_context(dotLabel), "status-badge");

    GtkWidget* radarTitle = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(radarTitle), "<b>Radar Scanner</b>");

    m_scanBadge = gtk_label_new("Active");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_scanBadge), "pill-active");
    GtkWidget* activeBadge = m_scanBadge;

    m_scanStatusLabel = gtk_label_new("Listening on LAN port 8848 (0 devices found)");
    gtk_widget_set_opacity(m_scanStatusLabel, 0.7);

    GtkWidget* refreshBtn = gtk_button_new_with_label("↺ Refresh");
    gtk_style_context_add_class(gtk_widget_get_style_context(refreshBtn), "apple-accent-btn");
    g_signal_connect(refreshBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getDiscoveryPresenter().onRefreshDevicesClicked();
    }), this);

    gtk_box_pack_start(GTK_BOX(topCard), dotLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), radarTitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), activeBadge, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), m_scanStatusLabel, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(topCard), refreshBtn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(rootBox), topCard, FALSE, FALSE, 0);

    // My Device identity card
    GtkWidget* myDeviceCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(myDeviceCard), "apple-card");

    GtkWidget* myIcon = gtk_label_new("📡");
    gtk_style_context_add_class(gtk_widget_get_style_context(myIcon), "device-icon-box");
    gtk_widget_set_size_request(myIcon, 38, 38);

    GtkWidget* myInfoBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* myTitle = gtk_label_new(nullptr);
    gchar* escName = g_markup_escape_text(m_discoveryPresenter.getLocalDeviceName().c_str(), -1);
    std::string myTitleText = "<b>" + std::string(escName ? escName : "") + " (This Device)</b>";
    gtk_label_set_markup(GTK_LABEL(myTitle), myTitleText.c_str());
    if (escName) g_free(escName);
    gtk_label_set_xalign(GTK_LABEL(myTitle), 0.0f);

    GtkWidget* mySub = gtk_label_new(nullptr);
    std::string mySubText = "Ready to receive at " + m_discoveryPresenter.getLocalDeviceIp() + ":" + std::to_string(m_discoveryPresenter.getLocalDevicePort()) + "  •  Discoverable on local network";
    gtk_label_set_text(GTK_LABEL(mySub), mySubText.c_str());
    gtk_widget_set_opacity(mySub, 0.7);
    gtk_label_set_xalign(GTK_LABEL(mySub), 0.0f);

    gtk_box_pack_start(GTK_BOX(myInfoBox), myTitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(myInfoBox), mySub, FALSE, FALSE, 0);

    GtkWidget* myPill = gtk_label_new("Discoverable");
    gtk_style_context_add_class(gtk_widget_get_style_context(myPill), "pill-active");

    gtk_box_pack_start(GTK_BOX(myDeviceCard), myIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(myDeviceCard), myInfoBox, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(myDeviceCard), myPill, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(rootBox), myDeviceCard, FALSE, FALSE, 0);

    m_deviceGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(m_deviceGrid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(m_deviceGrid), 12);
    gtk_grid_set_row_homogeneous(GTK_GRID(m_deviceGrid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(m_deviceGrid), TRUE);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroll), m_deviceGrid);
    gtk_box_pack_start(GTK_BOX(rootBox), scroll, TRUE, TRUE, 0);

    GtkWidget* directCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(directCard), "apple-card");

    GtkWidget* directIcon = gtk_label_new("🌐");
    gtk_style_context_add_class(gtk_widget_get_style_context(directIcon), "device-icon-box");
    gtk_widget_set_size_request(directIcon, 42, 42);

    GtkWidget* directBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* directTitle = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(directTitle), "<b>Direct Peer Connection</b>");
    gtk_label_set_xalign(GTK_LABEL(directTitle), 0.0f);

    GtkWidget* directSub = gtk_label_new("Connect directly via IP and port for routed LANs or isolated subnets");
    gtk_widget_set_opacity(directSub, 0.65);
    gtk_label_set_xalign(GTK_LABEL(directSub), 0.0f);

    GtkWidget* directInputs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    m_directIpEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(m_directIpEntry), "Peer IP Address or Hostname (e.g. 192.168.1.100)");

    m_directPortSpin = gtk_spin_button_new_with_range(1024, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(m_directPortSpin), 8848);

    GtkWidget* connectBtn = gtk_button_new_with_label("Connect Peer");
    gtk_style_context_add_class(gtk_widget_get_style_context(connectBtn), "apple-accent-btn");

    GtkWidget* directSendBtn = gtk_button_new_with_label("Send File...");
    gtk_style_context_add_class(gtk_widget_get_style_context(directSendBtn), "apple-accent-btn");

    g_signal_connect(connectBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        const char* ip = gtk_entry_get_text(GTK_ENTRY(app->m_directIpEntry));
        uint16_t port = static_cast<uint16_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->m_directPortSpin)));
        if (ip && strlen(ip) > 0) {
            app->getDiscoveryPresenter().onDirectConnectRequested(ip, port);
        } else {
            app->showNotification("Please enter a valid IP address or hostname.");
        }
    }), this);

    g_signal_connect(directSendBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        const char* ip = gtk_entry_get_text(GTK_ENTRY(app->m_directIpEntry));
        uint16_t port = static_cast<uint16_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->m_directPortSpin)));
        if (ip && strlen(ip) > 0) {
            app->getDiscoveryPresenter().onDirectConnectRequested(ip, port);
        }
        app->chooseAndSendFile("");
    }), this);

    gtk_box_pack_start(GTK_BOX(directInputs), m_directIpEntry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(directInputs), m_directPortSpin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(directInputs), connectBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(directInputs), directSendBtn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(directBox), directTitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(directBox), directSub, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(directBox), directInputs, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(directCard), directIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(directCard), directBox, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(rootBox), directCard, FALSE, FALSE, 0);

    return rootBox;
}

GtkWidget* GtkDesktopApp::createTransfersScreen() {
    GtkWidget* rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 18);

    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "apple-card");

    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Active Transfers (Table Container):</b>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_pack_start(GTK_BOX(card), title, FALSE, FALSE, 4);

    m_transferListStore = gtk_list_store_new(7,
        G_TYPE_STRING,
        G_TYPE_STRING,
        G_TYPE_STRING,
        G_TYPE_INT,
        G_TYPE_STRING,
        G_TYPE_STRING,
        G_TYPE_STRING
    );

    m_transferTreeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(m_transferListStore));

    GtkCellRenderer* rendererText = gtk_cell_renderer_text_new();
    GtkCellRenderer* rendererProgress = gtk_cell_renderer_progress_new();

    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_transferTreeView), -1, "ID", rendererText, "text", 0, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_transferTreeView), -1, "File Name", rendererText, "text", 1, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_transferTreeView), -1, "Size", rendererText, "text", 2, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_transferTreeView), -1, "Progress", rendererProgress, "value", 3, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_transferTreeView), -1, "Speed", rendererText, "text", 4, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_transferTreeView), -1, "Status", rendererText, "text", 5, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_transferTreeView), -1, "Direction", rendererText, "text", 6, nullptr);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 260);
    gtk_container_add(GTK_CONTAINER(scroll), m_transferTreeView);
    gtk_box_pack_start(GTK_BOX(card), scroll, TRUE, TRUE, 0);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(m_transferTreeView));
    g_signal_connect(selection, "changed", G_CALLBACK(+[](GtkTreeSelection* sel, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
            gchar* tid = nullptr;
            gchar* fname = nullptr;
            gchar* st = nullptr;
            gtk_tree_model_get(model, &iter, 0, &tid, 1, &fname, 5, &st, -1);
            if (tid) {
                app->m_selectedTaskId = tid;
                if (app->m_selectedTransferLabel) {
                    std::string s = "Selected: " + std::string(fname ? fname : "") + " (" + tid + ") | State: " + (st ? st : "");
                    gtk_label_set_text(GTK_LABEL(app->m_selectedTransferLabel), s.c_str());
                }
                g_free(tid);
            }
            if (fname) g_free(fname);
            if (st) g_free(st);

            GtkTreePath* path = gtk_tree_model_get_path(model, &iter);
            if (path) {
                int* indices = gtk_tree_path_get_indices(path);
                if (indices) {
                    app->getTransferPresenter().onSelectTransferRow(static_cast<size_t>(indices[0]));
                }
                gtk_tree_path_free(path);
            }
        } else {
            app->m_selectedTaskId = "";
            if (app->m_selectedTransferLabel) {
                gtk_label_set_text(GTK_LABEL(app->m_selectedTransferLabel), "Selection: None (queue empty or no row selected).");
            }
        }
        app->updateTransferButtonStates();
    }), this);

    m_selectedTransferLabel = gtk_label_new("Selection: None (queue empty or no row selected)");
    gtk_label_set_xalign(GTK_LABEL(m_selectedTransferLabel), 0.0f);
    gtk_box_pack_start(GTK_BOX(card), m_selectedTransferLabel, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(rootBox), card, TRUE, TRUE, 0);

    GtkWidget* actionCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(actionCard), "apple-card");

    m_pauseBtn = gtk_button_new_with_label("⏸ Pause");
    m_resumeBtn = gtk_button_new_with_label("▶ Resume");
    m_cancelBtn = gtk_button_new_with_label("✕ Cancel");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_cancelBtn), "apple-danger-btn");

    GtkWidget* sendBtn = gtk_button_new_with_label("Send File...");
    gtk_style_context_add_class(gtk_widget_get_style_context(sendBtn), "apple-accent-btn");
    GtkWidget* clearBtn = gtk_button_new_with_label("Clear Completed");

    g_signal_connect(m_pauseBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        if (!app->m_selectedTaskId.empty()) {
            app->getTransferPresenter().onPauseTransferClicked(app->m_selectedTaskId);
        }
    }), this);

    g_signal_connect(m_resumeBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        if (!app->m_selectedTaskId.empty()) {
            app->getTransferPresenter().onResumeTransferClicked(app->m_selectedTaskId);
        }
    }), this);

    g_signal_connect(m_cancelBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        if (!app->m_selectedTaskId.empty()) {
            app->getTransferPresenter().onCancelTransferClicked(app->m_selectedTaskId);
        }
    }), this);

    g_signal_connect(sendBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->chooseAndSendFile("");
    }), this);

    g_signal_connect(clearBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().onClearHistoryClicked();
    }), this);

    gtk_box_pack_start(GTK_BOX(actionCard), m_pauseBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actionCard), m_resumeBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actionCard), m_cancelBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actionCard), sendBtn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(actionCard), clearBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), actionCard, FALSE, FALSE, 0);

    updateTransferButtonStates();

    return rootBox;
}

GtkWidget* GtkDesktopApp::createStorageScreen() {
    GtkWidget* rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 18);

    GtkWidget* topCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(topCard), "apple-card");

    GtkWidget* pathLabel = gtk_label_new("📁 Shared Folder:");
    m_storagePathEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(m_storagePathEntry), "./shared_drop");
    g_signal_connect(m_storagePathEntry, "activate", G_CALLBACK(+[](GtkEntry*, gpointer data) {
        static_cast<GtkDesktopApp*>(data)->applyStoragePathFromEntry();
    }), this);

    GtkWidget* browseBtn = gtk_button_new_with_label("Browse...");
    g_signal_connect(browseBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Select Shared Directory",
            GTK_WINDOW(app->m_mainWindow),
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Select", GTK_RESPONSE_ACCEPT,
            nullptr
        );
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (folder) {
                gtk_entry_set_text(GTK_ENTRY(app->m_storagePathEntry), folder);
                app->getStoragePresenter().onSharedPathChanged(folder);
                g_free(folder);
            }
        }
        gtk_widget_destroy(dialog);
    }), this);

    GtkWidget* rescanBtn = gtk_button_new_with_label("↺ Rescan");
    gtk_style_context_add_class(gtk_widget_get_style_context(rescanBtn), "apple-accent-btn");

    g_signal_connect(rescanBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->applyStoragePathFromEntry();
        app->getStoragePresenter().onRescanClicked();
    }), this);

    GtkWidget* addFileBtn = gtk_button_new_with_label("+ Add File");
    g_signal_connect(addFileBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Add File to Shared Storage",
            GTK_WINDOW(app->m_mainWindow),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Add", GTK_RESPONSE_ACCEPT,
            nullptr
        );
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (file) {
                app->getStoragePresenter().onAddFileClicked(file);
                g_free(file);
            }
        }
        gtk_widget_destroy(dialog);
    }), this);

    GtkWidget* openFolderBtn = gtk_button_new_with_label("📂 Open Folder");
    g_signal_connect(openFolderBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        const char* path = gtk_entry_get_text(GTK_ENTRY(app->m_storagePathEntry));
        if (path && strlen(path) > 0) {
            char* uri = g_filename_to_uri(path, nullptr, nullptr);
            if (uri) {
                g_app_info_launch_default_for_uri(uri, nullptr, nullptr);
                g_free(uri);
            }
        }
    }), this);

    gtk_box_pack_start(GTK_BOX(topCard), pathLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), m_storagePathEntry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), browseBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), rescanBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), openFolderBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), addFileBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), topCard, FALSE, FALSE, 0);

    GtkWidget* listCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(listCard), "apple-card");

    GtkWidget* listHeaderBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    m_storageStatsLabel = gtk_label_new("● 0 Files  |  0 KB Total  |  🔓 Open Access  |  Chunked Transfer");
    gtk_label_set_xalign(GTK_LABEL(m_storageStatsLabel), 0.0f);
    gtk_box_pack_start(GTK_BOX(listHeaderBox), m_storageStatsLabel, TRUE, TRUE, 4);

    m_removeFileBtn = gtk_button_new_with_label("✕ Remove File");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_removeFileBtn), "apple-danger-btn");
    gtk_widget_set_sensitive(m_removeFileBtn, FALSE);
    g_signal_connect(m_removeFileBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        if (!app->m_selectedSharedFile.empty()) {
            app->getStoragePresenter().onRemoveFileClicked(app->m_selectedSharedFile);
            app->m_selectedSharedFile = "";
            gtk_widget_set_sensitive(app->m_removeFileBtn, FALSE);
        }
    }), this);
    gtk_box_pack_end(GTK_BOX(listHeaderBox), m_removeFileBtn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(listCard), listHeaderBox, FALSE, FALSE, 4);

    m_fileListStore = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    m_fileTreeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(m_fileListStore));

    GtkCellRenderer* r = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_fileTreeView), -1, "File Name", r, "text", 0, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_fileTreeView), -1, "Size", r, "text", 1, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_fileTreeView), -1, "Chunks", r, "text", 2, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_fileTreeView), -1, "SHA-256", r, "text", 3, nullptr);

    GtkTreeSelection* fileSel = gtk_tree_view_get_selection(GTK_TREE_VIEW(m_fileTreeView));
    g_signal_connect(fileSel, "changed", G_CALLBACK(+[](GtkTreeSelection* sel, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
            gchar* rawName = nullptr;
            gtk_tree_model_get(model, &iter, 4, &rawName, -1);
            if (rawName) {
                app->m_selectedSharedFile = rawName;
                g_free(rawName);
            }
        } else {
            app->m_selectedSharedFile = "";
        }
        if (app->m_removeFileBtn) {
            gtk_widget_set_sensitive(app->m_removeFileBtn, !app->m_selectedSharedFile.empty());
        }
    }), this);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 240);
    gtk_container_add(GTK_CONTAINER(scroll), m_fileTreeView);
    gtk_box_pack_start(GTK_BOX(listCard), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), listCard, TRUE, TRUE, 0);

    GtkWidget* passCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(passCard), "apple-card");

    GtkWidget* passIcon = gtk_label_new("🔐");
    gtk_style_context_add_class(gtk_widget_get_style_context(passIcon), "device-icon-box");
    gtk_widget_set_size_request(passIcon, 36, 36);

    GtkWidget* passTextBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* passTitle = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(passTitle), "<b>Access Protection</b>");
    gtk_label_set_xalign(GTK_LABEL(passTitle), 0.0f);
    GtkWidget* passSub = gtk_label_new("PIN or password required to access storage");
    gtk_widget_set_opacity(passSub, 0.65);
    gtk_label_set_xalign(GTK_LABEL(passSub), 0.0f);
    gtk_box_pack_start(GTK_BOX(passTextBox), passTitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(passTextBox), passSub, FALSE, FALSE, 0);

    m_passwordEntry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(m_passwordEntry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(m_passwordEntry), "PIN");

    GtkWidget* setPassBtn = gtk_button_new_with_label("Set Password");
    GtkWidget* clearPassBtn = gtk_button_new_with_label("Clear");

    g_signal_connect(setPassBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        const char* p = gtk_entry_get_text(GTK_ENTRY(app->m_passwordEntry));
        app->getStoragePresenter().onSetPasswordClicked(p);
    }), this);

    g_signal_connect(clearPassBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        gtk_entry_set_text(GTK_ENTRY(app->m_passwordEntry), "");
        app->getStoragePresenter().onClearPasswordClicked();
    }), this);

    gtk_box_pack_start(GTK_BOX(passCard), passIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(passCard), passTextBox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(passCard), m_passwordEntry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(passCard), setPassBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(passCard), clearPassBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), passCard, FALSE, FALSE, 0);

    return rootBox;
}

GtkWidget* GtkDesktopApp::createSettingsScreen() {
    GtkWidget* rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 18);

    GtkWidget* confCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(confCard), "apple-card");

    GtkWidget* portBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* portLabel = gtk_label_new("Listen Port:");
    m_portSpin = gtk_spin_button_new_with_range(1024, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(m_portSpin), 8848);
    gtk_box_pack_start(GTK_BOX(portBox), portLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(portBox), m_portSpin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(confCard), portBox, FALSE, FALSE, 0);

    GtkWidget* speedBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* speedLabelBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* speedLabel = gtk_label_new("Speed Limit:");
    gtk_label_set_xalign(GTK_LABEL(speedLabel), 0.0f);
    GtkWidget* speedSub = gtk_label_new("Maximum transfer rate (0 = unlimited)");
    gtk_widget_set_opacity(speedSub, 0.65);
    gtk_label_set_xalign(GTK_LABEL(speedSub), 0.0f);
    gtk_box_pack_start(GTK_BOX(speedLabelBox), speedLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(speedLabelBox), speedSub, FALSE, FALSE, 0);

    m_speedScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 50, 1);
    gtk_scale_set_draw_value(GTK_SCALE(m_speedScale), FALSE);
    gtk_range_set_value(GTK_RANGE(m_speedScale), 25);
    m_speedValueLabel = gtk_label_new("25 MB/s");
    gtk_widget_set_size_request(m_speedValueLabel, 80, -1);
    g_signal_connect(m_speedScale, "value-changed", G_CALLBACK(+[](GtkRange* r, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        int val = static_cast<int>(gtk_range_get_value(r));
        if (app->m_speedValueLabel) {
            std::string s = (val == 0) ? "Unlimited" : (std::to_string(val) + " MB/s");
            gtk_label_set_text(GTK_LABEL(app->m_speedValueLabel), s.c_str());
        }
    }), this);

    gtk_box_pack_start(GTK_BOX(speedBox), speedLabelBox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(speedBox), m_speedScale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(speedBox), m_speedValueLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(confCard), speedBox, FALSE, FALSE, 0);

    GtkWidget* autoBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* autoLabelBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* autoLabel = gtk_label_new("Auto-Accept Transfers:");
    gtk_label_set_xalign(GTK_LABEL(autoLabel), 0.0f);
    GtkWidget* autoSub = gtk_label_new("Accept incoming files without confirmation prompt");
    gtk_widget_set_opacity(autoSub, 0.65);
    gtk_label_set_xalign(GTK_LABEL(autoSub), 0.0f);
    gtk_box_pack_start(GTK_BOX(autoLabelBox), autoLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(autoLabelBox), autoSub, FALSE, FALSE, 0);

    m_autoAcceptSwitch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(m_autoAcceptSwitch), FALSE);
    gtk_box_pack_start(GTK_BOX(autoBox), autoLabelBox, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(autoBox), m_autoAcceptSwitch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(confCard), autoBox, FALSE, FALSE, 0);

    GtkWidget* airdropBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* airdropLabelBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* airdropLabel = gtk_label_new("Multicast Beacon Discovery (LAN Broadcast):");
    gtk_label_set_xalign(GTK_LABEL(airdropLabel), 0.0f);
    GtkWidget* airdropSub = gtk_label_new("Broadcast UDP discovery beacons on local subnet port 8848");
    gtk_widget_set_opacity(airdropSub, 0.65);
    gtk_label_set_xalign(GTK_LABEL(airdropSub), 0.0f);
    gtk_box_pack_start(GTK_BOX(airdropLabelBox), airdropLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(airdropLabelBox), airdropSub, FALSE, FALSE, 0);

    m_airdropSwitch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(m_airdropSwitch), TRUE);
    gtk_box_pack_start(GTK_BOX(airdropBox), airdropLabelBox, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(airdropBox), m_airdropSwitch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(confCard), airdropBox, FALSE, FALSE, 0);

    GtkWidget* saveBtn = gtk_button_new_with_label("Save Preferences");
    gtk_style_context_add_class(gtk_widget_get_style_context(saveBtn), "apple-accent-btn");
    g_signal_connect(saveBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getSettingsPresenter().onPortChanged(static_cast<uint16_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->m_portSpin))));
        uint32_t speedMb = static_cast<uint32_t>(gtk_range_get_value(GTK_RANGE(app->m_speedScale)));
        app->getSettingsPresenter().onSpeedLimitChanged(speedMb * 1000);
        app->getSettingsPresenter().onAutoAcceptToggled(gtk_switch_get_active(GTK_SWITCH(app->m_autoAcceptSwitch)));
        app->getSettingsPresenter().onAirDropToggled(gtk_switch_get_active(GTK_SWITCH(app->m_airdropSwitch)));
        app->getSettingsPresenter().onSaveSettingsClicked();
    }), this);
    gtk_box_pack_start(GTK_BOX(confCard), saveBtn, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(rootBox), confCard, FALSE, FALSE, 0);

    GtkWidget* termContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(termContainer), "mac-terminal-card");

    GtkWidget* termTopBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(termTopBar), "mac-terminal-header");

    GtkWidget* termIcon = gtk_label_new("💻");
    gtk_box_pack_start(GTK_BOX(termTopBar), termIcon, FALSE, FALSE, 2);

    GtkWidget* termTitle = gtk_label_new("landrop — diagnostics — active");
    gtk_style_context_add_class(gtk_widget_get_style_context(termTitle), "mac-terminal-title");
    gtk_box_pack_start(GTK_BOX(termTopBar), termTitle, FALSE, FALSE, 6);

    GtkWidget* clearLogsBtn = gtk_button_new_with_label("Clear");
    gtk_style_context_add_class(gtk_widget_get_style_context(clearLogsBtn), "theme-btn");
    g_signal_connect(clearLogsBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer appPtr) {
        auto* app = static_cast<GtkDesktopApp*>(appPtr);
        if (app->m_logBuffer) {
            gtk_text_buffer_set_text(app->m_logBuffer, "", 0);
        }
        app->getSettingsPresenter().onClearLogsClicked();
    }), this);
    gtk_box_pack_end(GTK_BOX(termTopBar), clearLogsBtn, FALSE, FALSE, 4);

    GtkWidget* termPulse = gtk_label_new("● Live");
    gtk_style_context_add_class(gtk_widget_get_style_context(termPulse), "status-badge");
    gtk_box_pack_end(GTK_BOX(termTopBar), termPulse, FALSE, FALSE, 6);

    gtk_box_pack_start(GTK_BOX(termContainer), termTopBar, FALSE, FALSE, 0);

    m_logTextView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(m_logTextView), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(m_logTextView), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_logTextView), "mac-terminal-view");
    m_logBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m_logTextView));

    GtkWidget* logScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(logScroll), 180);
    gtk_container_add(GTK_CONTAINER(logScroll), m_logTextView);
    gtk_box_pack_start(GTK_BOX(termContainer), logScroll, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(rootBox), termContainer, TRUE, TRUE, 0);

    return rootBox;
}

void GtkDesktopApp::updateDeviceList(const std::vector<DeviceViewModel>& devices) {
    struct DeviceUpdateContext {
        GtkDesktopApp* app;
        std::vector<DeviceViewModel> devs;
    };
    auto* ctx = new DeviceUpdateContext{this, devices};
    g_idle_add(+[](gpointer p) -> gboolean {
        auto* c = static_cast<DeviceUpdateContext*>(p);
        c->app->updateDeviceListInternal(c->devs);
        delete c;
        return G_SOURCE_REMOVE;
    }, ctx);
}

void GtkDesktopApp::updateDeviceListInternal(const std::vector<DeviceViewModel>& devices) {
    if (!m_deviceGrid) return;

    GList* children = gtk_container_get_children(GTK_CONTAINER(m_deviceGrid));
    for (GList* iter = children; iter != nullptr; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    std::string activeSelId = m_selectedPeerId;
    auto selectedPeer = m_discoveryPresenter.getSelectedPeer();
    if (selectedPeer && activeSelId.empty()) {
        activeSelId = selectedPeer->getId();
    }

    if (devices.empty()) {
        GtkWidget* emptyCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_style_context_add_class(gtk_widget_get_style_context(emptyCard), "apple-card");
        gtk_widget_set_margin_top(emptyCard, 24);
        gtk_widget_set_margin_bottom(emptyCard, 24);
        gtk_widget_set_margin_start(emptyCard, 24);
        gtk_widget_set_margin_end(emptyCard, 24);

        GtkWidget* emptyIcon = gtk_label_new("📡");
        gtk_style_context_add_class(gtk_widget_get_style_context(emptyIcon), "device-icon-box");
        gtk_widget_set_size_request(emptyIcon, 56, 56);
        gtk_widget_set_halign(emptyIcon, GTK_ALIGN_CENTER);

        GtkWidget* emptyTitle = gtk_label_new(nullptr);
        gtk_label_set_markup(GTK_LABEL(emptyTitle), "<span size='large'><b>No LAN Devices Found Yet</b></span>");
        gtk_widget_set_halign(emptyTitle, GTK_ALIGN_CENTER);

        GtkWidget* emptyDesc = gtk_label_new("Ensure other devices are on the same Wi-Fi/Ethernet network with LAN-Drop open,\nor connect directly below using an IP address.");
        gtk_widget_set_opacity(emptyDesc, 0.7);
        gtk_widget_set_halign(emptyDesc, GTK_ALIGN_CENTER);
        gtk_label_set_justify(GTK_LABEL(emptyDesc), GTK_JUSTIFY_CENTER);

        GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_halign(btnBox, GTK_ALIGN_CENTER);

        GtkWidget* scanBtn = gtk_button_new_with_label("↺ Broadcast Scan");
        gtk_style_context_add_class(gtk_widget_get_style_context(scanBtn), "apple-accent-btn");
        g_signal_connect(scanBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* app = static_cast<GtkDesktopApp*>(data);
            app->getDiscoveryPresenter().onRefreshDevicesClicked();
        }), this);

        GtkWidget* loopbackBtn = gtk_button_new_with_label("💻 Add Loopback (127.0.0.1)");
        g_signal_connect(loopbackBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* app = static_cast<GtkDesktopApp*>(data);
            app->getDiscoveryPresenter().onDirectConnectRequested("127.0.0.1", 8848);
        }), this);

        gtk_box_pack_start(GTK_BOX(btnBox), scanBtn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(btnBox), loopbackBtn, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(emptyCard), emptyIcon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(emptyCard), emptyTitle, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(emptyCard), emptyDesc, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(emptyCard), btnBox, FALSE, FALSE, 4);

        gtk_grid_attach(GTK_GRID(m_deviceGrid), emptyCard, 0, 0, m_isMobileMode ? 1 : 2, 1);
        gtk_widget_show_all(m_deviceGrid);

        if (m_scanStatusLabel) {
            gtk_label_set_text(GTK_LABEL(m_scanStatusLabel), "Listening on LAN port 8848 (0 devices found)");
        }
        return;
    }

    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& dev = devices[i];

        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_style_context_add_class(gtk_widget_get_style_context(card), "apple-card");
        if (!activeSelId.empty() && dev.id == activeSelId) {
            gtk_style_context_add_class(gtk_widget_get_style_context(card), "device-card-selected");
        }

        GtkWidget* eventBox = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(eventBox), FALSE);
        g_object_set_data_full(G_OBJECT(eventBox), "peer_id", g_strdup(dev.id.c_str()), g_free);
        g_signal_connect(eventBox, "button-press-event", G_CALLBACK(+[](GtkWidget* eb, GdkEventButton*, gpointer data) -> gboolean {
            auto* app = static_cast<GtkDesktopApp*>(data);
            const char* pid = static_cast<const char*>(g_object_get_data(G_OBJECT(eb), "peer_id"));
            if (pid) {
                app->m_selectedPeerId = pid;
                app->getDiscoveryPresenter().onSelectDeviceClicked(pid);
            }
            return FALSE;
        }), this);

        GtkWidget* topRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

        std::string icon = "💻";
        if (dev.name.find("MacBook") != std::string::npos) icon = "💻";
        else if (dev.name.find("ThinkPad") != std::string::npos || dev.name.find("PC") != std::string::npos) icon = "🖥️";
        else if (dev.name.find("Galaxy") != std::string::npos || dev.name.find("Phone") != std::string::npos || dev.type == "Android" || dev.type == "iOS") icon = "📱";
        else if (dev.name.find("Cloud") != std::string::npos || dev.type == "Cloud Relay") icon = "☁️";

        GtkWidget* iconBox = gtk_label_new(icon.c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(iconBox), "device-icon-box");
        gtk_widget_set_size_request(iconBox, 42, 42);
        gtk_box_pack_start(GTK_BOX(topRow), iconBox, FALSE, FALSE, 0);

        GtkWidget* infoBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget* nameLabel = gtk_label_new(nullptr);
        gchar* escDevName = g_markup_escape_text(dev.name.c_str(), -1);
        std::string devNameMarkup = "<b>" + std::string(escDevName ? escDevName : "") + "</b>";
        gtk_label_set_markup(GTK_LABEL(nameLabel), devNameMarkup.c_str());
        if (escDevName) g_free(escDevName);
        gtk_label_set_xalign(GTK_LABEL(nameLabel), 0.0f);

        GtkWidget* epLabel = gtk_label_new(dev.endpoint.c_str());
        gtk_widget_set_opacity(epLabel, 0.65);
        gtk_label_set_xalign(GTK_LABEL(epLabel), 0.0f);

        gtk_box_pack_start(GTK_BOX(infoBox), nameLabel, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(infoBox), epLabel, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(topRow), infoBox, TRUE, TRUE, 0);

        int signal = (dev.signalRssi >= -40 ? 4 : (dev.signalRssi >= -60 ? 3 : (dev.signalRssi >= -75 ? 2 : 1)));
        if (dev.type == "Cloud Relay") signal = 4;

        GtkWidget* signalBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_widget_set_valign(signalBox, GTK_ALIGN_END);
        for (int bar = 1; bar <= 4; ++bar) {
            GtkWidget* b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            gtk_widget_set_size_request(b, 4, 4 + bar * 3);
            gtk_style_context_add_class(gtk_widget_get_style_context(b), bar <= signal ? "rssi-bar-active" : "rssi-bar-dim");
            gtk_box_pack_start(GTK_BOX(signalBox), b, FALSE, FALSE, 0);
        }
        gtk_box_pack_end(GTK_BOX(topRow), signalBox, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(card), topRow, FALSE, FALSE, 0);

        GtkWidget* botRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

        std::string osType = dev.type;
        if (dev.name.find("MacBook") != std::string::npos) osType = "macOS";
        else if (dev.name.find("ThinkPad") != std::string::npos) osType = "Windows";
        else if (dev.name.find("Galaxy") != std::string::npos) osType = "Android";
        else if (dev.name.find("Cloud") != std::string::npos) osType = "Cloud";

        GtkWidget* badge = gtk_label_new(osType.c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(badge), osType == "Cloud" ? "pill-cloud" : "pill-os");
        gtk_box_pack_start(GTK_BOX(botRow), badge, FALSE, FALSE, 0);

        if (dev.type == "Direct" || dev.id.find("direct") != std::string::npos) {
            GtkWidget* removeBtn = gtk_button_new_with_label("✕");
            gtk_style_context_add_class(gtk_widget_get_style_context(removeBtn), "apple-danger-btn");
            gtk_widget_set_tooltip_text(removeBtn, "Remove this direct peer");
            g_object_set_data_full(G_OBJECT(removeBtn), "device_id", g_strdup(dev.id.c_str()), g_free);
            g_signal_connect(removeBtn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer appPtr) {
                auto* app = static_cast<GtkDesktopApp*>(appPtr);
                const char* devId = static_cast<const char*>(g_object_get_data(G_OBJECT(b), "device_id"));
                if (devId) {
                    app->getDiscoveryPresenter().onRemovePeerClicked(devId);
                }
            }), this);
            gtk_box_pack_end(GTK_BOX(botRow), removeBtn, FALSE, FALSE, 0);
        }

        GtkWidget* sendBtn = gtk_button_new_with_label("Send File");
        gtk_style_context_add_class(gtk_widget_get_style_context(sendBtn), "apple-accent-btn");
        g_object_set_data_full(G_OBJECT(sendBtn), "device_id", g_strdup(dev.id.c_str()), g_free);
        g_signal_connect(sendBtn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer appPtr) {
            auto* app = static_cast<GtkDesktopApp*>(appPtr);
            const char* devId = static_cast<const char*>(g_object_get_data(G_OBJECT(b), "device_id"));
            if (!devId) return;
            app->m_selectedPeerId = devId;
            app->getDiscoveryPresenter().onSelectDeviceClicked(devId);
            app->chooseAndSendFile(devId);
        }), this);

        gtk_box_pack_end(GTK_BOX(botRow), sendBtn, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(card), botRow, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(eventBox), card);

        int col = m_isMobileMode ? 0 : static_cast<int>(i % 2);
        int row = m_isMobileMode ? static_cast<int>(i) : static_cast<int>(i / 2);
        gtk_grid_attach(GTK_GRID(m_deviceGrid), eventBox, col, row, 1, 1);
    }

    gtk_widget_show_all(m_deviceGrid);

    if (m_scanStatusLabel) {
        std::string s = std::to_string(devices.size()) + " devices found";
        gtk_label_set_text(GTK_LABEL(m_scanStatusLabel), s.c_str());
    }
}

void GtkDesktopApp::setScanningStatus(bool isScanning) {
    struct ScanStatusContext {
        GtkDesktopApp* app;
        bool scanning;
    };
    auto* ctx = new ScanStatusContext{this, isScanning};
    g_idle_add(+[](gpointer p) -> gboolean {
        auto* c = static_cast<ScanStatusContext*>(p);
        if (c->app->m_scanStatusLabel) {
            std::string s = "Radar Scanner: " + std::string(c->scanning ? "Active (Listening for LAN Beacons)" : "Idle");
            gtk_label_set_text(GTK_LABEL(c->app->m_scanStatusLabel), s.c_str());
        }
        if (c->app->m_scanBadge) {
            gtk_label_set_text(GTK_LABEL(c->app->m_scanBadge), c->scanning ? "Active" : "Paused");
            auto* style = gtk_widget_get_style_context(c->app->m_scanBadge);
            if (c->scanning) {
                gtk_style_context_remove_class(style, "pill-os");
                gtk_style_context_add_class(style, "pill-active");
            } else {
                gtk_style_context_remove_class(style, "pill-active");
                gtk_style_context_add_class(style, "pill-os");
            }
        }
        delete c;
        return G_SOURCE_REMOVE;
    }, ctx);
}

void GtkDesktopApp::showNfcPairResult(bool success, const std::string& message) {
    showNotification((success ? "NFC Pair OK: " : "NFC Pair Failed: ") + message);
}

void GtkDesktopApp::showNotification(const std::string& text) {
    struct NotifContext {
        GtkDesktopApp* app;
        std::string msg;
    };
    auto* ctx = new NotifContext{this, text};
    g_idle_add(+[](gpointer p) -> gboolean {
        auto* c = static_cast<NotifContext*>(p);
        if (c->app->m_statusLabel) {
            std::string s = "Status: " + c->msg;
            gtk_label_set_text(GTK_LABEL(c->app->m_statusLabel), s.c_str());
        }
        delete c;
        return G_SOURCE_REMOVE;
    }, ctx);
}

void GtkDesktopApp::updateTransferTable(const std::vector<TransferViewModel>& transfers) {
    if (!m_transferListStore) return;

    if (transfers.empty()) {
        gtk_list_store_clear(m_transferListStore);
        m_selectedTaskId = "";
        if (m_selectedTransferLabel) {
            gtk_label_set_text(GTK_LABEL(m_selectedTransferLabel), "Selection: None (queue empty).");
        }
        updateTransferButtonStates();
        return;
    }

    // Map existing rows by taskId -> GtkTreeIter
    std::unordered_map<std::string, GtkTreeIter> existingIters;
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(m_transferListStore), &iter);
    while (valid) {
        gchar* idStr = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(m_transferListStore), &iter, 0, &idStr, -1);
        if (idStr) {
            existingIters[idStr] = iter;
            g_free(idStr);
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(m_transferListStore), &iter);
    }

    std::unordered_set<std::string> currentIds;
    GtkTreeIter selectedIter;
    bool hasSelected = false;

    for (const auto& t : transfers) {
        currentIds.insert(t.taskId);

        std::string sizeStr;
        if (t.fileSize >= 1024ULL * 1024 * 1024) {
            sizeStr = std::to_string(t.fileSize / (1024ULL * 1024 * 1024)) + "." + std::to_string((t.fileSize % (1024ULL * 1024 * 1024)) / (100ULL * 1024 * 1024)) + " GB";
        } else if (t.fileSize >= 1024 * 1024) {
            sizeStr = std::to_string(t.fileSize / (1024 * 1024)) + "." + std::to_string((t.fileSize % (1024 * 1024)) / (100 * 1024)) + " MB";
        } else {
            sizeStr = std::to_string(t.fileSize / 1024) + " KB";
        }

        std::string speedStr;
        if (t.speedBytesPerSec >= 1024.0 * 1024.0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f MB/s", t.speedBytesPerSec / (1024.0 * 1024.0));
            speedStr = buf;
        } else if (t.speedBytesPerSec > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f KB/s", t.speedBytesPerSec / 1024.0);
            speedStr = buf;
        } else {
            speedStr = "0 KB/s";
        }

        GtkTreeIter rowIter;
        auto it = existingIters.find(t.taskId);
        if (it != existingIters.end()) {
            rowIter = it->second;
        } else {
            gtk_list_store_append(m_transferListStore, &rowIter);
        }

        gtk_list_store_set(m_transferListStore, &rowIter,
            0, t.taskId.c_str(),
            1, t.fileName.c_str(),
            2, sizeStr.c_str(),
            3, static_cast<int>(t.progress),
            4, speedStr.c_str(),
            5, t.stateStr.c_str(),
            6, t.direction.c_str(),
            -1
        );

        if (!m_selectedTaskId.empty() && t.taskId == m_selectedTaskId) {
            selectedIter = rowIter;
            hasSelected = true;
        }
    }

    // Remove rows that no longer exist in transfers
    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(m_transferListStore), &iter);
    while (valid) {
        gchar* idStr = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(m_transferListStore), &iter, 0, &idStr, -1);
        if (idStr) {
            std::string id(idStr);
            g_free(idStr);
            if (currentIds.find(id) == currentIds.end()) {
                valid = gtk_list_store_remove(m_transferListStore, &iter);
                continue;
            }
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(m_transferListStore), &iter);
    }

    if (hasSelected && m_transferTreeView) {
        GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(m_transferTreeView));
        gtk_tree_selection_select_iter(sel, &selectedIter);
    } else if (!m_selectedTaskId.empty() && !hasSelected) {
        m_selectedTaskId = "";
        if (m_selectedTransferLabel) {
            gtk_label_set_text(GTK_LABEL(m_selectedTransferLabel), "Selection: None (queue empty or row deselected).");
        }
    }
    updateTransferButtonStates();
}

void GtkDesktopApp::updateTransferButtonStates() {
    bool hasSelection = !m_selectedTaskId.empty();
    if (m_pauseBtn) gtk_widget_set_sensitive(m_pauseBtn, hasSelection);
    if (m_resumeBtn) gtk_widget_set_sensitive(m_resumeBtn, hasSelection);
    if (m_cancelBtn) gtk_widget_set_sensitive(m_cancelBtn, hasSelection);
}

void GtkDesktopApp::updateSelectedTransferDetails(const TransferViewModel& item) {
    if (m_selectedTransferLabel) {
        if (!m_selectedTaskId.empty() && item.taskId != m_selectedTaskId) {
            return;
        }
        if (item.taskId.empty()) {
            gtk_label_set_text(GTK_LABEL(m_selectedTransferLabel), "Selection: None (queue empty or no row selected).");
        } else {
            std::string s = "Selected: " + item.fileName + " (" + item.taskId + ") | State: " + item.stateStr;
            gtk_label_set_text(GTK_LABEL(m_selectedTransferLabel), s.c_str());
        }
    }
}

void GtkDesktopApp::showTransferError(const std::string& taskId, const std::string& error) {
    showNotification("Error in " + taskId + ": " + error);
}

void GtkDesktopApp::setSharedPath(const std::string& path) {
    if (m_storagePathEntry) {
        gtk_entry_set_text(GTK_ENTRY(m_storagePathEntry), path.c_str());
    }
}

void GtkDesktopApp::updateFileList(const std::vector<FileMetadata>& files) {
    if (!m_fileListStore) return;
    gtk_list_store_clear(m_fileListStore);

    for (const auto& f : files) {
        GtkTreeIter iter;
        gtk_list_store_append(m_fileListStore, &iter);

        std::string icon = "📄 ";
        if (f.fileName.ends_with(".fig") || f.fileName.ends_with(".png") || f.fileName.ends_with(".jpg")) icon = "🎨 ";
        else if (f.fileName.ends_with(".tar.gz") || f.fileName.ends_with(".zip") || f.fileName.ends_with(".tar")) icon = "📦 ";
        else if (f.fileName.ends_with(".mp4") || f.fileName.ends_with(".mov") || f.fileName.ends_with(".mkv")) icon = "🎬 ";
        else if (f.fileName.ends_with(".pdf") || f.fileName.ends_with(".doc") || f.fileName.ends_with(".txt")) icon = "📄 ";
        else icon = "🗂️ ";

        std::string nameWithIcon = icon + f.fileName;
        std::string sizeStr;
        if (f.fileSize >= 1024ULL * 1024 * 1024) {
            sizeStr = std::to_string(f.fileSize / (1024ULL * 1024 * 1024)) + "." + std::to_string((f.fileSize % (1024ULL * 1024 * 1024)) / (100ULL * 1024 * 1024)) + " GB";
        } else if (f.fileSize >= 1024 * 1024) {
            sizeStr = std::to_string(f.fileSize / (1024 * 1024)) + "." + std::to_string((f.fileSize % (1024 * 1024)) / (100 * 1024)) + " MB";
        } else {
            sizeStr = std::to_string(f.fileSize / 1024) + " KB";
        }

        std::string chunksStr = std::to_string(f.totalChunks);
        std::string hashStr = f.fileSha256.substr(0, std::min<size_t>(16, f.fileSha256.size())) + "…";

        gtk_list_store_set(m_fileListStore, &iter,
            0, nameWithIcon.c_str(),
            1, sizeStr.c_str(),
            2, chunksStr.c_str(),
            3, hashStr.c_str(),
            4, f.fileName.c_str(),
            -1
        );
    }
}

void GtkDesktopApp::updateStorageStatsLabel() {
    if (!m_storageStatsLabel) return;
    std::string totalStr;
    if (m_storageTotalBytes >= 1024ULL * 1024 * 1024) {
        totalStr = std::to_string(m_storageTotalBytes / (1024ULL * 1024 * 1024)) + "." + std::to_string((m_storageTotalBytes % (1024ULL * 1024 * 1024)) / (100ULL * 1024 * 1024)) + " GB";
    } else if (m_storageTotalBytes >= 1024 * 1024) {
        totalStr = std::to_string(m_storageTotalBytes / (1024 * 1024)) + "." + std::to_string((m_storageTotalBytes % (1024 * 1024)) / (100 * 1024)) + " MB";
    } else {
        totalStr = std::to_string(m_storageTotalBytes / 1024) + " KB";
    }
    std::string secStr = m_storageIsProtected ? "🔐 PIN Protected" : "🔓 Open Access";
    std::string s = "● " + std::to_string(m_storageFileCount) + " Files  |  " + totalStr + " Total  |  " + secStr + "  |  Chunked Transfer";
    gtk_label_set_text(GTK_LABEL(m_storageStatsLabel), s.c_str());
}

void GtkDesktopApp::setStorageStats(uint64_t totalBytes, size_t fileCount) {
    m_storageTotalBytes = totalBytes;
    m_storageFileCount = fileCount;
    updateStorageStatsLabel();
}

void GtkDesktopApp::setPasswordProtectionStatus(bool isProtected) {
    m_storageIsProtected = isProtected;
    updateStorageStatsLabel();
}

void GtkDesktopApp::loadSettings(uint16_t port, uint32_t speedLimitKbps, bool autoAccept, bool airdropEnabled) {
    if (m_portSpin) gtk_spin_button_set_value(GTK_SPIN_BUTTON(m_portSpin), port);
    if (m_speedScale) {
        double speedMb = static_cast<double>(speedLimitKbps) / 1000.0;
        gtk_range_set_value(GTK_RANGE(m_speedScale), speedMb);
        if (m_speedValueLabel) {
            std::string s = (static_cast<int>(speedMb) == 0) ? "Unlimited" : (std::to_string(static_cast<int>(speedMb)) + " MB/s");
            gtk_label_set_text(GTK_LABEL(m_speedValueLabel), s.c_str());
        }
    }
    if (m_autoAcceptSwitch) gtk_switch_set_active(GTK_SWITCH(m_autoAcceptSwitch), autoAccept);
    if (m_airdropSwitch) gtk_switch_set_active(GTK_SWITCH(m_airdropSwitch), airdropEnabled);
}

void GtkDesktopApp::appendDiagnosticLog(const std::string& logLine) {
    if (m_logBuffer) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(m_logBuffer, &end);
        std::string line = logLine + "\n";
        gtk_text_buffer_insert(m_logBuffer, &end, line.c_str(), static_cast<int>(line.size()));
        if (m_logTextView) {
            gtk_text_buffer_get_end_iter(m_logBuffer, &end);
            GtkTextMark* mark = gtk_text_buffer_create_mark(m_logBuffer, nullptr, &end, FALSE);
            gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(m_logTextView), mark, 0.0, FALSE, 0.0, 1.0);
            gtk_text_buffer_delete_mark(m_logBuffer, mark);
        }
    }
}

void GtkDesktopApp::showSaveSuccess() {
    showNotification("Preferences saved.");
}

}
