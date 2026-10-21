#include "landrop/ui/DesktopGuiEngine.hpp"
#include <iostream>
#include <thread>
#include <chrono>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstring>

namespace landrop::ui {

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

    m_lastNotification = "LAN-Drop Desktop Ready.";

    return initX11();
}

bool DesktopGuiEngine::initX11() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return false;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    unsigned long black = BlackPixel(display, screen);
    unsigned long white = WhitePixel(display, screen);

    Window win = XCreateSimpleWindow(
        display, root, 50, 50,
        static_cast<unsigned int>(m_width),
        static_cast<unsigned int>(m_height),
        1, black, white
    );

    XStoreName(display, win, m_title.c_str());
    XSelectInput(display, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(display, win);

    GC gc = XCreateGC(display, win, 0, nullptr);
    XSetForeground(display, gc, black);

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
            } else if (key == XK_r) {
                triggerAction(1);
            } else if (key == XK_n) {
                triggerAction(2);
            } else if (key == XK_p) {
                triggerAction(3);
            } else if (key == XK_c) {
                triggerAction(5);
            } else if (key == XK_q || key == XK_Escape) {
                m_isRunning = false;
            }
        } else if (ev.type == ButtonPress) {
            int x = ev.xbutton.x;
            int y = ev.xbutton.y;

            if (y >= 10 && y <= 45) {
                if (x >= 20 && x <= 160) {
                    switchScreen(GuiScreen::Discovery);
                } else if (x >= 170 && x <= 310) {
                    switchScreen(GuiScreen::Transfers);
                } else if (x >= 320 && x <= 460) {
                    switchScreen(GuiScreen::Storage);
                } else if (x >= 470 && x <= 610) {
                    switchScreen(GuiScreen::Settings);
                }
            } else if (y >= 540 && y <= 580) {
                if (x >= 20 && x <= 140) {
                    triggerAction(1);
                } else if (x >= 150 && x <= 290) {
                    triggerAction(2);
                } else if (x >= 300 && x <= 420) {
                    triggerAction(3);
                } else if (x >= 430 && x <= 550) {
                    triggerAction(4);
                } else if (x >= 560 && x <= 680) {
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

    XClearWindow(display, win);

    XDrawRectangle(display, win, gc, 10, 10, m_width - 20, m_height - 20);

    XDrawRectangle(display, win, gc, 20, 15, 140, 30);
    XDrawString(display, win, gc, 30, 35, "[1] Discovery", 13);

    XDrawRectangle(display, win, gc, 170, 15, 140, 30);
    XDrawString(display, win, gc, 180, 35, "[2] Transfers", 13);

    XDrawRectangle(display, win, gc, 320, 15, 140, 30);
    XDrawString(display, win, gc, 335, 35, "[3] Storage", 11);

    XDrawRectangle(display, win, gc, 470, 15, 140, 30);
    XDrawString(display, win, gc, 485, 35, "[4] Settings", 12);

    XDrawLine(display, win, gc, 10, 55, m_width - 10, 55);

    if (m_currentScreen == GuiScreen::Discovery) {
        XDrawString(display, win, gc, 30, 80, "SCREEN: RADAR / NETWORK DISCOVERY", 33);
        std::string scanInfo = "Scanner Status: " + std::string(m_isScanning ? "ACTIVE (Listening for LAN Beacons)" : "IDLE");
        XDrawString(display, win, gc, 30, 105, scanInfo.c_str(), static_cast<int>(scanInfo.size()));

        XDrawRectangle(display, win, gc, 30, 120, m_width - 60, 240);
        XDrawString(display, win, gc, 40, 140, "DEVICE LIST CONTAINER (Discovered Nodes):", 41);

        int rowY = 170;
        for (size_t i = 0; i < m_deviceList.size() && i < 6; ++i) {
            const auto& dev = m_deviceList[i];
            std::string line = "[" + std::to_string(i + 1) + "] " + dev.name + " (" + dev.endpoint + ") | Type: " + dev.type +
                               " | RSSI: " + std::to_string(dev.signalRssi) + "dBm | " + (dev.isOnline ? "Online" : "Offline");
            XDrawString(display, win, gc, 45, rowY, line.c_str(), static_cast<int>(line.size()));
            rowY += 28;
        }

        XDrawRectangle(display, win, gc, 30, 380, m_width - 60, 80);
        XDrawString(display, win, gc, 40, 405, "NFC QUICK-PAIR INTEGRATION:", 27);
        XDrawString(display, win, gc, 40, 430, "Payload input: LANDROP_BEACON|MobileNfcNode|uuid-nfc-77|8848", 59);

    } else if (m_currentScreen == GuiScreen::Transfers) {
        XDrawString(display, win, gc, 30, 80, "SCREEN: TRANSFER MANAGER", 24);

        XDrawRectangle(display, win, gc, 30, 100, m_width - 60, 280);
        XDrawString(display, win, gc, 40, 125, "TRANSFER TABLE CONTAINER:", 25);
        XDrawString(display, win, gc, 40, 150, "ID       FILENAME                     SIZE        PROGRESS     SPEED       STATE", 76);
        XDrawLine(display, win, gc, 40, 155, m_width - 40, 155);

        int rowY = 180;
        for (size_t i = 0; i < m_transferList.size() && i < 6; ++i) {
            const auto& t = m_transferList[i];
            char rowBuf[256];
            snprintf(rowBuf, sizeof(rowBuf), "%-8s %-28s %-11llu %3d%%        %-11.1f %-10s",
                     t.taskId.c_str(), t.fileName.c_str(),
                     static_cast<unsigned long long>(t.fileSize),
                     static_cast<int>(t.progress),
                     t.speedBytesPerSec / 1024.0,
                     t.stateStr.c_str());
            XDrawString(display, win, gc, 40, rowY, rowBuf, static_cast<int>(strlen(rowBuf)));
            rowY += 25;
        }

        XDrawRectangle(display, win, gc, 30, 400, m_width - 60, 70);
        std::string selText = "Selected: " + (m_selectedTransfer.fileName.empty() ? "None" : m_selectedTransfer.fileName + " (" + m_selectedTransfer.stateStr + ")");
        XDrawString(display, win, gc, 40, 430, selText.c_str(), static_cast<int>(selText.size()));

    } else if (m_currentScreen == GuiScreen::Storage) {
        XDrawString(display, win, gc, 30, 80, "SCREEN: SHARED STORAGE REPOSITORY", 33);
        std::string pathText = "Shared Folder Root: " + m_sharedPath;
        XDrawString(display, win, gc, 30, 110, pathText.c_str(), static_cast<int>(pathText.size()));

        std::string statsText = "Stats: " + std::to_string(m_sharedFileCount) + " files | Total: " +
                                std::to_string(m_sharedTotalBytes / 1024) + " KB | Password: " +
                                (m_isPasswordProtected ? "PROTECTED" : "NONE");
        XDrawString(display, win, gc, 30, 135, statsText.c_str(), static_cast<int>(statsText.size()));

        XDrawRectangle(display, win, gc, 30, 150, m_width - 60, 240);
        XDrawString(display, win, gc, 40, 175, "SHARED FILES CATALOG:", 21);

        int rowY = 205;
        for (size_t i = 0; i < m_fileList.size() && i < 6; ++i) {
            const auto& file = m_fileList[i];
            std::string line = file.fileName + " (" + std::to_string(file.fileSize) + " bytes, " +
                               std::to_string(file.totalChunks) + " chunks) SHA: " + file.fileSha256.substr(0, 16) + "...";
            XDrawString(display, win, gc, 40, rowY, line.c_str(), static_cast<int>(line.size()));
            rowY += 26;
        }

    } else if (m_currentScreen == GuiScreen::Settings) {
        XDrawString(display, win, gc, 30, 80, "SCREEN: SETTINGS & DIAGNOSTICS", 30);
        std::string s1 = "Listen Port: " + std::to_string(m_settingsPort);
        std::string s2 = "Transfer Speed Limit: " + (m_settingsSpeedLimit == 0 ? "Unlimited" : std::to_string(m_settingsSpeedLimit) + " KB/s");
        std::string s3 = "Auto-Accept Transfers: " + std::string(m_settingsAutoAccept ? "[X] ENABLED" : "[ ] DISABLED");
        std::string s4 = "AirDrop Protocol Bridge: " + std::string(m_settingsAirDropEnabled ? "[X] ACTIVE" : "[ ] INACTIVE");

        XDrawString(display, win, gc, 30, 115, s1.c_str(), static_cast<int>(s1.size()));
        XDrawString(display, win, gc, 30, 140, s2.c_str(), static_cast<int>(s2.size()));
        XDrawString(display, win, gc, 30, 165, s3.c_str(), static_cast<int>(s3.size()));
        XDrawString(display, win, gc, 30, 190, s4.c_str(), static_cast<int>(s4.size()));

        XDrawRectangle(display, win, gc, 30, 220, m_width - 60, 200);
        XDrawString(display, win, gc, 40, 245, "DIAGNOSTIC LOGS CONTAINER:", 26);
        int logY = 275;
        for (int i = static_cast<int>(m_diagnosticLogs.size()) - 1; i >= 0 && logY < 400; --i) {
            XDrawString(display, win, gc, 40, logY, m_diagnosticLogs[static_cast<size_t>(i)].c_str(),
                        static_cast<int>(m_diagnosticLogs[static_cast<size_t>(i)].size()));
            logY += 24;
        }
    }

    XDrawRectangle(display, win, gc, 20, 540, 120, 35);
    XDrawString(display, win, gc, 30, 562, "[R] Refresh", 11);

    XDrawRectangle(display, win, gc, 150, 540, 140, 35);
    XDrawString(display, win, gc, 160, 562, "[N] NFC Pair", 12);

    XDrawRectangle(display, win, gc, 300, 540, 120, 35);
    XDrawString(display, win, gc, 315, 562, "[P] Pause", 9);

    XDrawRectangle(display, win, gc, 430, 540, 120, 35);
    XDrawString(display, win, gc, 445, 562, "[C] Resume", 10);

    XDrawRectangle(display, win, gc, 560, 540, 120, 35);
    XDrawString(display, win, gc, 575, 562, "[X] Cancel", 10);

    XDrawRectangle(display, win, gc, 20, 585, m_width - 40, 25);
    std::string notif = "STATUS: " + m_lastNotification;
    XDrawString(display, win, gc, 30, 602, notif.c_str(), static_cast<int>(notif.size()));

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
