#ifndef ROMVERIFIER_H
#define ROMVERIFIER_H

#include <QIODevice>
#include <QPointer>

// A write-only sink that compares what the firmware dumps with the file that was written.
// The reference is read sequentially from its current position; bytes after the end of the
// reference (the rest of the dumped size) are ignored. Keeps the first difference.
class RomVerifier : public QIODevice
{
    Q_OBJECT
public:
    // `reference` must be open, readable and outlive the verifier.
    explicit RomVerifier(QIODevice *reference, QObject *parent = nullptr);

    qint64 compared() const { return m_compared; }
    qint64 mismatches() const { return m_mismatches; }
    bool ok() const { return m_mismatches == 0 && !m_referenceError; }
    qint64 firstOffset() const { return m_firstOffset; }
    quint8 expectedByte() const { return m_expected; }
    quint8 actualByte() const { return m_actual; }

    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 len) override;

private:
    QPointer<QIODevice> m_ref;
    qint64 m_pos = 0;
    qint64 m_compared = 0;
    qint64 m_mismatches = 0;
    qint64 m_firstOffset = -1;
    quint8 m_expected = 0;
    quint8 m_actual = 0;
    bool m_referenceError = false;
};

#endif // ROMVERIFIER_H
