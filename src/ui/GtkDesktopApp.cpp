#include "landrop/ui/GtkDesktopApp.hpp"
#include <iostream>
#include <cstring>
#include <filesystem>

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
            .apple-card {
                background-color: #2C2C2E;
                border: 1px solid #3A3A3C;
                border-radius: 10px;
                padding: 12px;
            }
            button {
                background-color: #3A3A3C;
                color: #F5F5F7;
                border: 1px solid #48484A;
                border-radius: 8px;
                padding: 6px 14px;
            }
            button:hover {
                background-color: #48484A;
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
            .traffic-red {
                background-color: #FF5F57;
                border-radius: 50%;
                border: none;
                min-width: 12px;
                min-height: 12px;
                padding: 0;
            }
            .traffic-yellow {
                background-color: #FEBC2E;
                border-radius: 50%;
                border: none;
                min-width: 12px;
                min-height: 12px;
                padding: 0;
            }
            .traffic-green {
                background-color: #28C840;
                border-radius: 50%;
                border: none;
                min-width: 12px;
                min-height: 12px;
                padding: 0;
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
                padding: 6px 10px;
                border-radius: 10px 10px 0 0;
            }
            .mac-terminal-title {
                color: #636366;
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
                padding: 6px;
            }
            treeview {
                background-color: #2C2C2E;
                color: #F5F5F7;
                border: 1px solid #3A3A3C;
                border-radius: 8px;
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
            .apple-card {
                background-color: #FFFFFF;
                border: 1px solid #D2D2D7;
                border-radius: 10px;
                padding: 12px;
            }
            button {
                background-color: #E5E5EA;
                color: #1D1D1F;
                border: 1px solid #D1D1D6;
                border-radius: 8px;
                padding: 6px 14px;
            }
            button:hover {
                background-color: #D1D1D6;
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
            .traffic-red {
                background-color: #FF5F57;
                border-radius: 50%;
                border: none;
                min-width: 12px;
                min-height: 12px;
                padding: 0;
            }
            .traffic-yellow {
                background-color: #FEBC2E;
                border-radius: 50%;
                border: none;
                min-width: 12px;
                min-height: 12px;
                padding: 0;
            }
            .traffic-green {
                background-color: #28C840;
                border-radius: 50%;
                border: none;
                min-width: 12px;
                min-height: 12px;
                padding: 0;
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
                background-color: #0D0D0F;
                border: 1px solid #2A2A2C;
                border-radius: 10px;
            }
            .mac-terminal-header {
                background-color: #151518;
                border-bottom: 1px solid #2A2A2C;
                padding: 6px 10px;
                border-radius: 10px 10px 0 0;
            }
            .mac-terminal-title {
                color: #8E8E93;
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
                background-color: #FFFFFF;
                border-top: 1px solid #D2D2D7;
                padding: 6px;
            }
            entry {
                background-color: #FFFFFF;
                color: #1D1D1F;
                border: 1px solid #D2D2D7;
                border-radius: 6px;
                padding: 6px;
            }
            treeview {
                background-color: #FFFFFF;
                color: #1D1D1F;
                border: 1px solid #D2D2D7;
                border-radius: 8px;
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

GtkWidget* GtkDesktopApp::createTrafficLights() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    const char* classes[] = {"traffic-red", "traffic-yellow", "traffic-green"};
    for (int i = 0; i < 3; ++i) {
        GtkWidget* dot = gtk_button_new();
        gtk_widget_set_size_request(dot, 12, 12);
        gtk_style_context_add_class(gtk_widget_get_style_context(dot), classes[i]);
        gtk_box_pack_start(GTK_BOX(box), dot, FALSE, FALSE, 0);
    }
    return box;
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
    gtk_window_set_position(GTK_WINDOW(m_mainWindow), GTK_WIN_POS_CENTER);
    if (!gtk_window_set_icon_from_file(GTK_WINDOW(m_mainWindow), "/home/vladis/lan-drop/resources/lan-drop.svg", nullptr)) {
        gtk_window_set_icon_from_file(GTK_WINDOW(m_mainWindow), "resources/lan-drop.svg", nullptr);
    }

    g_signal_connect(m_mainWindow, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    m_headerBar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(m_headerBar), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(m_mainWindow), m_headerBar);

    if (!m_isMobileMode) {
        gtk_header_bar_pack_start(GTK_HEADER_BAR(m_headerBar), createTrafficLights());
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
        gtk_box_pack_start(GTK_BOX(mainBox), createMobileBottomBar(), FALSE, FALSE, 0);
    } else {
        GtkWidget* footerBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(footerBar), 6);
        m_statusLabel = gtk_label_new("Status: Ready. Apple HIG Desktop Application.");
        gtk_box_pack_start(GTK_BOX(footerBar), m_statusLabel, FALSE, FALSE, 8);

        GtkWidget* verLabel = gtk_label_new("v2.4.1 | P2P + Cloud Relay");
        gtk_widget_set_opacity(verLabel, 0.6);
        gtk_box_pack_end(GTK_BOX(footerBar), verLabel, FALSE, FALSE, 8);
        gtk_box_pack_start(GTK_BOX(mainBox), footerBar, FALSE, FALSE, 0);
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

    GtkWidget* activeBadge = gtk_label_new("Active");
    gtk_style_context_add_class(gtk_widget_get_style_context(activeBadge), "pill-active");

    m_scanStatusLabel = gtk_label_new("4 devices found");
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

    m_deviceGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(m_deviceGrid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(m_deviceGrid), 12);
    gtk_grid_set_row_homogeneous(GTK_GRID(m_deviceGrid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(m_deviceGrid), TRUE);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroll), m_deviceGrid);
    gtk_box_pack_start(GTK_BOX(rootBox), scroll, TRUE, TRUE, 0);

    GtkWidget* nfcCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(nfcCard), "apple-card");

    GtkWidget* nfcIcon = gtk_label_new("📡");
    gtk_style_context_add_class(gtk_widget_get_style_context(nfcIcon), "device-icon-box");
    gtk_widget_set_size_request(nfcIcon, 36, 36);

    GtkWidget* nfcBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget* nfcTitle = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(nfcTitle), "<b>NFC Quick-Pair</b>");
    gtk_label_set_xalign(GTK_LABEL(nfcTitle), 0.0f);

    m_nfcEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(m_nfcEntry), "Hold device near NFC sensor to pair...");
    gtk_entry_set_text(GTK_ENTRY(m_nfcEntry), "LANDROP:NFC:v1;192.168.1.180:8848;PIN=4242");
    gtk_box_pack_start(GTK_BOX(nfcBox), nfcTitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nfcBox), m_nfcEntry, TRUE, TRUE, 0);

    GtkWidget* nfcTouchBtn = gtk_button_new_with_label("Simulate Touch");
    gtk_style_context_add_class(gtk_widget_get_style_context(nfcTouchBtn), "apple-accent-btn");
    g_signal_connect(nfcTouchBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        const char* val = gtk_entry_get_text(GTK_ENTRY(app->m_nfcEntry));
        app->getDiscoveryPresenter().onNfcPairTriggered(val ? val : "");
    }), this);

    gtk_box_pack_start(GTK_BOX(nfcCard), nfcIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nfcCard), nfcBox, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(nfcCard), nfcTouchBtn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(rootBox), nfcCard, FALSE, FALSE, 0);

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
        }
    }), this);

    m_selectedTransferLabel = gtk_label_new("Selection: T-0042 (project-assets.zip)");
    gtk_label_set_xalign(GTK_LABEL(m_selectedTransferLabel), 0.0f);
    gtk_box_pack_start(GTK_BOX(card), m_selectedTransferLabel, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(rootBox), card, TRUE, TRUE, 0);

    GtkWidget* actionCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(actionCard), "apple-card");

    GtkWidget* pauseBtn = gtk_button_new_with_label("⏸ Pause");
    GtkWidget* resumeBtn = gtk_button_new_with_label("▶ Resume");
    GtkWidget* cancelBtn = gtk_button_new_with_label("✕ Cancel");
    GtkWidget* sendBtn = gtk_button_new_with_label("Send File...");
    gtk_style_context_add_class(gtk_widget_get_style_context(sendBtn), "apple-accent-btn");
    GtkWidget* clearBtn = gtk_button_new_with_label("Clear Completed");

    g_signal_connect(pauseBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().onPauseTransferClicked(app->m_selectedTaskId);
    }), this);

    g_signal_connect(resumeBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().onResumeTransferClicked(app->m_selectedTaskId);
    }), this);

    g_signal_connect(cancelBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().onCancelTransferClicked(app->m_selectedTaskId);
    }), this);

    g_signal_connect(sendBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Select File to Send",
            GTK_WINDOW(app->m_mainWindow),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Send", GTK_RESPONSE_ACCEPT,
            nullptr
        );
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (file) {
                app->getTransferPresenter().onSendFileClicked("peer-thinkpad", file);
                g_free(file);
            }
        }
        gtk_widget_destroy(dialog);
    }), this);

    g_signal_connect(clearBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().onClearHistoryClicked();
    }), this);

    gtk_box_pack_start(GTK_BOX(actionCard), pauseBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actionCard), resumeBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actionCard), cancelBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actionCard), sendBtn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(actionCard), clearBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), actionCard, FALSE, FALSE, 0);

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

    gtk_box_pack_start(GTK_BOX(topCard), pathLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), m_storagePathEntry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), browseBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), rescanBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), addFileBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), topCard, FALSE, FALSE, 0);

    GtkWidget* listCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(listCard), "apple-card");

    m_storageStatsLabel = gtk_label_new("● 5 Files  |  3.16 GB Total  |  Chunked Transfer");
    gtk_label_set_xalign(GTK_LABEL(m_storageStatsLabel), 0.0f);
    gtk_box_pack_start(GTK_BOX(listCard), m_storageStatsLabel, FALSE, FALSE, 4);

    m_fileListStore = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    m_fileTreeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(m_fileListStore));

    GtkCellRenderer* r = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_fileTreeView), -1, "File Name", r, "text", 0, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_fileTreeView), -1, "Size", r, "text", 1, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_fileTreeView), -1, "Chunks", r, "text", 2, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m_fileTreeView), -1, "SHA-256", r, "text", 3, nullptr);

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
    GtkWidget* speedLabel = gtk_label_new("Speed Limit (KB/s, 0 = Unlimited):");
    m_speedScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 50000, 1000);
    gtk_scale_set_draw_value(GTK_SCALE(m_speedScale), TRUE);
    gtk_box_pack_start(GTK_BOX(speedBox), speedLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(speedBox), m_speedScale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(confCard), speedBox, FALSE, FALSE, 0);

    GtkWidget* autoBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* autoLabel = gtk_label_new("Auto-Accept Incoming Transfers:");
    m_autoAcceptSwitch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(m_autoAcceptSwitch), TRUE);
    gtk_box_pack_start(GTK_BOX(autoBox), autoLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(autoBox), m_autoAcceptSwitch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(confCard), autoBox, FALSE, FALSE, 0);

    GtkWidget* airdropBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* airdropLabel = gtk_label_new("AirDrop Apple Compatibility Protocol:");
    m_airdropSwitch = gtk_switch_new();
    gtk_box_pack_start(GTK_BOX(airdropBox), airdropLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(airdropBox), m_airdropSwitch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(confCard), airdropBox, FALSE, FALSE, 0);

    GtkWidget* saveBtn = gtk_button_new_with_label("Save Preferences");
    gtk_style_context_add_class(gtk_widget_get_style_context(saveBtn), "apple-accent-btn");
    g_signal_connect(saveBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getSettingsPresenter().onPortChanged(static_cast<uint16_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->m_portSpin))));
        app->getSettingsPresenter().onSpeedLimitChanged(static_cast<uint32_t>(gtk_range_get_value(GTK_RANGE(app->m_speedScale))));
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

    const char* colors[] = {"traffic-red", "traffic-yellow", "traffic-green"};
    for (int i = 0; i < 3; ++i) {
        GtkWidget* dot = gtk_button_new();
        gtk_widget_set_size_request(dot, 10, 10);
        gtk_style_context_add_class(gtk_widget_get_style_context(dot), colors[i]);
        gtk_box_pack_start(GTK_BOX(termTopBar), dot, FALSE, FALSE, 2);
    }

    GtkWidget* termTitle = gtk_label_new("landrop — diagnostics — 96×28");
    gtk_style_context_add_class(gtk_widget_get_style_context(termTitle), "mac-terminal-title");
    gtk_box_pack_start(GTK_BOX(termTopBar), termTitle, FALSE, FALSE, 8);

    GtkWidget* termPulse = gtk_label_new("●");
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
    if (!m_deviceGrid) return;

    GList* children = gtk_container_get_children(GTK_CONTAINER(m_deviceGrid));
    for (GList* iter = children; iter != nullptr; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& dev = devices[i];

        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_style_context_add_class(gtk_widget_get_style_context(card), "apple-card");

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
        gtk_label_set_markup(GTK_LABEL(nameLabel), ("<b>" + dev.name + "</b>").c_str());
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

        GtkWidget* sendBtn = gtk_button_new_with_label("Send File");
        gtk_style_context_add_class(gtk_widget_get_style_context(sendBtn), "apple-accent-btn");
        g_object_set_data_full(G_OBJECT(sendBtn), "device_id", g_strdup(dev.id.c_str()), g_free);
        g_signal_connect(sendBtn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer appPtr) {
            auto* app = static_cast<GtkDesktopApp*>(appPtr);
            const char* devId = static_cast<const char*>(g_object_get_data(G_OBJECT(b), "device_id"));
            if (!devId) return;

            GtkWidget* dialog = gtk_file_chooser_dialog_new(
                "Select File to Send via LAN-Drop",
                GTK_WINDOW(app->m_mainWindow),
                GTK_FILE_CHOOSER_ACTION_OPEN,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Send", GTK_RESPONSE_ACCEPT,
                nullptr
            );

            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
                char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
                if (filename) {
                    app->getTransferPresenter().onSendFileClicked(devId, filename);
                    app->switchToTab("transfers");
                    g_free(filename);
                }
            }
            gtk_widget_destroy(dialog);
        }), this);

        gtk_box_pack_end(GTK_BOX(botRow), sendBtn, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(card), botRow, FALSE, FALSE, 0);

        int col = m_isMobileMode ? 0 : static_cast<int>(i % 2);
        int row = m_isMobileMode ? static_cast<int>(i) : static_cast<int>(i / 2);
        gtk_grid_attach(GTK_GRID(m_deviceGrid), card, col, row, 1, 1);
    }

    gtk_widget_show_all(m_deviceGrid);

    if (m_scanStatusLabel) {
        std::string s = std::to_string(devices.size()) + " devices found";
        gtk_label_set_text(GTK_LABEL(m_scanStatusLabel), s.c_str());
    }
}

void GtkDesktopApp::setScanningStatus(bool isScanning) {
    if (m_scanStatusLabel) {
        std::string s = "Radar Scanner: " + std::string(isScanning ? "Active (Listening for LAN Beacons)" : "Idle");
        gtk_label_set_text(GTK_LABEL(m_scanStatusLabel), s.c_str());
    }
}

void GtkDesktopApp::showNfcPairResult(bool success, const std::string& message) {
    showNotification((success ? "NFC Pair OK: " : "NFC Pair Failed: ") + message);
}

void GtkDesktopApp::showNotification(const std::string& text) {
    if (m_statusLabel) {
        std::string s = "Status: " + text;
        gtk_label_set_text(GTK_LABEL(m_statusLabel), s.c_str());
    }
}

void GtkDesktopApp::updateTransferTable(const std::vector<TransferViewModel>& transfers) {
    if (!m_transferListStore) return;
    gtk_list_store_clear(m_transferListStore);

    for (const auto& t : transfers) {
        GtkTreeIter iter;
        gtk_list_store_append(m_transferListStore, &iter);

        std::string sizeStr = std::to_string(t.fileSize / 1024) + " KB";
        std::string speedStr = std::to_string(static_cast<int>(t.speedBytesPerSec / 1024.0)) + " KB/s";

        gtk_list_store_set(m_transferListStore, &iter,
            0, t.taskId.c_str(),
            1, t.fileName.c_str(),
            2, sizeStr.c_str(),
            3, static_cast<int>(t.progress),
            4, speedStr.c_str(),
            5, t.stateStr.c_str(),
            6, t.direction.c_str(),
            -1
        );
    }
}

void GtkDesktopApp::updateSelectedTransferDetails(const TransferViewModel& item) {
    if (m_selectedTransferLabel) {
        std::string s = "Selected: " + item.fileName + " (" + item.taskId + ") | State: " + item.stateStr;
        gtk_label_set_text(GTK_LABEL(m_selectedTransferLabel), s.c_str());
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
            -1
        );
    }
}

void GtkDesktopApp::setStorageStats(uint64_t totalBytes, size_t fileCount) {
    if (m_storageStatsLabel) {
        std::string totalStr;
        if (totalBytes >= 1024ULL * 1024 * 1024) {
            totalStr = std::to_string(totalBytes / (1024ULL * 1024 * 1024)) + "." + std::to_string((totalBytes % (1024ULL * 1024 * 1024)) / (100ULL * 1024 * 1024)) + " GB";
        } else if (totalBytes >= 1024 * 1024) {
            totalStr = std::to_string(totalBytes / (1024 * 1024)) + "." + std::to_string((totalBytes % (1024 * 1024)) / (100 * 1024)) + " MB";
        } else {
            totalStr = std::to_string(totalBytes / 1024) + " KB";
        }
        std::string s = "● " + std::to_string(fileCount) + " Files  |  " + totalStr + " Total  |  Chunked Transfer";
        gtk_label_set_text(GTK_LABEL(m_storageStatsLabel), s.c_str());
    }
}

void GtkDesktopApp::setPasswordProtectionStatus(bool isProtected) {
    if (m_storageStatsLabel) {
        std::string current = gtk_label_get_text(GTK_LABEL(m_storageStatsLabel));
        current += (isProtected ? " | Security: PIN Protected" : " | Security: Open Access");
        gtk_label_set_text(GTK_LABEL(m_storageStatsLabel), current.c_str());
    }
}

void GtkDesktopApp::loadSettings(uint16_t port, uint32_t speedLimitKbps, bool autoAccept, bool airdropEnabled) {
    if (m_portSpin) gtk_spin_button_set_value(GTK_SPIN_BUTTON(m_portSpin), port);
    if (m_speedScale) gtk_range_set_value(GTK_RANGE(m_speedScale), speedLimitKbps);
    if (m_autoAcceptSwitch) gtk_switch_set_active(GTK_SWITCH(m_autoAcceptSwitch), autoAccept);
    if (m_airdropSwitch) gtk_switch_set_active(GTK_SWITCH(m_airdropSwitch), airdropEnabled);
}

void GtkDesktopApp::appendDiagnosticLog(const std::string& logLine) {
    if (m_logBuffer) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(m_logBuffer, &end);
        std::string line = logLine + "\n";
        gtk_text_buffer_insert(m_logBuffer, &end, line.c_str(), static_cast<int>(line.size()));
    }
}

void GtkDesktopApp::showSaveSuccess() {
    showNotification("Preferences saved.");
}

}
