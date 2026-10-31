#ifndef STREAMPARSER_H
#define STREAMPARSER_H

#include <QByteArray>
#include <QObject>
#include <QString>

// Splits the byte stream coming from the firmware into text lines and raw dump data.
//
// The firmware mixes two things on one serial stream: text lines ("...\r\n") and, after a
// "... DUMP START!!!" line, exactly N raw bytes. The operating system may deliver the stream
// in chunks of any size, so the parser never relies on how reads are split: it is driven only
// by the announced data length. Call expectDump() before sending a dump command.
class StreamParser : public QObject
{
    Q_OBJECT
public:
    explicit StreamParser(QObject *parent = nullptr);

    // Arm the parser: the next "DUMP START!!!" line is followed by `bytes` raw bytes.
    void expectDump(quint32 bytes);
    // Forget any armed dump and any partial line (new operation, reconnect).
    void reset();

    bool inData() const { return m_remaining > 0; }
    quint32 remainingData() const { return m_remaining; }

    // Feed bytes exactly as received.
    void feed(const QByteArray &bytes);

signals:
    // Raw text exactly as received (for the live log), emitted once per feed() call.
    void textReceived(const QString &text);
    // One complete text line without the line terminator.
    void lineReceived(const QString &line);
    // A piece of dump payload. Concatenation of all pieces is the dump.
    void dumpData(const QByteArray &data);
    // All announced bytes have arrived.
    void dumpComplete();
    // First line after the data was not the expected FINISH line: stream out of sync.
    void outOfSync(const QString &line);

private:
    void handleLine(const QString &line);

    QByteArray m_line;           // current partial text line
    bool m_armed = false;
    quint32 m_expected = 0;
    quint32 m_remaining = 0;
    bool m_checkFinish = false;  // next complete line must be a FINISH line
};

#endif // STREAMPARSER_H
