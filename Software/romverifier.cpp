#include "romverifier.h"

RomVerifier::RomVerifier(QIODevice *reference, QObject *parent)
    : QIODevice(parent)
    , m_ref(reference)
{
    open(QIODevice::WriteOnly);
}

qint64 RomVerifier::writeData(const char *data, qint64 len)
{
    if (!m_ref) {
        m_referenceError = true;
        return len;
    }
    qint64 done = 0;
    while (done < len && !m_ref->atEnd()) {
        const qint64 want = qMin<qint64>(len - done, 64 * 1024);
        const QByteArray expected = m_ref->read(want);
        if (expected.isEmpty()) {
            m_referenceError = true;
            break;
        }
        for (int i = 0; i < expected.size(); ++i) {
            const quint8 e = static_cast<quint8>(expected.at(i));
            const quint8 a = static_cast<quint8>(data[done + i]);
            if (e != a) {
                if (m_mismatches == 0) {
                    m_firstOffset = m_pos + i;
                    m_expected = e;
                    m_actual = a;
                }
                ++m_mismatches;
            }
        }
        m_pos += expected.size();
        m_compared += expected.size();
        done += expected.size();
    }
    // Whatever is left lies beyond the reference file: accept and ignore it.
    m_pos += len - done;
    return len;
}
