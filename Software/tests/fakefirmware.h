#ifndef FAKEFIRMWARE_H
#define FAKEFIRMWARE_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QIODevice>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>

// An in-memory stand-in for the md_rewriter firmware 1.0, seen from the PC side as a QIODevice.
// It parses the bytes the app writes exactly as the real firmware would (command header
// AA 55 AA BB, 1 KiB write blocks followed by a 7-byte command) and answers with the same
// strings, in randomly sized fragments to mimic a serial driver.
// The behaviour is derived from legacy_firmware/Core/Src/main.c; keep both in sync.
class FakeFirmware : public QIODevice
{
    Q_OBJECT
public:
    explicit FakeFirmware(QObject *parent = nullptr);

    // Content of the cartridge ROM / save RAM the fake serves and receives.
    QByteArray rom;
    QByteArray sram;

    // Fragmentation of the answers: 0 = deliver each answer in one piece, otherwise chunks
    // of 1..maxChunk bytes chosen by a generator seeded with `seed`.
    void setFragmentation(int maxChunk, quint32 seed);

    // Fault injection
    bool silent = false;           // never answer
    bool sendBanner = true;        // answer to Connect
    int deafAfterDetectMs = 0;     // firmware 1.0/1.1: bytes received this long after the detect answer are dropped (HAL_Delay then cmdclear)
    int droppedBytes = 0;          // bytes thrown away that way
    int ignoreConnects = 0;        // firmware 1.0 seen on the bench: the first Connect after the port was reopened gets no answer
    QString fwVersion;             // new firmware: sent as "FWVER:<x>" after the banner and enables dump code 5 (8M)
    int dropDumpBlock = -1;        // old-firmware bug: this dump block is not sent
    int stopAfterDumpBlocks = -1;  // stop answering after this many dump blocks
    bool eraseFails = false;       // answer to erase with a timeout line
    QString flashId = QStringLiteral("C2CB");                         // text after "FLASHID:"
    QString chipLine = QStringLiteral("MX29LV640EB MD FLASH CART");   // what firmware 1.0 prints next
    int failRomWriteAtBlock = -1;  // new-firmware behaviour: answer this ROM block with "WRITE TIMEOUT"
    int corruptRomOffset = -1;     // flip this ROM byte right after it is programmed (a bad write)
    // Deterministic brake for timing-sensitive tests: after this many reply chunks the device
    // goes quiet (what it would still say is kept) until resume() is called.
    int holdAfterReplies = -1;
    void resume();
    int replyCount() const { return m_replyCount; }

    // What the fake observed
    QList<QByteArray> commands;    // every command packet received, verbatim
    QStringList errors;            // protocol violations seen from the app
    int blocksWritten = 0;

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

    // Inject bytes as if the firmware had sent them (for parser tests).
    void inject(const QByteArray &bytes);
    // Drop the connection from the device side.
    void unplug();

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    void process();
    void reply(const QString &text);
    void replyRaw(const QByteArray &bytes);
    void pump();
    void dump(const QByteArray &image, bool isRom, int sizeCode);
    void handle(const QByteArray &cmd, const QByteArray &blockData);

    QElapsedTimer m_deafClock;
    bool m_deaf = false;
    QByteArray m_rx;        // bytes from the app not yet consumed
    QByteArray m_pending;   // answers not yet delivered
    QByteArray m_ready;     // answers the app can read now
    QTimer m_timer;
    int m_maxChunk = 0;
    QRandomGenerator m_rng;
    int m_dumpBlocksSent = 0;
    int m_replyCount = 0;
    QByteArray m_held;
};

#endif // FAKEFIRMWARE_H
