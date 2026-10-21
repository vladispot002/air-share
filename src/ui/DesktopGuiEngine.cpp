#include "landrop/ui/DesktopGuiEngine.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdio>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

namespace landrop::ui {

struct ApplePalette {
    unsigned long bg;
    unsigned long headerBg;
    unsigned long cardBg;
    unsigned long cardBorder;
    unsigned long textPrimary;
    unsigned long textSecondary;
    unsigned long accent;
    unsigned long accentText;
    unsigned long trafficRed;
    unsigned long trafficYellow;
    unsigned long trafficGreen;
    unsigned long progressTrack;
    unsigned long progressFill;
    unsigned long buttonBg;
    unsigned long buttonBorder;
    unsigned long buttonText;
    unsigned long successGreen;
};

static ApplePalette getPalette(ThemeMode mode) {
    ApplePalette p;
    if (mode == ThemeMode::Dark) {
        p.bg = 0x1E1E1E;
        p.headerBg = 0x252528;
        p.cardBg = 0x2C2C2E;
        p.cardBorder = 0x3A3A3C;
        p.textPrimary = 0xF5F5F7;
        p.textSecondary = 0x98989D;
        p.accent = 0x0A84FF;
        p.accentText = 0xFFFFFF;
        p.trafficRed = 0xFF5F56;
        p.trafficYellow = 0xFFBD2E;
        p.trafficGreen = 0x27C93F;
        p.progressTrack = 0x3A3A3C;
        p.progressFill = 0x0A84FF;
        p.buttonBg = 0x3A3A3C;
        p.buttonBorder = 0x48484A;
        p.buttonText = 0xF5F5F7;
        p.successGreen = 0x30D158;
    } else {
        p.bg = 0xF5F5F7;
        p.headerBg = 0xECECEE;
        p.cardBg = 0xFFFFFF;
        p.cardBorder = 0xD2D2D7;
        p.textPrimary = 0x1D1D1F;
        p.textSecondary = 0x86868B;
        p.accent = 0x007AFF;
        p.accentText = 0xFFFFFF;
        p.trafficRed = 0xFF5F56;
        p.trafficYellow = 0xFFBD2E;
        p.trafficGreen = 0x27C93F;
        p.progressTrack = 0xE5E5EA;
        p.progressFill = 0x007AFF;
        p.buttonBg = 0xE5E5EA;
        p.buttonBorder = 0xD1D1D6;
        p.buttonText = 0x1D1D1F;
        p.successGreen = 0x34C759;
    }
    return p;
}

DesktopGuiEngine::DesktopGuiEngine()
    : m_discoveryPresenter(this),
      m_transferPresenter(this),
      m_storagePresenter(this),
      m_settingsPresenter(this) {}

DesktopGuiEngine::~DesktopGuiEngine() {
    closeX11();
}

bool DesktopGuiEngine::initialize(int width, int height, const std::string& title) {
    m_width = width;
    m_height = height;
    m_title = title;

    m_discoveryPresenter.initialize();
    m_transferPresenter.initialize();
    m_storagePresenter.initialize();
    m_settingsPresenter.initialize();

    m_lastNotification = "LAN-Drop Desktop Ready (macOS HIG Theme)";

    return initX11();
}

bool DesktopGuiEngine::initX11() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return false;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    ApplePalette pal = getPalette(m_theme);

    Window win = XCreateSimpleWindow(
        display, root, 50, 50,
        static_cast<unsigned int>(m_width),
        static_cast<unsigned int>(m_height),
        1, pal.cardBorder, pal.bg
    );

    XStoreName(display, win, m_title.c_str());
    XSelectInput(display, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(display, win);

    GC gc = XCreateGC(display, win, 0, nullptr);
    XSetForeground(display, gc, pal.textPrimary);

    m_displayPtr = display;
    m_windowId = win;
    m_gcPtr = gc;

    return true;
}

void DesktopGuiEngine::closeX11() {
    if (m_displayPtr) {
        Display* display = static_cast<Display*>(m_displayPtr);
        if (m_gcPtr) {
            XFreeGC(display, static_cast<GC>(m_gcPtr));
            m_gcPtr = nullptr;
        }
        if (m_windowId) {
            XDestroyWindow(display, m_windowId);
            m_windowId = 0;
        }
        XCloseDisplay(display);
        m_displayPtr = nullptr;
    }
}

void DesktopGuiEngine::toggleTheme() {
    m_theme = (m_theme == ThemeMode::Dark) ? ThemeMode::Light : ThemeMode::Dark;
    if (m_displayPtr) {
        ApplePalette pal = getPalette(m_theme);
        Display* display = static_cast<Display*>(m_displayPtr);
        XSetWindowBackground(display, m_windowId, pal.bg);
        XClearWindow(display, m_windowId);
        renderScreen();
    }
}

void DesktopGuiEngine::run() {
    m_isRunning = true;
    if (m_displayPtr) {
        while (m_isRunning) {
            handleX11Events();
            tick();
            renderScreen();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    } else {
        std::cout << "[GUI Headless Engine] Running in non-display fallback mode.\n";
        renderScreen();
    }
}

void DesktopGuiEngine::stop() {
    m_isRunning = false;
}

void DesktopGuiEngine::handleX11Events() {
    if (!m_displayPtr) {
        return;
    }

    Display* display = static_cast<Display*>(m_displayPtr);
    while (XPending(display) > 0) {
        XEvent ev;
        XNextEvent(display, &ev);

        if (ev.type == Expose) {
            renderScreen();
        } else if (ev.type == KeyPress) {
            KeySym key = XLookupKeysym(&ev.xkey, 0);
            if (key == XK_1) {
                switchScreen(GuiScreen::Discovery);
            } else if (key == XK_2) {
                switchScreen(GuiScreen::Transfers);
            } else if (key == XK_3) {
                switchScreen(GuiScreen::Storage);
            } else if (key == XK_4) {
                switchScreen(GuiScreen::Settings);
            } else if (key == XK_t || key == XK_T) {
                toggleTheme();
            } else if (key == XK_r) {
                triggerAction(1);
            } else if (key == XK_n) {
                triggerAction(2);
            } else if (key == XK_p) {
                triggerAction(3);
            } else if (key == XK_c) {
                triggerAction(4);
            } else if (key == XK_x) {
                triggerAction(5);
            } else if (key == XK_q || key == XK_Escape) {
                m_isRunning = false;
            }
        } else if (ev.type == ButtonPress) {
            int x = ev.xbutton.x;
            int y = ev.xbutton.y;

            if (y >= 14 && y <= 32 && x >= 18 && x <= 36) {
                m_isRunning = false;
                return;
            }

            if (x >= 810 && x <= 940 && y >= 12 && y <= 38) {
                toggleTheme();
                return;
            }

            if (y >= 54 && y <= 84) {
                if (x >= 150 && x <= 290) {
                    switchScreen(GuiScreen::Discovery);
                } else if (x >= 300 && x <= 440) {
                    switchScreen(GuiScreen::Transfers);
                } else if (x >= 450 && x <= 590) {
                    switchScreen(GuiScreen::Storage);
                } else if (x >= 600 && x <= 740) {
                    switchScreen(GuiScreen::Settings);
                }
            } else if (y >= 545 && y <= 585) {
                if (x >= 30 && x <= 150) {
                    triggerAction(1);
                } else if (x >= 160 && x <= 300) {
                    triggerAction(2);
                } else if (x >= 310 && x <= 430) {
                    triggerAction(3);
                } else if (x >= 440 && x <= 560) {
                    triggerAction(4);
                } else if (x >= 570 && x <= 690) {
                    triggerAction(5);
                }
            }
        }
    }
}

void DesktopGuiEngine::renderScreen() {
    if (!m_displayPtr) {
        return;
    }

    Display* display = static_cast<Display*>(m_displayPtr);
    Window win = m_windowId;
    GC gc = static_cast<GC>(m_gcPtr);

    ApplePalette pal = getPalette(m_theme);

    XSetForeground(display, gc, pal.bg);
    XFillRectangle(display, win, gc, 0, 0, static_cast<unsigned int>(m_width), static_cast<unsigned int>(m_height));

    XSetForeground(display, gc, pal.headerBg);
    XFillRectangle(display, win, gc, 0, 0, static_cast<unsigned int>(m_width), 46);

    XSetForeground(display, gc, pal.trafficRed);
    XFillArc(display, win, gc, 20, 16, 12, 12, 0, 360 * 64);

    XSetForeground(display, gc, pal.trafficYellow);
    XFillArc(display, win, gc, 38, 16, 12, 12, 0, 360 * 64);

    XSetForeground(display, gc, pal.trafficGreen);
    XFillArc(display, win, gc, 56, 16, 12, 12, 0, 360 * 64);

    XSetForeground(display, gc, pal.textPrimary);
    const char* title = "LAN-Drop — AirShare";
    XDrawString(display, win, gc, m_width / 2 - 60, 26, title, static_cast<int>(strlen(title)));

    XSetForeground(display, gc, pal.buttonBg);
    XFillRectangle(display, win, gc, 810, 12, 130, 24);
    XSetForeground(display, gc, pal.buttonBorder);
    XDrawRectangle(display, win, gc, 810, 12, 130, 24);

    XSetForeground(display, gc, pal.buttonText);
    const char* themeLabel = (m_theme == ThemeMode::Dark) ? "[T] Theme: Dark" : "[T] Theme: Light";
    XDrawString(display, win, gc, 825, 28, themeLabel, static_cast<int>(strlen(themeLabel)));

    XSetForeground(display, gc, pal.cardBorder);
    XDrawLine(display, win, gc, 0, 46, m_width, 46);

    const char* tabNames[4] = {"Radar / Discover", "Transfers", "Shared Storage", "Settings"};
    GuiScreen tabScreens[4] = {GuiScreen::Discovery, GuiScreen::Transfers, GuiScreen::Storage, GuiScreen::Settings};
    int startX = 170;
    int tabW = 150;

    for (int i = 0; i < 4; ++i) {
        int tx = startX + i * (tabW + 10);
        bool isActive = (m_currentScreen == tabScreens[i]);

        XSetForeground(display, gc, isActive ? pal.accent : pal.cardBg);
        XFillRectangle(display, win, gc, tx, 54, static_cast<unsigned int>(tabW), 28);

        XSetForeground(display, gc, pal.cardBorder);
        XDrawRectangle(display, win, gc, tx, 54, static_cast<unsigned int>(tabW), 28);

        XSetForeground(display, gc, isActive ? pal.accentText : pal.textPrimary);
        XDrawString(display, win, gc, tx + 16, 72, tabNames[i], static_cast<int>(strlen(tabNames[i])));
    }

    if (m_currentScreen == GuiScreen::Discovery) {
        XSetForeground(display, gc, pal.cardBg);
        XFillRectangle(display, win, gc, 30, 95, static_cast<unsigned int>(m_width - 60), 430);
        XSetForeground(display, gc, pal.cardBorder);
        XDrawRectangle(display, win, gc, 30, 95, static_cast<unsigned int>(m_width - 60), 430);

        XSetForeground(display, gc, pal.successGreen);
        XFillArc(display, win, gc, 50, 115, 10, 10, 0, 360 * 64);

        XSetForeground(display, gc, pal.textPrimary);
        const char* radarHead = "Nearby Devices (LAN Radar & AirDrop Discovery)";
        XDrawString(display, win, gc, 70, 124, radarHead, static_cast<int>(strlen(radarHead)));

        XSetForeground(display, gc, pal.textSecondary);
        std::string statusSub = "Scanner Status: " + std::string(m_isScanning ? "Active (Broadcasting UDP Beacons)" : "Idle");
        XDrawString(display, win, gc, 50, 150, statusSub.c_str(), static_cast<int>(statusSub.size()));

        int cardY = 175;
        for (size_t i = 0; i < m_deviceList.size() && i < 4; ++i) {
            const auto& dev = m_deviceList[i];

            XSetForeground(display, gc, pal.headerBg);
            XFillRectangle(display, win, gc, 50, cardY, static_cast<unsigned int>(m_width - 100), 52);
            XSetForeground(display, gc, pal.cardBorder);
            XDrawRectangle(display, win, gc, 50, cardY, static_cast<unsigned int>(m_width - 100), 52);

            XSetForeground(display, gc, pal.accent);
            std::string badge = "[" + dev.type + "]";
            XDrawString(display, win, gc, 65, cardY + 22, badge.c_str(), static_cast<int>(badge.size()));

            XSetForeground(display, gc, pal.textPrimary);
            XDrawString(display, win, gc, 160, cardY + 22, dev.name.c_str(), static_cast<int>(dev.name.size()));

            XSetForeground(display, gc, pal.textSecondary);
            std::string meta = dev.endpoint + " | RSSI: " + std::to_string(dev.signalRssi) + "dBm | " + (dev.isOnline ? "Online" : "Offline");
            XDrawString(display, win, gc, 160, cardY + 40, meta.c_str(), static_cast<int>(meta.size()));

            XSetForeground(display, gc, pal.accent);
            XFillRectangle(display, win, gc, m_width - 170, cardY + 14, 90, 24);
            XSetForeground(display, gc, pal.accentText);
            const char* sendBtn = "Send File";
            XDrawString(display, win, gc, m_width - 155, cardY + 30, sendBtn, static_cast<int>(strlen(sendBtn)));

            cardY += 62;
        }

        XSetForeground(display, gc, pal.headerBg);
        XFillRectangle(display, win, gc, 50, 440, static_cast<unsigned int>(m_width - 100), 65);
        XSetForeground(display, gc, pal.cardBorder);
        XDrawRectangle(display, win, gc, 50, 440, static_cast<unsigned int>(m_width - 100), 65);

        XSetForeground(display, gc, pal.textPrimary);
        const char* nfcTitle = "NFC Quick-Pair Card";
        XDrawString(display, win, gc, 65, 465, nfcTitle, static_cast<int>(strlen(nfcTitle)));

        XSetForeground(display, gc, pal.textSecondary);
        const char* nfcDesc = "Tap an Android or iOS device against the NFC reader for instant 1-touch handshake.";
        XDrawString(display, win, gc, 65, 488, nfcDesc, static_cast<int>(strlen(nfcDesc)));

    } else if (m_currentScreen == GuiScreen::Transfers) {
        XSetForeground(display, gc, pal.cardBg);
        XFillRectangle(display, win, gc, 30, 95, static_cast<unsigned int>(m_width - 60), 430);
        XSetForeground(display, gc, pal.cardBorder);
        XDrawRectangle(display, win, gc, 30, 95, static_cast<unsigned int>(m_width - 60), 430);

        XSetForeground(display, gc, pal.textPrimary);
        const char* th1 = "Transfer Queue (P2P Chunked Engine)";
        XDrawString(display, win, gc, 50, 125, th1, static_cast<int>(strlen(th1)));

        XSetForeground(display, gc, pal.textSecondary);
        const char* tableHeader = "TASK ID     FILE NAME                    SIZE        PROGRESS          SPEED       STATUS";
        XDrawString(display, win, gc, 50, 155, tableHeader, static_cast<int>(strlen(tableHeader)));
        XSetForeground(display, gc, pal.cardBorder);
        XDrawLine(display, win, gc, 50, 162, m_width - 50, 162);

        int rowY = 185;
        for (size_t i = 0; i < m_transferList.size() && i < 5; ++i) {
            const auto& t = m_transferList[i];

            XSetForeground(display, gc, pal.textPrimary);
            char col1[128];
            snprintf(col1, sizeof(col1), "%-11s %-28s %-11llu", t.taskId.c_str(), t.fileName.c_str(), static_cast<unsigned long long>(t.fileSize));
            XDrawString(display, win, gc, 50, rowY, col1, static_cast<int>(strlen(col1)));

            int barX = 420;
            int barW = 100;
            int fillW = static_cast<int>((t.progress / 100.0) * barW);

            XSetForeground(display, gc, pal.progressTrack);
            XFillRectangle(display, win, gc, barX, rowY - 9, static_cast<unsigned int>(barW), 10);

            XSetForeground(display, gc, pal.progressFill);
            if (fillW > 0) {
                XFillRectangle(display, win, gc, barX, rowY - 9, static_cast<unsigned int>(fillW), 10);
            }

            char col2[128];
            snprintf(col2, sizeof(col2), "%3d%%   %-11.1f %-10s",
                     static_cast<int>(t.progress),
                     t.speedBytesPerSec / 1024.0,
                     t.stateStr.c_str());
            XSetForeground(display, gc, pal.textPrimary);
            XDrawString(display, win, gc, 535, rowY, col2, static_cast<int>(strlen(col2)));

            rowY += 34;
        }

        XSetForeground(display, gc, pal.headerBg);
        XFillRectangle(display, win, gc, 50, 440, static_cast<unsigned int>(m_width - 100), 65);
        XSetForeground(display, gc, pal.cardBorder);
        XDrawRectangle(display, win, gc, 50, 440, static_cast<unsigned int>(m_width - 100), 65);

        XSetForeground(display, gc, pal.textPrimary);
        std::string selText = "Active Selection: " + (m_selectedTransfer.fileName.empty() ? "None" : m_selectedTransfer.fileName + " | State: " + m_selectedTransfer.stateStr);
        XDrawString(display, win, gc, 65, 475, selText.c_str(), static_cast<int>(selText.size()));

    } else if (m_currentScreen == GuiScreen::Storage) {
        XSetForeground(display, gc, pal.cardBg);
        XFillRectangle(display, win, gc, 30, 95, static_cast<unsigned int>(m_width - 60), 430);
        XSetForeground(display, gc, pal.cardBorder);
        XDrawRectangle(display, win, gc, 30, 95, static_cast<unsigned int>(m_width - 60), 430);

        XSetForeground(display, gc, pal.textPrimary);
        std::string sPath = "Shared Public Directory: " + m_sharedPath;
        XDrawString(display, win, gc, 50, 125, sPath.c_str(), static_cast<int>(sPath.size()));

        XSetForeground(display, gc, pal.textSecondary);
        std::string sStats = "Capacity: " + std::to_string(m_sharedFileCount) + " items | Total: " +
                             std::to_string(m_sharedTotalBytes / 1024) + " KB | Security: " +
                             (m_isPasswordProtected ? "PIN Protected" : "Open Access");
        XDrawString(display, win, gc, 50, 150, sStats.c_str(), static_cast<int>(sStats.size()));

        XSetForeground(display, gc, pal.cardBorder);
        XDrawLine(display, win, gc, 50, 162, m_width - 50, 162);

        int rowY = 190;
        for (size_t i = 0; i < m_fileList.size() && i < 6; ++i) {
            const auto& f = m_fileList[i];
            XSetForeground(display, gc, pal.textPrimary);
            XDrawString(display, win, gc, 50, rowY, f.fileName.c_str(), static_cast<int>(f.fileName.size()));

            XSetForeground(display, gc, pal.textSecondary);
            std::string fDetails = std::to_string(f.fileSize) + " bytes | " + std::to_string(f.totalChunks) + " chunks | SHA-256: " + f.fileSha256.substr(0, 20) + "...";
            XDrawString(display, win, gc, 300, rowY, fDetails.c_str(), static_cast<int>(fDetails.size()));
            rowY += 30;
        }

    } else if (m_currentScreen == GuiScreen::Settings) {
        XSetForeground(display, gc, pal.cardBg);
        XFillRectangle(display, win, gc, 30, 95, static_cast<unsigned int>(m_width - 60), 430);
        XSetForeground(display, gc, pal.cardBorder);
        XDrawRectangle(display, win, gc, 30, 95, static_cast<unsigned int>(m_width - 60), 430);

        XSetForeground(display, gc, pal.textPrimary);
        const char* sTitle = "Preferences & Protocols";
        XDrawString(display, win, gc, 50, 125, sTitle, static_cast<int>(strlen(sTitle)));

        std::string set1 = "Listen Port: " + std::to_string(m_settingsPort);
        std::string set2 = "Transfer Speed Limit: " + (m_settingsSpeedLimit == 0 ? "Unlimited" : std::to_string(m_settingsSpeedLimit) + " KB/s");
        std::string set3 = "Auto-Accept Transfers: " + std::string(m_settingsAutoAccept ? "[✓] Enabled" : "[ ] Disabled");
        std::string set4 = "AirDrop Apple Protocol Bridge: " + std::string(m_settingsAirDropEnabled ? "[✓] Active" : "[ ] Inactive");

        XSetForeground(display, gc, pal.textSecondary);
        XDrawString(display, win, gc, 50, 155, set1.c_str(), static_cast<int>(set1.size()));
        XDrawString(display, win, gc, 50, 180, set2.c_str(), static_cast<int>(set2.size()));
        XDrawString(display, win, gc, 50, 205, set3.c_str(), static_cast<int>(set3.size()));
        XDrawString(display, win, gc, 50, 230, set4.c_str(), static_cast<int>(set4.size()));

        XSetForeground(display, gc, pal.cardBorder);
        XDrawLine(display, win, gc, 50, 245, m_width - 50, 245);

        XSetForeground(display, gc, pal.textPrimary);
        const char* logTitle = "System Diagnostics Console:";
        XDrawString(display, win, gc, 50, 268, logTitle, static_cast<int>(strlen(logTitle)));

        int logY = 295;
        for (int i = static_cast<int>(m_diagnosticLogs.size()) - 1; i >= 0 && logY < 500; --i) {
            XSetForeground(display, gc, pal.textSecondary);
            XDrawString(display, win, gc, 50, logY, m_diagnosticLogs[static_cast<size_t>(i)].c_str(),
                        static_cast<int>(m_diagnosticLogs[static_cast<size_t>(i)].size()));
            logY += 24;
        }
    }

    const char* actions[5] = {"[R] Refresh", "[N] NFC Pair", "[P] Pause", "[C] Resume", "[X] Cancel"};
    int btnX = 30;
    int btnW = 120;
    for (int i = 0; i < 5; ++i) {
        int bx = btnX + i * (btnW + 15);
        XSetForeground(display, gc, pal.buttonBg);
        XFillRectangle(display, win, gc, bx, 545, static_cast<unsigned int>(btnW), 32);

        XSetForeground(display, gc, pal.buttonBorder);
        XDrawRectangle(display, win, gc, bx, 545, static_cast<unsigned int>(btnW), 32);

        XSetForeground(display, gc, pal.buttonText);
        XDrawString(display, win, gc, bx + 16, 565, actions[i], static_cast<int>(strlen(actions[i])));
    }

    XSetForeground(display, gc, pal.cardBg);
    XFillRectangle(display, win, gc, 30, 592, static_cast<unsigned int>(m_width - 60), 30);
    XSetForeground(display, gc, pal.cardBorder);
    XDrawRectangle(display, win, gc, 30, 592, static_cast<unsigned int>(m_width - 60), 30);

    XSetForeground(display, gc, pal.textSecondary);
    std::string notif = "STATUS: " + m_lastNotification + "  |  [T] Toggle Theme  |  [1-4] Tabs  |  [Q] Quit";
    XDrawString(display, win, gc, 45, 612, notif.c_str(), static_cast<int>(notif.size()));

    XFlush(display);
}

void DesktopGuiEngine::switchScreen(GuiScreen screen) {
    m_currentScreen = screen;
    if (m_currentScreen == GuiScreen::Discovery) {
        m_discoveryPresenter.onRefreshDevicesClicked();
    } else if (m_currentScreen == GuiScreen::Transfers) {
        m_transferPresenter.tick();
    } else if (m_currentScreen == GuiScreen::Storage) {
        m_storagePresenter.onRescanClicked();
    }
}

void DesktopGuiEngine::triggerAction(int actionCode) {
    if (actionCode == 1) {
        m_discoveryPresenter.onRefreshDevicesClicked();
        m_storagePresenter.onRescanClicked();
    } else if (actionCode == 2) {
        m_discoveryPresenter.onNfcPairTriggered("LANDROP_BEACON|MobileDeviceNFC|uuid-nfc-999|8848");
    } else if (actionCode == 3) {
        if (!m_transferList.empty()) {
            m_transferPresenter.onPauseTransferClicked(m_transferList[0].taskId);
        }
    } else if (actionCode == 4) {
        if (!m_transferList.empty()) {
            m_transferPresenter.onResumeTransferClicked(m_transferList[0].taskId);
        }
    } else if (actionCode == 5) {
        if (!m_transferList.empty()) {
            m_transferPresenter.onCancelTransferClicked(m_transferList[0].taskId);
        }
    }
}

void DesktopGuiEngine::tick() {
    m_transferPresenter.tick();
}

void DesktopGuiEngine::updateDeviceList(const std::vector<DeviceViewModel>& devices) {
    m_deviceList = devices;
}

void DesktopGuiEngine::setScanningStatus(bool isScanning) {
    m_isScanning = isScanning;
}

void DesktopGuiEngine::showNfcPairResult(bool success, const std::string& message) {
    m_lastNotification = (success ? "NFC OK: " : "NFC FAIL: ") + message;
}

void DesktopGuiEngine::showNotification(const std::string& text) {
    m_lastNotification = text;
}

void DesktopGuiEngine::updateTransferTable(const std::vector<TransferViewModel>& transfers) {
    m_transferList = transfers;
}

void DesktopGuiEngine::updateSelectedTransferDetails(const TransferViewModel& item) {
    m_selectedTransfer = item;
}

void DesktopGuiEngine::showTransferError(const std::string& taskId, const std::string& error) {
    m_lastNotification = "Error in " + taskId + ": " + error;
}

void DesktopGuiEngine::setSharedPath(const std::string& path) {
    m_sharedPath = path;
}

void DesktopGuiEngine::updateFileList(const std::vector<FileMetadata>& files) {
    m_fileList = files;
}

void DesktopGuiEngine::setStorageStats(uint64_t totalBytes, size_t fileCount) {
    m_sharedTotalBytes = totalBytes;
    m_sharedFileCount = fileCount;
}

void DesktopGuiEngine::setPasswordProtectionStatus(bool isProtected) {
    m_isPasswordProtected = isProtected;
}

void DesktopGuiEngine::loadSettings(uint16_t port, uint32_t speedLimitKbps, bool autoAccept, bool airdropEnabled) {
    m_settingsPort = port;
    m_settingsSpeedLimit = speedLimitKbps;
    m_settingsAutoAccept = autoAccept;
    m_settingsAirDropEnabled = airdropEnabled;
}

void DesktopGuiEngine::appendDiagnosticLog(const std::string& logLine) {
    m_diagnosticLogs.push_back(logLine);
}

void DesktopGuiEngine::showSaveSuccess() {
    m_lastNotification = "Settings saved successfully.";
}

}
