// End-to-end: the real window, a real QSerialPort and a real tty (a pseudo-terminal pair),
// with the fake firmware answering on the other end. Linux only.
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSocketNotifier>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "fakefirmware.h"
#include "helpdialog.h"
#include "mainwindow.h"

#ifdef Q_OS_LINUX
#include <pty.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

QByteArray randomBytes(int n, quint32 seed)
{
    QRandomGenerator g(seed);
    QByteArray b(n, 0);
    for (int i = 0; i < n; ++i)
        b[i] = static_cast<char>(g.bounded(256));
    return b;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TstE2E : public QObject
{
    Q_OBJECT

    // ---- pseudo-terminal wired to the fake firmware ----
    int m_master = -1;
    int m_slave = -1;
    QString m_slaveName;
    QSocketNotifier *m_notifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QByteArray m_toDevice;   // firmware answers not yet accepted by the pty
    FakeFirmware *m_fw = nullptr;
    MainWindow *m_win = nullptr;
    QTemporaryDir m_dir;
    QTemporaryDir m_settingsDir;

    // dialog scripting
    QString m_nextOpen;
    QStringList m_openQueue;
    QString m_nextSave;
    QList<QPair<QString, QString>> m_confirms;
    bool m_confirmAnswer = true;

    template <class T> T *w(const char *name) { return m_win->findChild<T *>(name); }
    QString logText() { return w<QPlainTextEdit>("logView")->toPlainText(); }
    bool enabled(const char *name) { return w<QPushButton>(name)->isEnabled(); }

    void startFixture()
    {
#ifdef Q_OS_LINUX
        char name[256] = {};
        QVERIFY(openpty(&m_master, &m_slave, name, nullptr, nullptr) == 0);
        struct termios t;
        tcgetattr(m_slave, &t);
        cfmakeraw(&t);
        tcsetattr(m_slave, TCSANOW, &t);
        m_slaveName = QString::fromLocal8Bit(name);

        ::fcntl(m_master, F_SETFL, ::fcntl(m_master, F_GETFL) | O_NONBLOCK);
        m_fw = new FakeFirmware(this);
        m_notifier = new QSocketNotifier(m_master, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, [this]() {
            char buf[4096];
            const ssize_t n = ::read(m_master, buf, sizeof buf);
            if (n > 0)
                m_fw->write(buf, n);
        });
        // A pty accepts only a few KiB at a time: queue the answers and flush when writable.
        m_writeNotifier = new QSocketNotifier(m_master, QSocketNotifier::Write, this);
        m_writeNotifier->setEnabled(false);
        auto flush = [this]() {
            while (m_master >= 0 && !m_toDevice.isEmpty()) {
                const ssize_t n = ::write(m_master, m_toDevice.constData(), static_cast<size_t>(m_toDevice.size()));
                if (n <= 0)
                    break;
                m_toDevice.remove(0, static_cast<int>(n));
            }
            if (m_writeNotifier)
                m_writeNotifier->setEnabled(m_master >= 0 && !m_toDevice.isEmpty());
        };
        connect(m_writeNotifier, &QSocketNotifier::activated, this, flush);
        connect(m_fw, &QIODevice::readyRead, this, [this, flush]() {
            m_toDevice.append(m_fw->readAll());
            flush();
        });

        qputenv("MD_REWRITER_EXTRA_PORT", m_slaveName.toLocal8Bit());
        m_win = new MainWindow;
        MainWindow::DialogHooks hooks;
        hooks.openFile = [this](const QString &, const QString &) {
            if (!m_openQueue.isEmpty())
                return m_openQueue.takeFirst();
            return m_nextOpen;
        };
        hooks.saveFile = [this](const QString &, const QString &) { return m_nextSave; };
        hooks.confirm = [this](const QString &t, const QString &x) {
            m_confirms.append({t, x});
            return m_confirmAnswer;
        };
        m_win->setDialogHooks(hooks);
        m_win->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_win));
#endif
    }

    void stopFixture()
    {
        delete m_win;
        m_win = nullptr;
        delete m_notifier;
        m_notifier = nullptr;
        delete m_writeNotifier;
        m_writeNotifier = nullptr;
        m_toDevice.clear();
        delete m_fw;
        m_fw = nullptr;
#ifdef Q_OS_LINUX
        if (m_master >= 0) ::close(m_master);
        if (m_slave >= 0) ::close(m_slave);
#endif
        m_master = m_slave = -1;
        m_confirms.clear();
        m_confirmAnswer = true;
        m_nextOpen.clear();
        m_nextSave.clear();
        m_openQueue.clear();
    }

    void connectDevice()
    {
        auto *combo = w<QComboBox>("portCombo");
        const int idx = combo->findData(m_slaveName);
        QVERIFY2(idx >= 0, "the test tty must be offered in the port list");
        combo->setCurrentIndex(idx);
        QTest::mouseClick(w<QPushButton>("connectButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("Device identified"), 5000);
        QVERIFY(enabled("readRomButton"));
    }

private slots:
    void initTestCase()
    {
#ifndef Q_OS_LINUX
        QSKIP("pseudo-terminals are Linux specific");
#endif
        QCoreApplication::setOrganizationName("MDRewriterTest");
        QCoreApplication::setApplicationName("e2e");
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settingsDir.path());
        QVERIFY(m_dir.isValid());
    }

    void init() { startFixture(); }
    void cleanup() { stopFixture(); }

    void startsDisconnectedWithOnlyConnectionEnabled()
    {
        QVERIFY(w<QComboBox>("portCombo")->findData(m_slaveName) >= 0);
        QVERIFY(enabled("connectButton"));
        for (const char *n : {"detectButton", "readRomButton", "writeRomButton", "readSaveButton", "writeSaveButton", "eraseSizeButton", "eraseAllButton", "cancelButton"})
            QVERIFY2(!enabled(n), n);
        QVERIFY(enabled("compareButton"));   // needs no device
        QVERIFY(m_win->statusBar()->findChild<QLabel *>()->text().contains("Not connected"));
    }

    void connectEnablesActionsAndDisconnectDisables()
    {
        connectDevice();
        for (const char *n : {"detectButton", "readRomButton", "writeRomButton", "readSaveButton", "writeSaveButton", "eraseSizeButton", "eraseAllButton"})
            QVERIFY2(enabled(n), n);
        QVERIFY(!enabled("cancelButton"));
        QVERIFY(m_win->statusBar()->findChild<QLabel *>()->text().contains("Connected to"));
        QCOMPARE(w<QPushButton>("connectButton")->text().remove('&'), QString("Disconnect"));
        QTest::mouseClick(w<QPushButton>("connectButton"), Qt::LeftButton);
        QVERIFY(!enabled("readRomButton"));
        QVERIFY(logText().contains("Device disconnected"));
    }

    void detect()
    {
        connectDevice();
        QTest::mouseClick(w<QPushButton>("detectButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("MX29LV640EB MD FLASH CART"), 5000);
        QTRY_VERIFY(enabled("detectButton"));
    }

    void readRomToFile()
    {
        m_fw->rom = randomBytes(0x80000, 1);
        m_fw->setFragmentation(512, 3);
        connectDevice();
        w<QComboBox>("romSizeCombo")->setCurrentIndex(0);
        m_nextSave = m_dir.filePath("dump.bin");
        QTest::mouseClick(w<QPushButton>("readRomButton"), Qt::LeftButton);
        // while running: only Cancel is usable
        QTRY_VERIFY_WITH_TIMEOUT(!enabled("readRomButton"), 2000);
        QVERIFY(enabled("cancelButton"));
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("Saved to"), 20000);
        QCOMPARE(readFile(m_nextSave), m_fw->rom);
        QCOMPARE(w<QProgressBar>("progressBar")->value(), 100);
        QTRY_VERIFY(enabled("readRomButton"));
        QVERIFY(!enabled("cancelButton"));
    }

    void cancelReadLeavesNoFile()
    {
        m_fw->rom = randomBytes(0x80000, 2);
        m_fw->setFragmentation(200, 3);
        connectDevice();
        m_nextSave = m_dir.filePath("cancelled.bin");
        QTest::mouseClick(w<QPushButton>("readRomButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(w<QProgressBar>("progressBar")->value() > 5, 10000);
        QTest::mouseClick(w<QPushButton>("cancelButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("incomplete file was not saved"), 20000);
        QVERIFY2(!QFileInfo::exists(m_nextSave), "a cancelled dump must not leave a file behind");
        // device still usable afterwards
        QTRY_VERIFY(enabled("detectButton"));
        QTest::mouseClick(w<QPushButton>("detectButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("MD FLASH CART"), 5000);
    }

    void writeRomAsksFirstAndDeclineSendsNothing()
    {
        connectDevice();
        const QString f = m_dir.filePath("game.bin");
        QVERIFY(writeFile(f, randomBytes(0x80000, 4)));
        m_nextOpen = f;
        m_confirmAnswer = false;
        const int before = m_fw->commands.size();
        QTest::mouseClick(w<QPushButton>("writeRomButton"), Qt::LeftButton);
        QCOMPARE(m_confirms.size(), 1);
        QVERIFY(m_confirms[0].second.contains("game.bin"));
        QVERIFY(m_confirms[0].second.contains("overwritten"));
        QTest::qWait(100);
        QCOMPARE(m_fw->commands.size(), before);
        QVERIFY(enabled("readRomButton"));
    }

    void writeRomEndToEnd()
    {
        m_fw->rom = QByteArray(0x80000, '\xFF');
        m_fw->setFragmentation(700, 9);
        connectDevice();
        const QByteArray image = randomBytes(0x80000, 5);
        const QString f = m_dir.filePath("game2.bin");
        QVERIFY(writeFile(f, image));
        m_nextOpen = f;
        QTest::mouseClick(w<QPushButton>("writeRomButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("Game written"), 30000);
        QCOMPARE(m_fw->rom, image);
        QVERIFY(m_fw->errors.isEmpty());
    }

    void writeRomWithUnsupportedSizeIsRejectedBeforeAnything()
    {
        connectDevice();
        const QString f = m_dir.filePath("odd.bin");
        QVERIFY(writeFile(f, QByteArray(1234, 'x')));
        m_nextOpen = f;
        const int before = m_fw->commands.size();
        QTest::mouseClick(w<QPushButton>("writeRomButton"), Qt::LeftButton);
        QVERIFY(logText().contains("not supported"));
        QVERIFY(m_confirms.isEmpty());
        QTest::qWait(50);
        QCOMPARE(m_fw->commands.size(), before);
    }

    void saveRoundTrip()
    {
        m_fw->sram = randomBytes(0x8000, 6);
        connectDevice();
        w<QComboBox>("saveSizeCombo")->setCurrentIndex(0);   // 32K
        m_nextSave = m_dir.filePath("save.srm");
        QTest::mouseClick(w<QPushButton>("readSaveButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("Saved to"), 10000);
        QCOMPARE(readFile(m_nextSave), m_fw->sram);

        const QByteArray newSave = randomBytes(0x8000, 7);
        const QString f = m_dir.filePath("new.srm");
        QVERIFY(writeFile(f, newSave));
        m_nextOpen = f;
        QTest::mouseClick(w<QPushButton>("writeSaveButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("Save written"), 10000);
        QCOMPARE(m_fw->sram, newSave);
    }

    void destructiveActionsAskAndHonourNo()
    {
        connectDevice();
        m_confirmAnswer = false;
        const int before = m_fw->commands.size();
        QTest::mouseClick(w<QPushButton>("eraseAllButton"), Qt::LeftButton);
        QTest::mouseClick(w<QPushButton>("eraseSizeButton"), Qt::LeftButton);
        QCOMPARE(m_confirms.size(), 2);
        QVERIFY(m_confirms[0].second.contains("lost"));
        QTest::qWait(100);
        QCOMPARE(m_fw->commands.size(), before);
    }

    void eraseBySizeShowsProgress()
    {
        connectDevice();
        QTest::mouseClick(w<QPushButton>("eraseSizeButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("Erase complete"), 10000);
        QCOMPARE(w<QProgressBar>("progressBar")->value(), 100);
    }

    void eraseFailureIsShownAsError()
    {
        m_fw->eraseFails = true;
        connectDevice();
        QTest::mouseClick(w<QPushButton>("eraseAllButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("[error]"), 10000);
        QTRY_VERIFY(enabled("eraseAllButton"));
    }

    void unpluggingMidOperationRecovers()
    {
        m_fw->rom = randomBytes(0x80000, 8);
        m_fw->setFragmentation(100, 2);
        connectDevice();
        m_nextSave = m_dir.filePath("unplug.bin");
        QTest::mouseClick(w<QPushButton>("readRomButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(w<QProgressBar>("progressBar")->value() > 3, 10000);
        ::close(m_master);   // the device disappears
        m_master = -1;
        delete m_notifier;
        m_notifier = nullptr;
        delete m_writeNotifier;
        m_writeNotifier = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("[error]"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(w<QPushButton>("connectButton")->text().remove('&') == "Connect", 5000);
        QVERIFY(!enabled("readRomButton"));
        QVERIFY2(!QFileInfo::exists(m_nextSave), "no partial file after the device vanished");
    }

    void compareFiles()
    {
        QByteArray a = randomBytes(300000, 10), b = a;
        for (int i = 0; i < 300; ++i)
            b[i * 7] = static_cast<char>(b[i * 7] ^ 0xFF);
        QVERIFY(writeFile(m_dir.filePath("a.bin"), a));
        QVERIFY(writeFile(m_dir.filePath("b.bin"), b));
        QVERIFY(writeFile(m_dir.filePath("c.bin"), a));
        // no device connected: comparison must work anyway
        m_openQueue = {m_dir.filePath("a.bin"), m_dir.filePath("b.bin")};
        QTest::mouseClick(w<QPushButton>("compareButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("300 difference"), 5000);
        QVERIFY(logText().contains("Only the first 20 differences are listed"));
        QCOMPARE(logText().count("differs"), 20);

        m_win->findChild<QPlainTextEdit *>("logView")->clear();
        m_openQueue = {m_dir.filePath("a.bin"), m_dir.filePath("c.bin")};
        QTest::mouseClick(w<QPushButton>("compareButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("identical"), 5000);
    }

    void layoutHasNoClippedControls_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("minimum") << QSize(0, 0);   // 0 = the smallest size the window allows
        QTest::newRow("default") << QSize(460, 720);
        QTest::newRow("wide") << QSize(900, 800);
    }
    void layoutHasNoClippedControls()
    {
        QFETCH(QSize, size);
        m_win->resize(size.isEmpty() ? m_win->minimumSizeHint() : size);
        QTest::qWait(50);
        const auto buttons = m_win->findChildren<QPushButton *>();
        QVERIFY(buttons.size() >= 12);
        for (QPushButton *b : buttons) {
            QVERIFY2(b->width() >= b->sizeHint().width(), qPrintable(b->objectName() + " is clipped horizontally"));
            QVERIFY2(b->height() >= b->sizeHint().height(), qPrintable(b->objectName() + " is clipped vertically"));
        }
        for (QLabel *l : m_win->findChildren<QLabel *>()) {
            if (l->text().isEmpty()) continue;
            QVERIFY2(l->width() >= l->sizeHint().width(), qPrintable(l->text() + " is clipped"));
        }
        QVERIFY2(m_win->minimumSizeHint().width() <= 560 && m_win->minimumSizeHint().height() <= 680,
                 qPrintable(QStringLiteral("minimum size too large: %1x%2").arg(m_win->minimumSizeHint().width()).arg(m_win->minimumSizeHint().height())));
        // optional artefact for visual review
        const QString shots = qEnvironmentVariable("MD_SCREENSHOT_DIR");
        if (!shots.isEmpty()) {
            QDir().mkpath(shots);
            m_win->grab().save(QStringLiteral("%1/ui-%2x%3.png").arg(shots).arg(size.width()).arg(size.height()));
        }
    }

    void helpMenuHasGuideAndAbout()
    {
        const auto menus = m_win->menuBar()->actions();
        QCOMPARE(menus.size(), 1);
        QCOMPARE(menus[0]->text().remove('&'), QString("Help"));
        const auto items = menus[0]->menu()->actions();
        QStringList texts;
        for (auto *a : items)
            if (!a->isSeparator()) texts << a->text().remove('&');
        QCOMPARE(texts, (QStringList{"User guide", "About MD Rewriter"}));
    }

    void f1OpensTheGuideEvenWhileBusy()
    {
        m_fw->rom = randomBytes(0x80000, 12);
        m_fw->setFragmentation(100, 2);
        connectDevice();
        m_nextSave = m_dir.filePath("busy.bin");
        QTest::mouseClick(w<QPushButton>("readRomButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!enabled("readRomButton"), 3000);
        QVERIFY(!m_win->findChild<HelpDialog *>("helpDialog"));
        QTest::keyClick(m_win, Qt::Key_F1);
        auto *h = m_win->findChild<HelpDialog *>("helpDialog");
        QVERIFY(h);
        QVERIFY(h->isVisible());
        QVERIFY(h->plainText().contains("Quick start"));
        // the operation keeps running while the guide is open
        QTRY_VERIFY_WITH_TIMEOUT(logText().contains("Saved to"), 30000);
        h->close();
    }

    void aboutBoxMentionsLicenseAndLinks()
    {
        QString shown;
        QTimer::singleShot(300, this, [&shown]() {
            for (QWidget *top : QApplication::topLevelWidgets()) {
                if (top->objectName() == "aboutBox") {
                    shown = static_cast<QMessageBox *>(top)->text();
                    top->close();
                }
            }
        });
        const auto items = m_win->menuBar()->actions()[0]->menu()->actions();
        items.last()->trigger();
        QVERIFY2(shown.contains("MD Rewriter"), qPrintable(shown));
        QVERIFY(shown.contains("MIT"));
        QVERIFY(shown.contains("github.com/legendaryredfox/md_rewriter"));
        QVERIFY(shown.contains("buymeacoffee.com/legendaryredfox"));
        QVERIFY(shown.contains("F1"));
    }

    void closeWhileBusyAsksFirst()
    {
        m_fw->rom = randomBytes(0x80000, 11);
        m_fw->setFragmentation(100, 2);
        connectDevice();
        m_nextSave = m_dir.filePath("x.bin");
        QTest::mouseClick(w<QPushButton>("readRomButton"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!enabled("readRomButton"), 3000);
        m_confirmAnswer = false;
        QVERIFY(!m_win->close());            // refused
        QVERIFY(m_win->isVisible());
        QCOMPARE(m_confirms.last().first, QString("Operation in progress"));
        m_confirmAnswer = true;
        QVERIFY(m_win->close());             // accepted
    }
};

QTEST_MAIN(TstE2E)
#include "tst_e2e.moc"
