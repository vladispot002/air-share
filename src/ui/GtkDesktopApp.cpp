#include "landrop/ui/GtkDesktopApp.hpp"
#include <iostream>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>

namespace landrop::ui {

GtkDesktopApp::GtkDesktopApp()
    : m_discoveryPresenter(this),
      m_transferPresenter(this),
      m_storagePresenter(this),
      m_settingsPresenter(this) {}

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
    if (!m_usingWebKit) {
        applyAppleTheme();
    }

    m_discoveryPresenter.initialize();
    m_transferPresenter.initialize();
    m_storagePresenter.initialize();
    m_settingsPresenter.initialize();

    g_timeout_add(400, [](gpointer data) -> gboolean {
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
            }
            .apple-accent-btn:hover {
                background-color: #0071E3;
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
            }
            .apple-accent-btn:hover {
                background-color: #0062CC;
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
    showNotification(m_isDarkMode ? "Theme: Dark Mode" : "Theme: Light Mode");
}

void GtkDesktopApp::buildUi() {
    m_mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(m_mainWindow), "LAN-Drop — AirShare");
    gtk_window_set_default_size(GTK_WINDOW(m_mainWindow), 1080, 780);
    gtk_window_set_position(GTK_WINDOW(m_mainWindow), GTK_WIN_POS_CENTER);
    if (!gtk_window_set_icon_from_file(GTK_WINDOW(m_mainWindow), "/home/vladis/lan-drop/resources/lan-drop.svg", nullptr)) {
        gtk_window_set_icon_from_file(GTK_WINDOW(m_mainWindow), "resources/lan-drop.svg", nullptr);
    }

    g_signal_connect(m_mainWindow, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    std::string distHtml = "/home/vladis/lan-drop/Design LAN-Drop macOS UI/dist/index.html";
    if (std::filesystem::exists(distHtml)) {
        void* wkHandle = dlopen("libwebkit2gtk-4.1.so.0", RTLD_NOW);
        if (wkHandle) {
            typedef GtkWidget* (*WkNewFn)();
            typedef void (*WkLoadUriFn)(void*, const char*);
            auto wk_new = reinterpret_cast<WkNewFn>(dlsym(wkHandle, "webkit_web_view_new"));
            auto wk_load = reinterpret_cast<WkLoadUriFn>(dlsym(wkHandle, "webkit_web_view_load_uri"));
            if (wk_new && wk_load) {
                GtkWidget* webView = wk_new();
                gtk_container_add(GTK_CONTAINER(m_mainWindow), webView);
                std::string fileUri = "file://" + distHtml;
                wk_load(webView, fileUri.c_str());
                m_usingWebKit = true;
                return;
            }
        }
    }

    m_headerBar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(m_headerBar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(m_headerBar), "LAN-Drop");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(m_headerBar), "Cross-Platform P2P & Hybrid Cloud Transfer");
    gtk_window_set_titlebar(GTK_WINDOW(m_mainWindow), m_headerBar);

    m_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(m_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(m_stack), 250);

    m_stackSwitcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(m_stackSwitcher), GTK_STACK(m_stack));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(m_headerBar), m_stackSwitcher);

    GtkWidget* themeBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* themeIconLabel = gtk_label_new("Theme");
    m_themeSwitch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(m_themeSwitch), m_isDarkMode);

    g_signal_connect(m_themeSwitch, "notify::active", G_CALLBACK(+[](GObject* obj, GParamSpec*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->toggleTheme(gtk_switch_get_active(GTK_SWITCH(obj)));
    }), this);

    gtk_box_pack_start(GTK_BOX(themeBox), themeIconLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(themeBox), m_themeSwitch, FALSE, FALSE, 0);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(m_headerBar), themeBox);

    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(m_mainWindow), mainBox);

    gtk_stack_add_titled(GTK_STACK(m_stack), createDiscoveryScreen(), "discovery", "Radar / Discover");
    gtk_stack_add_titled(GTK_STACK(m_stack), createTransfersScreen(), "transfers", "Transfers");
    gtk_stack_add_titled(GTK_STACK(m_stack), createStorageScreen(), "storage", "Shared Storage");
    gtk_stack_add_titled(GTK_STACK(m_stack), createSettingsScreen(), "settings", "Settings");

    gtk_box_pack_start(GTK_BOX(mainBox), m_stack, TRUE, TRUE, 0);

    GtkWidget* footerBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(footerBar), 8);
    m_statusLabel = gtk_label_new("Status: Ready. Apple HIG Desktop Application.");
    gtk_box_pack_start(GTK_BOX(footerBar), m_statusLabel, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(mainBox), footerBar, FALSE, FALSE, 0);
}

GtkWidget* GtkDesktopApp::createDiscoveryScreen() {
    GtkWidget* rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 18);

    GtkWidget* topCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkStyleContext* ctx = gtk_widget_get_style_context(topCard);
    gtk_style_context_add_class(ctx, "apple-card");

    m_scanStatusLabel = gtk_label_new("Radar Scanner: Active (Listening for LAN Beacons)");
    GtkWidget* scanBtn = gtk_button_new_with_label("Toggle Scan");
    GtkWidget* refreshBtn = gtk_button_new_with_label("Refresh Devices");
    gtk_style_context_add_class(gtk_widget_get_style_context(refreshBtn), "apple-accent-btn");

    g_signal_connect(scanBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getDiscoveryPresenter().onToggleScanClicked(true);
    }), this);

    g_signal_connect(refreshBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getDiscoveryPresenter().onRefreshDevicesClicked();
    }), this);

    gtk_box_pack_start(GTK_BOX(topCard), m_scanStatusLabel, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), scanBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), refreshBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), topCard, FALSE, FALSE, 0);

    GtkWidget* listCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(listCard), "apple-card");

    GtkWidget* listTitle = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(listTitle), "<b>Discovered Network Nodes (Device List Container):</b>");
    gtk_label_set_xalign(GTK_LABEL(listTitle), 0.0f);
    gtk_box_pack_start(GTK_BOX(listCard), listTitle, FALSE, FALSE, 4);

    m_deviceListBox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(m_deviceListBox), GTK_SELECTION_SINGLE);

    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 240);
    gtk_container_add(GTK_CONTAINER(scrolled), m_deviceListBox);
    gtk_box_pack_start(GTK_BOX(listCard), scrolled, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), listCard, TRUE, TRUE, 0);

    GtkWidget* nfcCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(nfcCard), "apple-card");

    GtkWidget* nfcLabel = gtk_label_new("NFC Quick-Pair:");
    m_nfcEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(m_nfcEntry), "LANDROP_BEACON|MobileDeviceNFC|uuid-nfc-88|8848");

    GtkWidget* nfcPairBtn = gtk_button_new_with_label("Simulate NFC Touch");
    gtk_style_context_add_class(gtk_widget_get_style_context(nfcPairBtn), "apple-accent-btn");

    g_signal_connect(nfcPairBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        const char* text = gtk_entry_get_text(GTK_ENTRY(app->m_nfcEntry));
        app->getDiscoveryPresenter().onNfcPairTriggered(text);
    }), this);

    gtk_box_pack_start(GTK_BOX(nfcCard), nfcLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nfcCard), m_nfcEntry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(nfcCard), nfcPairBtn, FALSE, FALSE, 0);
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

    m_selectedTransferLabel = gtk_label_new("Selection: None");
    gtk_label_set_xalign(GTK_LABEL(m_selectedTransferLabel), 0.0f);
    gtk_box_pack_start(GTK_BOX(card), m_selectedTransferLabel, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(rootBox), card, TRUE, TRUE, 0);

    GtkWidget* actionCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(actionCard), "apple-card");

    GtkWidget* pauseBtn = gtk_button_new_with_label("Pause");
    GtkWidget* resumeBtn = gtk_button_new_with_label("Resume");
    GtkWidget* cancelBtn = gtk_button_new_with_label("Cancel");
    GtkWidget* clearBtn = gtk_button_new_with_label("Clear Completed");

    g_signal_connect(pauseBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().onPauseTransferClicked("task-001");
    }), this);

    g_signal_connect(resumeBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().onResumeTransferClicked("task-001");
    }), this);

    g_signal_connect(cancelBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().onCancelTransferClicked("task-001");
    }), this);

    g_signal_connect(clearBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getTransferPresenter().onClearHistoryClicked();
    }), this);

    gtk_box_pack_start(GTK_BOX(actionCard), pauseBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actionCard), resumeBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actionCard), cancelBtn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(actionCard), clearBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), actionCard, FALSE, FALSE, 0);

    return rootBox;
}

GtkWidget* GtkDesktopApp::createStorageScreen() {
    GtkWidget* rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 18);

    GtkWidget* topCard = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(topCard), "apple-card");

    GtkWidget* pathLabel = gtk_label_new("Shared Folder:");
    m_storagePathEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(m_storagePathEntry), "./shared_drop");

    GtkWidget* rescanBtn = gtk_button_new_with_label("Rescan");
    gtk_style_context_add_class(gtk_widget_get_style_context(rescanBtn), "apple-accent-btn");

    g_signal_connect(rescanBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* app = static_cast<GtkDesktopApp*>(data);
        app->getStoragePresenter().onRescanClicked();
    }), this);

    gtk_box_pack_start(GTK_BOX(topCard), pathLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), m_storagePathEntry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(topCard), rescanBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), topCard, FALSE, FALSE, 0);

    GtkWidget* listCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(listCard), "apple-card");

    m_storageStatsLabel = gtk_label_new("Files: 0 | Total: 0 KB | Security: Open Access");
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

    GtkWidget* passLabel = gtk_label_new("Access PIN / Password:");
    m_passwordEntry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(m_passwordEntry), FALSE);

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

    gtk_box_pack_start(GTK_BOX(passCard), passLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(passCard), m_passwordEntry, TRUE, TRUE, 0);
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

    GtkWidget* logCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(logCard), "apple-card");

    GtkWidget* logTitle = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(logTitle), "<b>System Diagnostics &amp; Activity Log:</b>");
    gtk_label_set_xalign(GTK_LABEL(logTitle), 0.0f);
    gtk_box_pack_start(GTK_BOX(logCard), logTitle, FALSE, FALSE, 2);

    m_logTextView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(m_logTextView), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(m_logTextView), TRUE);
    m_logBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m_logTextView));

    GtkWidget* logScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(logScroll), 160);
    gtk_container_add(GTK_CONTAINER(logScroll), m_logTextView);
    gtk_box_pack_start(GTK_BOX(logCard), logScroll, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(rootBox), logCard, TRUE, TRUE, 0);

    return rootBox;
}

void GtkDesktopApp::updateDeviceList(const std::vector<DeviceViewModel>& devices) {
    if (!m_deviceListBox) return;

    GList* children = gtk_container_get_children(GTK_CONTAINER(m_deviceListBox));
    for (GList* iter = children; iter != nullptr; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    for (const auto& dev : devices) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(row), 8);

        std::string badgeText = "[" + dev.type + "]";
        GtkWidget* badgeLabel = gtk_label_new(badgeText.c_str());

        std::string infoText = dev.name + " (" + dev.endpoint + ") | RSSI: " + std::to_string(dev.signalRssi) + "dBm";
        GtkWidget* infoLabel = gtk_label_new(infoText.c_str());

        GtkWidget* sendBtn = gtk_button_new_with_label("Send File");
        gtk_style_context_add_class(gtk_widget_get_style_context(sendBtn), "apple-accent-btn");

        gtk_box_pack_start(GTK_BOX(row), badgeLabel, FALSE, FALSE, 4);
        gtk_box_pack_start(GTK_BOX(row), infoLabel, TRUE, TRUE, 4);
        gtk_box_pack_end(GTK_BOX(row), sendBtn, FALSE, FALSE, 4);

        gtk_list_box_insert(GTK_LIST_BOX(m_deviceListBox), row, -1);
    }
    gtk_widget_show_all(m_deviceListBox);
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

        std::string sizeStr = std::to_string(f.fileSize) + " B";
        std::string chunksStr = std::to_string(f.totalChunks);
        std::string hashStr = f.fileSha256.substr(0, 16) + "...";

        gtk_list_store_set(m_fileListStore, &iter,
            0, f.fileName.c_str(),
            1, sizeStr.c_str(),
            2, chunksStr.c_str(),
            3, hashStr.c_str(),
            -1
        );
    }
}

void GtkDesktopApp::setStorageStats(uint64_t totalBytes, size_t fileCount) {
    if (m_storageStatsLabel) {
        std::string s = "Files: " + std::to_string(fileCount) + " | Total: " + std::to_string(totalBytes / 1024) + " KB";
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
