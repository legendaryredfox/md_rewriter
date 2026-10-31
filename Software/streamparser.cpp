#include "streamparser.h"

#include "protocol.h"

StreamParser::StreamParser(QObject *parent)
    : QObject(parent)
{
}

void StreamParser::expectDump(quint32 bytes)
{
    m_armed = true;
    m_expected = bytes;
    m_remaining = 0;
    m_checkFinish = false;
}

void StreamParser::reset()
{
    m_line.clear();
    m_armed = false;
    m_expected = 0;
    m_remaining = 0;
    m_checkFinish = false;
}

void StreamParser::feed(const QByteArray &bytes)
{
    QString text;
    int i = 0;
    const int n = bytes.size();
    while (i < n) {
        if (m_remaining > 0) {
            const int take = static_cast<int>(qMin<qint64>(m_remaining, n - i));
            emit dumpData(bytes.mid(i, take));
            m_remaining -= static_cast<quint32>(take);
            i += take;
            if (m_remaining == 0) {
                m_checkFinish = true;
                emit dumpComplete();
            }
            continue;
        }

        const char c = bytes.at(i++);
        // Text arrives as 7-bit ASCII from the firmware; keep any other byte visible.
        text.append(QChar::fromLatin1(c));
        if (c == '\n') {
            const QString line = QString::fromLatin1(m_line).trimmed();
            m_line.clear();
            // Deliver the text seen so far before the line event, so a log shows text in order.
            if (!text.isEmpty()) {
                emit textReceived(text);
                text.clear();
            }
            handleLine(line);
        } else if (c != '\r') {
            // A text line is never this long; if it is, we are fed garbage. Do not grow forever.
            if (m_line.size() < 4096)
                m_line.append(c);
        }
    }
    if (!text.isEmpty())
        emit textReceived(text);
}

void StreamParser::handleLine(const QString &line)
{
    if (m_checkFinish && !line.isEmpty()) {
        m_checkFinish = false;
        const Protocol::LineKind kind = Protocol::classifyLine(line);
        if (kind != Protocol::LineKind::DumpRomFinished && kind != Protocol::LineKind::DumpSramFinished)
            emit outOfSync(line);
    }

    emit lineReceived(line);

    if (m_armed && Protocol::classifyLine(line) == Protocol::LineKind::DumpStart) {
        m_armed = false;
        m_remaining = m_expected;
        if (m_remaining == 0) {
            m_checkFinish = true;
            emit dumpComplete();
        }
    }
}
