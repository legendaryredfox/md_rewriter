#include "filecompare.h"

FileComparer::FileComparer(QIODevice *a, QIODevice *b, int maxListed)
    : m_a(a)
    , m_b(b)
    , m_maxListed(maxListed)
    , m_sizeA(a ? a->size() : 0)
    , m_sizeB(b ? b->size() : 0)
{
    if (!a || !b || !a->isReadable() || !b->isReadable()) {
        m_failed = true;
        m_finished = true;
    }
}

bool FileComparer::step(int chunk)
{
    if (m_finished)
        return true;
    const QByteArray x = m_a->read(chunk);
    const QByteArray y = m_b->read(chunk);
    if ((x.isEmpty() && !m_a->atEnd()) || (y.isEmpty() && !m_b->atEnd())) {
        m_failed = true;
        m_finished = true;
        return true;
    }
    const int common = qMin(x.size(), y.size());
    for (int i = 0; i < common; ++i) {
        if (x.at(i) != y.at(i)) {
            if (m_first.size() < m_maxListed)
                m_first.append(m_offset + static_cast<quint64>(i));
            ++m_differences;
        }
    }
    const int longer = qMax(x.size(), y.size());
    for (int i = common; i < longer; ++i) {
        if (m_first.size() < m_maxListed)
            m_first.append(m_offset + static_cast<quint64>(i));
        ++m_differences;
    }
    m_offset += static_cast<quint64>(longer);
    if (longer == 0)
        m_finished = true;
    return m_finished;
}

int FileComparer::percent() const
{
    const qint64 total = qMax(m_sizeA, m_sizeB);
    if (m_finished || total <= 0)
        return 100;
    return static_cast<int>(qMin<quint64>(99, m_offset * 100 / static_cast<quint64>(total)));
}
