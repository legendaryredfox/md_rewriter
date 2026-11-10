#ifndef DEVICESESSION_H
#define DEVICESESSION_H

#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <memory>

#include "chipdb.h"
#include "protocol.h"
#include "romverifier.h"
#include "streamparser.h"

// Runs one operation at a time against the firmware over any QIODevice (a QSerialPort in the
// application, an in-memory fake in the tests). Owns no GUI. Knows the protocol: it builds the
// commands, feeds received bytes to a StreamParser and decides when an operation is finished.
//
// The device and the file/stream arguments are owned by the caller and must outlive the operation.
class DeviceSession : public QObject
{
    Q_OBJECT
public:
    enum class Op { None, Connect, Detect, ReadRom, WriteRom, VerifyRom, ReadSave, WriteSave, EraseAll, EraseBySize };
    enum class Severity { Info, Success, Warning, Error };
    Q_ENUM(Op)
    Q_ENUM(Severity)

    explicit DeviceSession(QObject *parent = nullptr);

    void setDevice(QIODevice *device);
    QIODevice *device() const { return m_io; }

    Op currentOp() const { return m_op; }
    bool busy() const { return m_op != Op::None; }
    // Reads and writes can be stopped from the host side. Erase cannot: the firmware is busy.
    bool canCancel() const;

    // Each returns false, after emitting an Error message, when it could not start.
    bool connectDevice();
    bool detect();
    bool readRom(const Protocol::RomSize &size, QIODevice *out);
    // ROM of a cartridge with the SSF2 bank-switch mapper: `pages` x 512 KiB (1..64), firmware 1.1 or later.
    bool readRomSsf2(int pages, QIODevice *out);
    // With `verify`, a successful write is followed by a read-back (the normal ROM dump command)
    // that is compared with `in`, which must be seekable. Same operation for the caller: one
    // finished() signal, ok only if the write and the comparison both succeeded.
    bool writeRom(QIODevice *in, bool verify = false);
    bool readSave(const Protocol::SramSize &size, QIODevice *out);
    bool writeSave(QIODevice *in);
    bool eraseAll();
    bool eraseBySize(const Protocol::RomSize &size);

    // Version text the firmware announced after the connect banner ("1.1.0"); empty for firmware 1.0.
    QString firmwareVersion() const { return m_fwVersion; }
    // Firmware that announces a version can read 8 MiB (see Protocol::extendedRomSize).
    bool canRead8M() const { return !m_fwVersion.isEmpty(); }
    // True after a detect found a chip the firmware does not list while firmware 1.0 is connected.
    // Firmware 1.0 was written for one chip and does not wait for the chip to finish programming
    // a word, so writes to other chips need the read-back check.
    bool unlistedChipOnOldFirmware() const { return m_unlistedChip && m_fwVersion.isEmpty(); }

    // Forget what the firmware announced (the port was closed).
    void resetFirmwareInfo();

    void cancel();
    // The port vanished or reported an error: abort whatever is running.
    void deviceLost(const QString &reason);

    // Idle time without any byte from the device after which an operation is given up.
    void setIdleTimeoutMs(int ms) { m_idleOverrideMs = ms; }

signals:
    void message(DeviceSession::Severity severity, const QString &text);
    void deviceText(const QString &text);   // raw firmware text, for the log
    void progress(int percent);             // 0..100, or -1 when the duration is unknown
    void started(DeviceSession::Op op);
    void finished(DeviceSession::Op op, bool ok, bool cancelled);
    void firmwareVersionChanged(const QString &version);   // empty when the connection is reset

private slots:
    void onReadyRead();
    void onLine(const QString &line);
    void onText(const QString &text);
    void onDumpData(const QByteArray &data);
    void onDumpComplete();
    void onOutOfSync(const QString &line);
    void onIdleTimeout();

private:
    bool begin(Op op, int idleMs);
    bool send(const QByteArray &bytes);
    void finish(bool ok, const QString &okMessage = QString());
    void fail(const QString &reason);
    bool startRead(Op op, quint32 bytes, Protocol::Command cmd, quint8 code, QIODevice *out);
    bool startWrite(Op op, QIODevice *in, Protocol::Command cmd, bool verify = false);
    void startVerify();
    void finishVerify();
    void sendNextBlock();
    void onWriteAck();
    void finishWrite();
    void failWrite(const QString &line);
    void setProgress(int percent);

    QPointer<QIODevice> m_io;
    StreamParser m_parser;
    QTimer m_idle;
    int m_idleOverrideMs = -1;
    int m_idleMs = 10000;

    Op m_op = Op::None;
    Op m_reportOp = Op::None;     // what finished() reports: a verify belongs to its write
    bool m_cancelled = false;
    bool m_finishing = false;
    int m_lastPercent = -2;

    // reads
    QIODevice *m_out = nullptr;
    quint32 m_expected = 0;
    quint32 m_received = 0;
    bool m_dumpComplete = false;
    bool m_outOfSync = false;
    bool m_writeFailed = false;

    // writes
    QIODevice *m_in = nullptr;
    Protocol::Command m_blockCmd = Protocol::WriteRomBlock;
    quint32 m_totalBlocks = 0;
    quint32 m_nextBlock = 0;
    quint32 m_acked = 0;
    bool m_awaitingAck = false;
    bool m_clearSent = false;
    bool m_verify = false;
    std::unique_ptr<RomVerifier> m_verifier;

    // connect
    QString m_fwVersion;
    bool m_bannerSeen = false;   // the banner came; FWVER may still follow within a short grace period

    // detect
    QString m_flashIdLine;
    QString m_cfiLine;             // "CFI:SIZE=8192KB CMDSET=2", sent by firmware 1.1 for a chip outside its table
    bool m_unlistedChip = false;   // kept across operations, cleared when connecting
    void reportChip(const QString &firmwareLine);
    void reportCfi();

    // erase by size
    int m_eraseSteps = 0;
    int m_dots = 0;
};

#endif // DEVICESESSION_H
