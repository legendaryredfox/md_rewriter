#ifndef FILECOMPARE_H
#define FILECOMPARE_H

#include <QIODevice>
#include <QVector>

// Compares two streams chunk by chunk, so the caller can interleave it with the event loop
// (call step() from a zero-timer) and never holds both files in memory.
// Bytes past the end of the shorter stream count as differences.
class FileComparer
{
public:
    FileComparer(QIODevice *a, QIODevice *b, int maxListed = 20);

    // Process up to `chunk` bytes of each stream. Returns true when finished (or on error).
    bool step(int chunk = 1 << 16);

    bool finished() const { return m_finished; }
    bool failed() const { return m_failed; }
    quint64 differenceCount() const { return m_differences; }
    // Offsets of the first differences, at most maxListed of them.
    const QVector<quint64> &firstDifferences() const { return m_first; }
    // 0..100
    int percent() const;
    qint64 sizeA() const { return m_sizeA; }
    qint64 sizeB() const { return m_sizeB; }

private:
    QIODevice *m_a;
    QIODevice *m_b;
    int m_maxListed;
    qint64 m_sizeA;
    qint64 m_sizeB;
    quint64 m_offset = 0;
    quint64 m_differences = 0;
    QVector<quint64> m_first;
    bool m_finished = false;
    bool m_failed = false;
};

#endif // FILECOMPARE_H
