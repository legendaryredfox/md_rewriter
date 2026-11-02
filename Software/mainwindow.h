#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QTimer>

#include <functional>
#include <memory>

#include "devicesession.h"
#include "filecompare.h"

class HelpDialog;
class QBuffer;
class QFile;
class QLabel;
class QSaveFile;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Replace the modal dialogs, so tests can drive the window without a human.
    struct DialogHooks {
        std::function<QString(const QString &title, const QString &filter)> openFile;
        std::function<QString(const QString &title, const QString &filter)> saveFile;
        std::function<bool(const QString &title, const QString &text)> confirm;
    };
    void setDialogHooks(const DialogHooks &hooks) { m_hooks = hooks; }

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshPorts();
    void toggleConnection();
    void detectCartridge();
    void readRom();
    void writeRom();
    void readSave();
    void writeSave();
    void eraseBySize();
    void eraseAll();
    void compareFiles();
    void cancelCurrent();
    void clearLog();
    void showGuide();
    void showAbout();

    void onSessionStarted(DeviceSession::Op op);
    void onSessionFinished(DeviceSession::Op op, bool ok, bool cancelled);
    void onSessionMessage(DeviceSession::Severity severity, const QString &text);
    void onDeviceText(const QString &text);
    void onProgress(int percent);
    void onFirmwareVersion(const QString &version);
    void onSerialError(QSerialPort::SerialPortError error);
    void stepCompare();

private:
    bool connected() const;
    bool working() const;
    void updateUi();
    void setStatus();
    bool confirm(const QString &title, const QString &text, const QString &acceptText);
    QString askOpenFile(const QString &title, const QString &filter);
    QString askSaveFile(const QString &title, const QString &filter, const QString &suffix);
    void rememberDir(const QString &path);
    QString lastDir() const;
    void log(DeviceSession::Severity severity, const QString &text);
    void closePort();
    void finishCompare(bool cancelled);
    const Protocol::RomSize &selectedRomSize() const;
    void refreshRomSizes();
    const Protocol::SramSize &selectedSramSize() const;
    static QString humanSize(qint64 bytes);

    std::unique_ptr<Ui::MainWindow> ui;
    QSerialPort m_serial;
    DeviceSession m_session;
    QTimer m_portTimer;
    QLabel *m_statusLabel = nullptr;

    // file of the running read or write
    std::unique_ptr<QSaveFile> m_outFile;
    std::unique_ptr<QFile> m_inFile;
    std::unique_ptr<QBuffer> m_romBuffer;   // the prepared ROM image of a running write
    QByteArray m_romData;
    QString m_outPath;

    // running comparison
    std::unique_ptr<QFile> m_cmpA;
    std::unique_ptr<QFile> m_cmpB;
    std::unique_ptr<FileComparer> m_comparer;
    QTimer m_cmpTimer;

    DeviceSession::Op m_lastOp = DeviceSession::Op::None;
    DialogHooks m_hooks;
    HelpDialog *m_help = nullptr;
};

#endif // MAINWINDOW_H
