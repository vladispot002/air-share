#include <QApplication>
#include <QTest>
#include <QSignalSpy>
#include <QListWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>

#include "landrop/ui/qt/QtDesktopApp.hpp"
#include "landrop/ui/qt/QtDiscoveryWidget.hpp"
#include "landrop/ui/qt/QtTransferWidget.hpp"
#include "landrop/ui/qt/QtStorageWidget.hpp"
#include "landrop/ui/qt/QtSettingsWidget.hpp"

using namespace landrop::ui;

class TestQtGui : public QObject {
    Q_OBJECT
private slots:
    void testAppInitialization();
    void testNavigationBetweenSections();
    void testDirectConnectInputValidation();
    void testButtonsAndDeviceDiscovery();
    void testThemeSwitching();
    void testInterWidgetSignals();
};

void TestQtGui::testAppInitialization() {
    QtDesktopApp app;
    app.initialize();

    QVERIFY(app.getSidebar() != nullptr);
    QCOMPARE(app.getSidebar()->count(), 4);
    QCOMPARE(app.getSidebar()->item(0)->text(), "📡  Radar");
    QCOMPARE(app.getSidebar()->item(1)->text(), "⇄  Transfers");
    QCOMPARE(app.getSidebar()->item(2)->text(), "📁  Storage");
    QCOMPARE(app.getSidebar()->item(3)->text(), "⚙️  Settings");

    QVERIFY(app.getStackedWidget() != nullptr);
    QCOMPARE(app.getStackedWidget()->count(), 4);
    QCOMPARE(app.getStackedWidget()->currentIndex(), 0);

    QVERIFY(app.getDiscoveryWidget() != nullptr);
    QVERIFY(app.getTransferWidget() != nullptr);
    QVERIFY(app.getStorageWidget() != nullptr);
    QVERIFY(app.getSettingsWidget() != nullptr);
}

void TestQtGui::testNavigationBetweenSections() {
    QtDesktopApp app;
    app.initialize();

    auto* sidebar = app.getSidebar();
    auto* stacked = app.getStackedWidget();

    // Navigate to Transfers (index 1)
    sidebar->setCurrentRow(1);
    QCOMPARE(stacked->currentIndex(), 1);

    // Navigate to Storage (index 2)
    sidebar->setCurrentRow(2);
    QCOMPARE(stacked->currentIndex(), 2);

    // Navigate to Settings (index 3)
    sidebar->setCurrentRow(3);
    QCOMPARE(stacked->currentIndex(), 3);

    // Navigate back to Radar (index 0)
    sidebar->setCurrentRow(0);
    QCOMPARE(stacked->currentIndex(), 0);
}

void TestQtGui::testDirectConnectInputValidation() {
    QString errorMsg;

    // Valid IPv4
    QVERIFY(QtDiscoveryWidget::validateDirectConnectInput("192.168.1.100", 8848, &errorMsg));
    QVERIFY(errorMsg.isEmpty());

    // Valid loopback
    QVERIFY(QtDiscoveryWidget::validateDirectConnectInput("127.0.0.1", 8848, &errorMsg));

    // Valid IPv6
    QVERIFY(QtDiscoveryWidget::validateDirectConnectInput("::1", 9000, &errorMsg));

    // Valid port boundary limits
    QVERIFY(QtDiscoveryWidget::validateDirectConnectInput("10.0.0.1", 1, &errorMsg));
    QVERIFY(QtDiscoveryWidget::validateDirectConnectInput("10.0.0.1", 65535, &errorMsg));

    // Invalid port numbers
    QVERIFY(!QtDiscoveryWidget::validateDirectConnectInput("10.0.0.1", 0, &errorMsg));
    QVERIFY(errorMsg.contains("Port"));

    QVERIFY(!QtDiscoveryWidget::validateDirectConnectInput("10.0.0.1", 70000, &errorMsg));
    QVERIFY(errorMsg.contains("Port"));

    // Invalid IP formats
    QVERIFY(!QtDiscoveryWidget::validateDirectConnectInput("999.999.999.999", 8848, &errorMsg));
    QVERIFY(errorMsg.contains("Invalid IP"));

    QVERIFY(!QtDiscoveryWidget::validateDirectConnectInput("random_string_not_ip", 8848, &errorMsg));
    QVERIFY(errorMsg.contains("Invalid IP"));

    // Empty IP
    QVERIFY(!QtDiscoveryWidget::validateDirectConnectInput("   ", 8848, &errorMsg));
    QVERIFY(errorMsg.contains("empty"));
}

void TestQtGui::testButtonsAndDeviceDiscovery() {
    QtDesktopApp app;
    app.initialize();

    auto* discoveryWidget = app.getDiscoveryWidget();
    QVERIFY(discoveryWidget != nullptr);

    // Click Add Loopback button
    QVERIFY(discoveryWidget->getLoopbackBtn() != nullptr);
    QTest::mouseClick(discoveryWidget->getLoopbackBtn(), Qt::LeftButton);

    auto* presenter = app.getDiscoveryPresenter();
    QVERIFY(presenter != nullptr);
    QVERIFY(!presenter->getPeers().empty());


    // Click Refresh button
    QVERIFY(discoveryWidget->getRefreshBtn() != nullptr);
    QTest::mouseClick(discoveryWidget->getRefreshBtn(), Qt::LeftButton);

    // Click Toggle Scan button
    QVERIFY(discoveryWidget->getScanToggleBtn() != nullptr);
    QString beforeText = discoveryWidget->getScanToggleBtn()->text();
    QTest::mouseClick(discoveryWidget->getScanToggleBtn(), Qt::LeftButton);
    QCoreApplication::processEvents();
    QString afterText = discoveryWidget->getScanToggleBtn()->text();
    QVERIFY(beforeText != afterText || afterText.contains("Beacon"));
}

void TestQtGui::testThemeSwitching() {
    QtDesktopApp app;
    app.initialize();

    auto* settingsWidget = app.getSettingsWidget();
    QVERIFY(settingsWidget != nullptr);

    QSignalSpy themeSpy(settingsWidget, &QtSettingsWidget::themeChanged);

    // Apply Light Theme
    app.applyTheme("Light");
    QCOMPARE(app.property("class").toString(), "light-theme");

    // Apply Dark Theme
    app.applyTheme("Dark");
    QCOMPARE(app.property("class").toString(), "dark-theme");

    // Trigger theme combo box change
    auto* combo = settingsWidget->getThemeCombo();
    QVERIFY(combo != nullptr);
    combo->setCurrentIndex(1); // switch theme
    QCOMPARE(themeSpy.count(), 1);
}


void TestQtGui::testInterWidgetSignals() {
    QtDesktopApp app;
    app.initialize();

    auto* discovery = app.getDiscoveryWidget();
    auto* stacked = app.getStackedWidget();

    // Initially at Radar (index 0)
    QCOMPARE(stacked->currentIndex(), 0);

    // Simulate requesting to send file to a peer
    emit discovery->sendFileRequested("peer-loopback-01", "/tmp/sample_transfer.bin");

    // Must automatically navigate to Transfers tab (index 1)
    QCOMPARE(stacked->currentIndex(), 1);
}

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication a(argc, argv);
    TestQtGui test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_gui.moc"
