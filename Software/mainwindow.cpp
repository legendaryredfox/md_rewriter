#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "helpdialog.h"
#include "romimage.h"

#include <QBuffer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <climits>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

namespace {

constexpr quint16 kStmVendorId = 0x0483;   // USB descriptor of the firmware (STM32 Virtual ComPort)
constexpr quint16 kStmProductId = 0x5740;
constexpr int kBaudRate = 1000000;

using Severity = DeviceSession::Severity;

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_session(this)
{
    ui->setupUi(this);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *guideAction = helpMenu->addAction(tr("&User guide"), this, &MainWindow::showGuide);
    guideAction->setShortcut(QKeySequence::HelpContents);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About MD Rewriter"), this, &MainWindow::showAbout);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);

    // Sizes come from the protocol tables, so the UI cannot offer what the firmware cannot do.
    refreshRomSizes();
    for (const auto &s : Protocol::sramSizes())
        ui->saveSizeCombo->addItem(humanSize(s.bytes));

    ui->verifyCheck->setChecked(QSettings().value(QStringLiteral("verifyAfterWrite"), true).toBool());
    connect(ui->verifyCheck, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("verifyAfterWrite"), on);
    });

    const QIcon warn = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    ui->eraseSizeButton->setIcon(warn);
    ui->eraseAllButton->setIcon(warn);

    ui->logView->setMaximumBlockCount(5000);
    ui->logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    m_session.setDevice(&m_serial);

    connect(ui->refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(ui->detectButton, &QPushButton::clicked, this, &MainWindow::detectCartridge);
    connect(ui->compareButton, &QPushButton::clicked, this, &MainWindow::compareFiles);
    connect(ui->readRomButton, &QPushButton::clicked, this, &MainWindow::readRom);
    connect(ui->writeRomButton, &QPushButton::clicked, this, &MainWindow::writeRom);
    connect(ui->readSaveButton, &QPushButton::clicked, this, &MainWindow::readSave);
    connect(ui->writeSaveButton, &QPushButton::clicked, this, &MainWindow::writeSave);
    connect(ui->eraseSizeButton, &QPushButton::clicked, this, &MainWindow::eraseBySize);
    connect(ui->eraseAllButton, &QPushButton::clicked, this, &MainWindow::eraseAll);
    connect(ui->cancelButton, &QPushButton::clicked, this, &MainWindow::cancelCurrent);
    connect(ui->clearLogButton, &QPushButton::clicked, this, &MainWindow::clearLog);

    connect(&m_session, &DeviceSession::started, this, &MainWindow::onSessionStarted);
    connect(&m_session, &DeviceSession::finished, this, &MainWindow::onSessionFinished);
    connect(&m_session, &DeviceSession::message, this, &MainWindow::onSessionMessage);
    connect(&m_session, &DeviceSession::deviceText, this, &MainWindow::onDeviceText);
    connect(&m_session, &DeviceSession::progress, this, &MainWindow::onProgress);
    connect(&m_session, &DeviceSession::firmwareVersionChanged, this, &MainWindow::onFirmwareVersion);

    connect(&m_serial, &QSerialPort::errorOccurred, this, &MainWindow::onSerialError);

    // Pick up a device that is plugged in or removed while the window is open.
    m_portTimer.setInterval(2000);
    connect(&m_portTimer, &QTimer::timeout, this, [this]() {
        if (!connected())
            refreshPorts();
    });
    m_portTimer.start();

    m_cmpTimer.setSingleShot(false);
    m_cmpTimer.setInterval(0);
    connect(&m_cmpTimer, &QTimer::timeout, this, &MainWindow::stepCompare);

    refreshPorts();
    updateUi();
}

MainWindow::~MainWindow()
{
    if (m_serial.isOpen())
        m_serial.close();
}

// ------------------------------------------------------------------ helpers

bool MainWindow::connected() const
{
    return m_serial.isOpen();
}

bool MainWindow::working() const
{
    return m_session.busy() || m_comparer != nullptr;
}

QString MainWindow::humanSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 0, QLocale::DataSizeIecFormat);
}

const Protocol::RomSize &MainWindow::selectedRomSize() const
{
    const auto &v = Protocol::romSizes();
    const int i = ui->romSizeCombo->currentIndex();
    if (i >= v.size() && m_session.canRead8M())
        return Protocol::extendedRomSize();
    return v[qBound(0, i, v.size() - 1)];
}

// The size list is what every firmware can do; 8 MiB is added when the firmware says it can.
void MainWindow::refreshRomSizes()
{
    const int keep = ui->romSizeCombo->currentIndex();
    const QSignalBlocker block(ui->romSizeCombo);
    ui->romSizeCombo->clear();
    for (const auto &s : Protocol::romSizes())
        ui->romSizeCombo->addItem(humanSize(s.bytes));
    if (m_session.canRead8M())
        ui->romSizeCombo->addItem(humanSize(Protocol::extendedRomSize().bytes));
    ui->romSizeCombo->setCurrentIndex(qBound(0, keep, ui->romSizeCombo->count() - 1));
}

void MainWindow::onFirmwareVersion(const QString &version)
{
    refreshRomSizes();
    if (!version.isEmpty())
        log(Severity::Info, tr("Firmware %1 detected: reading 8 MiB is available in the cartridge size list.").arg(version));
}

const Protocol::SramSize &MainWindow::selectedSramSize() const
{
    const auto &v = Protocol::sramSizes();
    return v[qBound(0, ui->saveSizeCombo->currentIndex(), v.size() - 1)];
}

QString MainWindow::lastDir() const
{
    QSettings s;
    const QString d = s.value(QStringLiteral("lastDir")).toString();
    return (!d.isEmpty() && QFileInfo(d).isDir()) ? d : QDir::homePath();
}

void MainWindow::rememberDir(const QString &path)
{
    QSettings s;
    s.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
}

QString MainWindow::askOpenFile(const QString &title, const QString &filter)
{
    const QString path = m_hooks.openFile ? m_hooks.openFile(title, filter)
                                          : QFileDialog::getOpenFileName(this, title, lastDir(), filter);
    if (!path.isEmpty())
        rememberDir(path);
    return path;
}

QString MainWindow::askSaveFile(const QString &title, const QString &filter, const QString &suffix)
{
    QString path = m_hooks.saveFile ? m_hooks.saveFile(title, filter)
                                    : QFileDialog::getSaveFileName(this, title, lastDir(), filter);
    if (path.isEmpty())
        return path;
    if (QFileInfo(path).suffix().isEmpty())
        path += QLatin1Char('.') + suffix;
    rememberDir(path);
    return path;
}

bool MainWindow::confirm(const QString &title, const QString &text, const QString &acceptText)
{
    if (m_hooks.confirm)
        return m_hooks.confirm(title, text);
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, this);
    QPushButton *accept = box.addButton(acceptText, QMessageBox::DestructiveRole);
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == accept;
}

// ------------------------------------------------------------------ log

void MainWindow::log(Severity severity, const QString &text)
{
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    QColor color = palette().color(QPalette::Text);
    QString prefix;
    switch (severity) {
    case Severity::Success:
        color = dark ? QColor(0x6f, 0xd3, 0x6f) : QColor(0x1a, 0x7f, 0x37);
        prefix = QStringLiteral("[ok] ");
        break;
    case Severity::Warning:
        color = dark ? QColor(0xf0, 0xb4, 0x29) : QColor(0x9a, 0x67, 0x00);
        prefix = QStringLiteral("[warning] ");
        break;
    case Severity::Error:
        color = dark ? QColor(0xff, 0x7b, 0x72) : QColor(0xc6, 0x28, 0x28);
        prefix = QStringLiteral("[error] ");
        break;
    case Severity::Info:
        break;
    }
    QTextCharFormat fmt;
    fmt.setForeground(color);
    if (severity != Severity::Info)
        fmt.setFontWeight(QFont::DemiBold);

    ui->logView->moveCursor(QTextCursor::End);
    QTextDocument *doc = ui->logView->document();
    QTextCursor c = ui->logView->textCursor();
    if (doc->characterCount() > 1 && doc->characterAt(doc->characterCount() - 2) != QLatin1Char('\n'))
        c.insertText(QStringLiteral("\n"));
    c.insertText(prefix + text + QLatin1Char('\n'), fmt);
    ui->logView->ensureCursorVisible();
}

void MainWindow::onDeviceText(const QString &raw)
{
    QString text = raw;
    text.remove(QLatin1Char('\r'));
    ui->logView->moveCursor(QTextCursor::End);
    QTextDocument *doc = ui->logView->document();
    // Firmware messages often start or end with an empty line; do not stack blank lines.
    const bool atLineStart = doc->characterCount() <= 1 || doc->characterAt(doc->characterCount() - 2) == QLatin1Char('\n');
    if (atLineStart) {
        int i = 0;
        while (i < text.size() && text.at(i) == QLatin1Char('\n'))
            ++i;
        text.remove(0, i);
    }
    if (text.isEmpty())
        return;
    QTextCharFormat fmt;
    fmt.setForeground(palette().color(QPalette::Text));
    ui->logView->textCursor().insertText(text, fmt);
    ui->logView->ensureCursorVisible();
}

void MainWindow::onSessionMessage(Severity severity, const QString &text)
{
    log(severity, text);
}

void MainWindow::clearLog()
{
    ui->logView->clear();
}

void MainWindow::showGuide()
{
    if (!m_help)
        m_help = new HelpDialog(this);
    m_help->show();
    m_help->raise();
    m_help->activateWindow();
}

void MainWindow::showAbout()
{
    QMessageBox box(QMessageBox::Information, tr("About MD Rewriter"), QString(), QMessageBox::Ok, this);
    box.setObjectName(QStringLiteral("aboutBox"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<h3>MD Rewriter %1</h3>"
                   "<p>Read and write Sega Mega Drive cartridges: back up games and saves, "
                   "write flash cartridges, compare files.</p>"
                   "<p>Press F1 for the user guide.</p>"
                   "<p>Licensed under the MIT license. Built with Qt %2.</p>"
                   "<p><a href=\"https://github.com/legendaryredfox/md_rewriter\">Project page</a> &middot; "
                   "<a href=\"https://buymeacoffee.com/legendaryredfox\">Buy Me a Coffee</a></p>")
                    .arg(QStringLiteral(APP_VERSION), QString::fromLatin1(qVersion())));
    box.exec();
}

// ------------------------------------------------------------------ ports

void MainWindow::refreshPorts()
{
    QStringList locations;
    QStringList labels;
    int preferred = -1;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString label = info.portName();
        if (!info.description().isEmpty())
            label += QStringLiteral(" - ") + info.description();
        if (info.hasVendorIdentifier() && info.hasProductIdentifier())
            label += QStringLiteral(" (%1:%2)").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0'))
                         .arg(info.productIdentifier(), 4, 16, QLatin1Char('0'));
        if (preferred < 0 && info.vendorIdentifier() == kStmVendorId && info.productIdentifier() == kStmProductId)
            preferred = locations.size();
        locations << info.systemLocation();
        labels << label;
    }

    // Lets a user (or a test) point the app at a port the OS does not enumerate, such as a
    // symlink or a pseudo-terminal: MD_REWRITER_EXTRA_PORT=/path/to/tty
    const QString extra = qEnvironmentVariable("MD_REWRITER_EXTRA_PORT");
    if (!extra.isEmpty() && !locations.contains(extra)) {
        locations << extra;
        labels << extra;
    }

    QStringList current;
    for (int i = 0; i < ui->portCombo->count(); ++i)
        current << ui->portCombo->itemData(i).toString();
    if (current == locations) {
        updateUi();
        return;
    }

    const QString keep = ui->portCombo->currentData().toString();
    ui->portCombo->clear();
    for (int i = 0; i < locations.size(); ++i)
        ui->portCombo->addItem(labels[i], locations[i]);
    int idx = ui->portCombo->findData(keep);
    if (idx < 0)
        idx = preferred;
    if (idx >= 0)
        ui->portCombo->setCurrentIndex(idx);
    if (locations.isEmpty())
        ui->portCombo->setPlaceholderText(tr("No serial port found. Plug in the device."));
    updateUi();
}

void MainWindow::closePort()
{
    if (m_session.busy())
        m_session.deviceLost(tr("The connection was closed."));
    if (m_serial.isOpen())
        m_serial.close();
    m_session.resetFirmwareInfo();
}

void MainWindow::toggleConnection()
{
    if (working())
        return;
    if (connected()) {
        closePort();
        log(Severity::Info, tr("Device disconnected."));
        updateUi();
        return;
    }
    const QString location = ui->portCombo->currentData().toString();
    if (location.isEmpty()) {
        log(Severity::Error, tr("Choose a serial port first."));
        return;
    }
    m_serial.setPortName(location);
    m_serial.setBaudRate(kBaudRate);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);
    if (!m_serial.open(QIODevice::ReadWrite)) {
        log(Severity::Error, Protocol::describeOpenError(QSerialPortInfo(m_serial).portName(),
                                                        m_serial.error(), m_serial.errorString()));
        updateUi();
        return;
    }
    log(Severity::Info, tr("Opened %1.").arg(ui->portCombo->currentText()));
    updateUi();
    m_session.connectDevice();
}

void MainWindow::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_serial.isOpen())
        return;
    const QString reason = tr("Lost the connection to the device: %1").arg(m_serial.errorString());
    log(Severity::Error, reason);
    if (m_session.busy())
        m_session.deviceLost(tr("The operation was interrupted."));
    m_serial.close();
    m_session.resetFirmwareInfo();
    updateUi();
}

// ------------------------------------------------------------------ device operations

void MainWindow::detectCartridge()
{
    m_session.detect();
}

void MainWindow::readRom()
{
    const auto &size = selectedRomSize();
    const QString path = askSaveFile(tr("Save the ROM as"), tr("ROM files (*.bin *.md *.gen);;All files (*)"), QStringLiteral("bin"));
    if (path.isEmpty())
        return;
    m_outFile = std::make_unique<QSaveFile>(path);
    if (!m_outFile->open(QIODevice::WriteOnly)) {
        log(Severity::Error, tr("Cannot create \"%1\": %2").arg(path, m_outFile->errorString()));
        m_outFile.reset();
        return;
    }
    m_outPath = path;
    if (!m_session.readRom(size, m_outFile.get())) {
        m_outFile->cancelWriting();
        m_outFile.reset();
    }
}

void MainWindow::readSave()
{
    const auto &size = selectedSramSize();
    const QString path = askSaveFile(tr("Save the save data as"), tr("Save files (*.srm *.sav);;All files (*)"), QStringLiteral("srm"));
    if (path.isEmpty())
        return;
    m_outFile = std::make_unique<QSaveFile>(path);
    if (!m_outFile->open(QIODevice::WriteOnly)) {
        log(Severity::Error, tr("Cannot create \"%1\": %2").arg(path, m_outFile->errorString()));
        m_outFile.reset();
        return;
    }
    m_outPath = path;
    if (!m_session.readSave(size, m_outFile.get())) {
        m_outFile->cancelWriting();
        m_outFile.reset();
    }
}

void MainWindow::writeRom()
{
    const QString path = askOpenFile(tr("Select the ROM file to write"), tr("ROM files (*.bin *.md *.smd *.gen);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        log(Severity::Error, tr("Cannot open \"%1\": %2").arg(path, file.errorString()));
        return;
    }
    // Refuse before reading a huge file into memory (an SMD file has 512 bytes of header on top).
    if (file.size() > RomImage::kMaxRomBytes + RomImage::kSmdHeaderBytes) {
        log(Severity::Error, tr("The file is %1, larger than the 8 MiB this device can write.").arg(humanSize(file.size())));
        return;
    }
    const RomImage::Result rom = RomImage::prepare(file.readAll());
    file.close();
    if (!rom.ok) {
        log(Severity::Error, rom.error);
        return;
    }
    for (const QString &info : rom.infos)
        log(Severity::Info, info);
    for (const QString &warning : rom.warnings)
        log(Severity::Warning, warning);

    QString text = tr("Write \"%1\" (%2) to the cartridge?\n\nThe cartridge must be erased first. "
                      "Anything already on it will be overwritten.")
                       .arg(QFileInfo(path).fileName(), humanSize(rom.data.size()));
    if (!rom.warnings.isEmpty())
        text += QStringLiteral("\n\n") + tr("Warning:") + QLatin1Char(' ') + rom.warnings.join(QStringLiteral("\n"));
    if (m_session.unlistedChipOnOldFirmware()) {
        text += QStringLiteral("\n\n")
                + tr("This chip is not the one firmware 1.0 was written for, and the firmware does not wait for the chip while "
                     "programming. Keep \"Verify after writing\" on and check the result.");
        if (!ui->verifyCheck->isChecked())
            text += QLatin1Char(' ') + tr("Verification is currently off.");
    }
    if (!confirm(tr("Write ROM"), text, tr("Write")))
        return;

    m_romData = rom.data;
    m_romBuffer = std::make_unique<QBuffer>(&m_romData);
    m_romBuffer->open(QIODevice::ReadOnly);
    if (!m_session.writeRom(m_romBuffer.get(), ui->verifyCheck->isChecked())) {
        m_romBuffer.reset();
        m_romData.clear();
    }
}

void MainWindow::writeSave()
{
    const QString path = askOpenFile(tr("Select the save file to write"), tr("Save files (*.srm *.sav);;All files (*)"));
    if (path.isEmpty())
        return;
    m_inFile = std::make_unique<QFile>(path);
    if (!m_inFile->open(QIODevice::ReadOnly)) {
        log(Severity::Error, tr("Cannot open \"%1\": %2").arg(path, m_inFile->errorString()));
        m_inFile.reset();
        return;
    }
    if (Protocol::isWritableSramSize(m_inFile->size())
        && !confirm(tr("Write save"),
                    tr("Replace the save data on the cartridge with \"%1\"?\n\nThe saved games currently on the cartridge will be lost.")
                        .arg(QFileInfo(path).fileName()),
                    tr("Replace"))) {
        m_inFile.reset();
        return;
    }
    if (!m_session.writeSave(m_inFile.get()))
        m_inFile.reset();
}

void MainWindow::eraseBySize()
{
    const auto &size = selectedRomSize();
    if (!confirm(tr("Erase by size"),
                 tr("Erase the first %1 of the cartridge flash?\n\nAll game data in that area will be lost.")
                     .arg(humanSize(size.bytes)),
                 tr("Erase")))
        return;
    m_session.eraseBySize(size);
}

void MainWindow::eraseAll()
{
    if (!confirm(tr("Erase all"),
                 tr("Erase the entire flash chip and the save memory of the cartridge?\n\n"
                    "Everything on the cartridge will be lost. This can take several minutes and cannot be interrupted.\n"
                    "Do not unplug the device while it runs."),
                 tr("Erase everything")))
        return;
    m_session.eraseAll();
}

void MainWindow::cancelCurrent()
{
    if (m_comparer)
        finishCompare(true);
    else
        m_session.cancel();
}

void MainWindow::onSessionStarted(DeviceSession::Op op)
{
    m_lastOp = op;
    updateUi();
}

void MainWindow::onSessionFinished(DeviceSession::Op op, bool ok, bool cancelled)
{
    Q_UNUSED(op);
    if (m_outFile) {
        if (ok) {
            if (m_outFile->commit())
                log(Severity::Success, tr("Saved to \"%1\".").arg(m_outPath));
            else
                log(Severity::Error, tr("Could not save \"%1\": %2").arg(m_outPath, m_outFile->errorString()));
        } else {
            m_outFile->cancelWriting();
            log(Severity::Warning, cancelled ? tr("Cancelled. The incomplete file was not saved.")
                                             : tr("The incomplete file was not saved."));
        }
        m_outFile.reset();
    }
    m_inFile.reset();
    m_romBuffer.reset();
    m_romData.clear();
    updateUi();
}

void MainWindow::onProgress(int percent)
{
    if (percent < 0) {
        ui->progressBar->setRange(0, 0);   // busy indicator
    } else {
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(percent);
    }
}

// ------------------------------------------------------------------ comparison

void MainWindow::compareFiles()
{
    if (working())
        return;
    const QString a = askOpenFile(tr("Select the first file"), tr("All files (*)"));
    if (a.isEmpty())
        return;
    const QString b = askOpenFile(tr("Select the second file"), tr("All files (*)"));
    if (b.isEmpty())
        return;
    m_cmpA = std::make_unique<QFile>(a);
    m_cmpB = std::make_unique<QFile>(b);
    if (!m_cmpA->open(QIODevice::ReadOnly) || !m_cmpB->open(QIODevice::ReadOnly)) {
        log(Severity::Error, tr("Could not open both files."));
        m_cmpA.reset();
        m_cmpB.reset();
        return;
    }
    log(Severity::Info, tr("Comparing \"%1\" with \"%2\"...").arg(QFileInfo(a).fileName(), QFileInfo(b).fileName()));
    m_comparer = std::make_unique<FileComparer>(m_cmpA.get(), m_cmpB.get(), 20);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    m_cmpTimer.start();
    updateUi();
}

void MainWindow::stepCompare()
{
    if (!m_comparer)
        return;
    const bool done = m_comparer->step(1 << 20);
    ui->progressBar->setValue(m_comparer->percent());
    if (done)
        finishCompare(false);
}

void MainWindow::finishCompare(bool cancelled)
{
    m_cmpTimer.stop();
    if (!m_comparer)
        return;
    if (cancelled) {
        log(Severity::Warning, tr("Comparison cancelled."));
    } else if (m_comparer->failed()) {
        log(Severity::Error, tr("Could not read one of the files."));
    } else if (m_comparer->differenceCount() == 0) {
        log(Severity::Success, tr("The two files are identical (%1 bytes).").arg(m_comparer->sizeA()));
        ui->progressBar->setValue(100);
    } else {
        if (m_comparer->sizeA() != m_comparer->sizeB())
            log(Severity::Warning, tr("The files have different sizes: %1 and %2 bytes.")
                                       .arg(m_comparer->sizeA()).arg(m_comparer->sizeB()));
        log(Severity::Warning, tr("%n difference(s) found.", nullptr, static_cast<int>(qMin<quint64>(m_comparer->differenceCount(), INT_MAX))));
        QString list;
        for (quint64 off : m_comparer->firstDifferences())
            list += tr("  byte at offset %1 (0x%2) differs").arg(off).arg(QString::number(off, 16).toUpper()) + QLatin1Char('\n');
        onDeviceText(list);
        if (m_comparer->differenceCount() > static_cast<quint64>(m_comparer->firstDifferences().size()))
            log(Severity::Info, tr("Only the first %1 differences are listed.").arg(m_comparer->firstDifferences().size()));
        ui->progressBar->setValue(100);
    }
    m_comparer.reset();
    m_cmpA.reset();
    m_cmpB.reset();
    updateUi();
}

// ------------------------------------------------------------------ state

void MainWindow::updateUi()
{
    const bool conn = connected();
    const bool busy = working();
    const bool idleDevice = conn && !busy;

    ui->portCombo->setEnabled(!conn && !busy);
    ui->refreshButton->setEnabled(!conn && !busy);
    ui->connectButton->setText(conn ? tr("Dis&connect") : tr("&Connect"));
    ui->connectButton->setEnabled(!busy && (conn || ui->portCombo->count() > 0));

    ui->detectButton->setEnabled(idleDevice);
    ui->readRomButton->setEnabled(idleDevice);
    ui->writeRomButton->setEnabled(idleDevice);
    ui->readSaveButton->setEnabled(idleDevice);
    ui->writeSaveButton->setEnabled(idleDevice);
    ui->eraseSizeButton->setEnabled(idleDevice);
    ui->eraseAllButton->setEnabled(idleDevice);
    ui->romSizeCombo->setEnabled(!busy);
    ui->verifyCheck->setEnabled(!busy);
    ui->saveSizeCombo->setEnabled(!busy);
    ui->compareButton->setEnabled(!busy);

    ui->cancelButton->setEnabled(m_comparer != nullptr || (m_session.busy() && m_session.canCancel()));

    if (!busy) {
        ui->progressBar->setRange(0, 100);
    }
    setStatus();
}

void MainWindow::setStatus()
{
    QString text = connected() ? tr("Connected to %1").arg(m_serial.portName()) : tr("Not connected");
    if (m_comparer) {
        text += tr(" - comparing files");
    } else if (m_session.busy()) {
        QString what;
        switch (m_session.currentOp()) {
        case DeviceSession::Op::Connect: what = tr("connecting"); break;
        case DeviceSession::Op::Detect: what = tr("detecting the cartridge"); break;
        case DeviceSession::Op::ReadRom: what = tr("reading the ROM"); break;
        case DeviceSession::Op::WriteRom: what = tr("writing the ROM"); break;
        case DeviceSession::Op::VerifyRom: what = tr("verifying the written ROM"); break;
        case DeviceSession::Op::ReadSave: what = tr("reading the save data"); break;
        case DeviceSession::Op::WriteSave: what = tr("writing the save data"); break;
        case DeviceSession::Op::EraseAll: what = tr("erasing the cartridge"); break;
        case DeviceSession::Op::EraseBySize: what = tr("erasing sectors"); break;
        case DeviceSession::Op::None: break;
        }
        if (!what.isEmpty())
            text += QStringLiteral(" - ") + what;
    }
    m_statusLabel->setText(text);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_session.busy()) {
        const bool isErase = m_session.currentOp() == DeviceSession::Op::EraseAll
                          || m_session.currentOp() == DeviceSession::Op::EraseBySize;
        const QString text = isErase
            ? tr("The cartridge is being erased. Closing now does not stop the erase, but you will not see the result.")
            : tr("An operation is still running. Closing now will abort it; a partly written cartridge or an unsaved file will be left unfinished.");
        if (!confirm(tr("Operation in progress"), text, tr("Close anyway"))) {
            event->ignore();
            return;
        }
    }
    m_cmpTimer.stop();
    closePort();
    event->accept();
}
