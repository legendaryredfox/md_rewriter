#include "devicesession.h"

#include <QCoreApplication>
#include <QIODevice>

using Protocol::LineKind;

namespace {
constexpr int kVersionGraceMs = 400;   // time to wait for "FWVER:" after the connect banner
}

DeviceSession::DeviceSession(QObject *parent)
    : QObject(parent)
    , m_parser(this)
{
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, &DeviceSession::onIdleTimeout);
    connect(&m_parser, &StreamParser::lineReceived, this, &DeviceSession::onLine);
    connect(&m_parser, &StreamParser::textReceived, this, &DeviceSession::onText);
    connect(&m_parser, &StreamParser::dumpData, this, &DeviceSession::onDumpData);
    connect(&m_parser, &StreamParser::dumpComplete, this, &DeviceSession::onDumpComplete);
    connect(&m_parser, &StreamParser::outOfSync, this, &DeviceSession::onOutOfSync);
}

void DeviceSession::setDevice(QIODevice *device)
{
    if (m_io)
        disconnect(m_io, nullptr, this, nullptr);
    m_io = device;
    m_parser.reset();
    if (m_io)
        connect(m_io, &QIODevice::readyRead, this, &DeviceSession::onReadyRead);
}

void DeviceSession::resetFirmwareInfo()
{
    if (m_fwVersion.isEmpty())
        return;
    m_fwVersion.clear();
    emit firmwareVersionChanged(m_fwVersion);
}

bool DeviceSession::canCancel() const
{
    return m_op == Op::ReadRom || m_op == Op::VerifyRom || m_op == Op::ReadSave || m_op == Op::WriteRom || m_op == Op::WriteSave;
}

// ---------------------------------------------------------------- starting operations

bool DeviceSession::begin(Op op, int idleMs)
{
    if (m_op != Op::None) {
        emit message(Severity::Error, tr("Another operation is still running."));
        return false;
    }
    if (!m_io || !m_io->isOpen()) {
        emit message(Severity::Error, tr("Device not connected."));
        return false;
    }
    m_parser.reset();
    m_op = op;
    m_reportOp = op;
    m_verify = false;
    m_verifier.reset();
    m_flashIdLine.clear();
    m_cancelled = false;
    m_finishing = false;
    m_lastPercent = -2;
    m_dumpComplete = false;
    m_outOfSync = false;
    m_writeFailed = false;
    m_out = nullptr;
    m_in = nullptr;
    m_received = 0;
    m_expected = 0;
    m_awaitingAck = false;
    m_clearSent = false;
    m_dots = 0;
    m_eraseSteps = 0;
    m_bannerSeen = false;
    m_idleMs = m_idleOverrideMs > 0 ? m_idleOverrideMs : idleMs;
    m_idle.start(m_idleMs);
    emit started(op);
    return true;
}

bool DeviceSession::send(const QByteArray &bytes)
{
    if (!m_io || !m_io->isOpen() || m_io->write(bytes) != bytes.size()) {
        fail(tr("Could not write to the device."));
        return false;
    }
    return true;
}

bool DeviceSession::connectDevice()
{
    if (!begin(Op::Connect, 3000))
        return false;
    if (!m_fwVersion.isEmpty()) {
        m_fwVersion.clear();
        emit firmwareVersionChanged(m_fwVersion);
    }
    setProgress(-1);
    return send(Protocol::buildCommand(Protocol::Connect));
}

bool DeviceSession::detect()
{
    if (!begin(Op::Detect, 5000))
        return false;
    setProgress(-1);
    return send(Protocol::buildCommand(Protocol::Detect));
}

bool DeviceSession::startRead(Op op, quint32 bytes, Protocol::Command cmd, quint8 code, QIODevice *out)
{
    if (!out || !out->isOpen() || !out->isWritable()) {
        emit message(Severity::Error, tr("The output file is not open for writing."));
        return false;
    }
    if (!begin(op, 10000))
        return false;
    m_out = out;
    m_expected = bytes;
    m_parser.expectDump(bytes);
    setProgress(0);
    QByteArray args;
    args.append(static_cast<char>(code));
    return send(Protocol::buildCommand(cmd, args));
}

bool DeviceSession::readRom(const Protocol::RomSize &size, QIODevice *out)
{
    if (size.bytes > 0x400000 && !canRead8M()) {
        emit message(Severity::Error, tr("Reading 8 MiB needs firmware 1.1 or later; this device has firmware 1.0."));
        return false;
    }
    return startRead(Op::ReadRom, size.bytes, Protocol::DumpRom, size.code, out);
}

bool DeviceSession::readSave(const Protocol::SramSize &size, QIODevice *out)
{
    return startRead(Op::ReadSave, size.bytes, Protocol::DumpSram, size.code, out);
}

bool DeviceSession::startWrite(Op op, QIODevice *in, Protocol::Command cmd, bool verify)
{
    if (!in || !in->isOpen() || !in->isReadable()) {
        emit message(Severity::Error, tr("The input file is not open for reading."));
        return false;
    }
    const qint64 size = in->size();
    const bool isRom = (op == Op::WriteRom);
    if (isRom ? !Protocol::isWritableRomSize(size) : !Protocol::isWritableSramSize(size)) {
        emit message(Severity::Error,
                     isRom ? tr("The file size (%1 bytes) is not supported. A ROM must have an even size between 512 bytes and 4 MiB.").arg(size)
                           : tr("The file size (%1 bytes) is not supported. Save files must be 8 KiB or 32 KiB.").arg(size));
        return false;
    }
    if (!begin(op, 10000))
        return false;
    m_in = in;
    m_verify = verify && op == Op::WriteRom && !in->isSequential();
    m_blockCmd = cmd;
    m_totalBlocks = Protocol::blockCount(size);
    m_nextBlock = 0;
    m_acked = 0;
    setProgress(0);
    sendNextBlock();
    return true;
}

bool DeviceSession::writeRom(QIODevice *in, bool verify)
{
    return startWrite(Op::WriteRom, in, Protocol::WriteRomBlock, verify);
}

bool DeviceSession::writeSave(QIODevice *in)
{
    return startWrite(Op::WriteSave, in, Protocol::WriteSramBlock);
}

bool DeviceSession::eraseAll()
{
    if (!begin(Op::EraseAll, 20000))
        return false;
    setProgress(-1);
    return send(Protocol::buildCommand(Protocol::EraseAll));
}

bool DeviceSession::eraseBySize(const Protocol::RomSize &size)
{
    if (!begin(Op::EraseBySize, 30000))
        return false;
    m_eraseSteps = size.eraseSectors;
    setProgress(size.eraseSectors > 0 ? 0 : -1);   // 8M is a full chip erase: duration unknown
    QByteArray args;
    args.append(static_cast<char>(size.code));
    return send(Protocol::buildCommand(Protocol::EraseBySize, args));
}

// ---------------------------------------------------------------- cancelling and failing

void DeviceSession::cancel()
{
    if (!canCancel() || m_cancelled)
        return;
    m_cancelled = true;
    emit message(Severity::Info, tr("Cancelling..."));
    if (m_op == Op::WriteRom || m_op == Op::WriteSave) {
        // Stop after the block in flight, or right now if none is.
        if (!m_awaitingAck)
            finishWrite();
    }
    // Reads cannot be interrupted in the firmware: keep consuming (and discarding) the
    // announced bytes so the stream stays in sync, then finish.
}

void DeviceSession::deviceLost(const QString &reason)
{
    if (m_op == Op::None)
        return;
    fail(reason);
}

void DeviceSession::fail(const QString &reason)
{
    emit message(Severity::Error, reason);
    finish(false);
}

void DeviceSession::finish(bool ok, const QString &okMessage)
{
    if (m_op == Op::None || m_finishing)
        return;
    m_finishing = true;
    m_idle.stop();
    const Op op = m_reportOp;
    const bool cancelled = m_cancelled;
    m_op = Op::None;
    m_reportOp = Op::None;
    m_verifier.reset();
    m_parser.reset();
    m_out = nullptr;
    m_in = nullptr;
    if (ok && !okMessage.isEmpty())
        emit message(Severity::Success, okMessage);
    if (ok)
        setProgress(100);
    emit finished(op, ok && !cancelled, cancelled);
}

void DeviceSession::onIdleTimeout()
{
    if (m_op == Op::None)
        return;
    if (m_cancelled) {
        if ((m_op == Op::WriteRom || m_op == Op::WriteSave) && m_clearSent)
            finishWrite();
        else
            finish(false);
        return;
    }
    // A dump whose data is all here but whose FINISH line got lost is still a good dump.
    if ((m_op == Op::ReadRom || m_op == Op::ReadSave) && m_dumpComplete && !m_outOfSync && !m_writeFailed) {
        emit message(Severity::Warning, tr("The device did not send the final confirmation, but all data was received."));
        finish(true, tr("Read complete: %1 bytes.").arg(m_received));
        return;
    }
    if (m_op == Op::VerifyRom && m_dumpComplete && !m_outOfSync) {
        emit message(Severity::Warning, tr("The device did not send the final confirmation, but all data was received."));
        finishVerify();
        return;
    }
    if ((m_op == Op::WriteRom || m_op == Op::WriteSave) && m_clearSent) {
        finishWrite();
        return;
    }
    if (m_op == Op::Connect && m_bannerSeen) {
        finish(true, tr("Device identified."));
        return;
    }
    switch (m_op) {
    case Op::VerifyRom:
        fail(tr("The device stopped answering after %1 of %2 bytes while verifying the write.").arg(m_received).arg(m_expected));
        break;
    case Op::ReadRom:
    case Op::ReadSave:
        fail(tr("The device stopped answering after %1 of %2 bytes.").arg(m_received).arg(m_expected));
        break;
    case Op::WriteRom:
    case Op::WriteSave:
        fail(tr("The device did not confirm block %1 of %2.").arg(m_acked + 1).arg(m_totalBlocks));
        break;
    case Op::Connect:
        emit message(Severity::Warning, tr("The port opened, but the device did not answer. Check that this is the right port and that the firmware is installed."));
        finish(false);
        break;
    default:
        fail(tr("The device did not answer."));
        break;
    }
}

// ---------------------------------------------------------------- receiving

void DeviceSession::onReadyRead()
{
    if (!m_io)
        return;
    const QByteArray bytes = m_io->readAll();
    if (bytes.isEmpty())
        return;
    if (m_op != Op::None)
        m_idle.start(m_idleMs);
    m_parser.feed(bytes);
}

void DeviceSession::onText(const QString &text)
{
    emit deviceText(text);
    if (m_op == Op::EraseBySize && m_eraseSteps > 0) {
        m_dots += Protocol::countDots(text);
        setProgress(qMin(99, m_dots * 100 / m_eraseSteps));
    }
}

void DeviceSession::onDumpData(const QByteArray &data)
{
    if (m_op != Op::ReadRom && m_op != Op::ReadSave && m_op != Op::VerifyRom)
        return;
    m_received += static_cast<quint32>(data.size());
    if (m_cancelled || m_writeFailed)
        return;
    if (m_out->write(data) != data.size()) {
        m_writeFailed = true;
        emit message(Severity::Error, tr("Could not write to the output file (disk full?)."));
        return;
    }
    if (m_expected > 0)
        setProgress(static_cast<int>(static_cast<qint64>(m_received) * 100 / m_expected));
}

void DeviceSession::onDumpComplete()
{
    m_dumpComplete = true;
}

void DeviceSession::onOutOfSync(const QString &line)
{
    if (m_op == Op::ReadRom || m_op == Op::ReadSave || m_op == Op::VerifyRom) {
        m_outOfSync = true;
        emit message(Severity::Warning, tr("Unexpected data from the device after the dump: \"%1\".").arg(line));
    }
}

void DeviceSession::onLine(const QString &line)
{
    if (Protocol::classifyLine(line) == LineKind::FirmwareVersion) {
        const QString version = line.trimmed().mid(6).trimmed();
        if (version != m_fwVersion) {
            m_fwVersion = version;
            emit firmwareVersionChanged(m_fwVersion);
        }
    }
    if (m_op == Op::None)
        return;
    const LineKind kind = Protocol::classifyLine(line);

    switch (m_op) {
    case Op::Connect:
        if (kind == LineKind::ConnectBanner) {
            // Firmware 1.1 sends its version right after the banner: give it a moment.
            m_bannerSeen = true;
            m_idle.start(kVersionGraceMs);
        } else if (kind == LineKind::FirmwareVersion) {
            finish(true, tr("Device identified."));
        }
        break;
    case Op::Detect:
        if (line.trimmed().startsWith(QLatin1String("FLASHID:")))
            m_flashIdLine = line.trimmed();
        if (kind == LineKind::DetectDone) {
            reportChip(line);
            finish(true);
        }
        break;
    case Op::VerifyRom:
        if (kind != LineKind::DumpRomFinished)
            break;
        if (m_cancelled)
            finish(false);
        else if (m_outOfSync || m_received != m_expected)
            fail(tr("The data read back is incomplete or out of sync (%1 of %2 bytes), so the write could not be verified.")
                     .arg(m_received).arg(m_expected));
        else
            finishVerify();
        break;
    case Op::ReadRom:
    case Op::ReadSave: {
        const LineKind want = (m_op == Op::ReadRom) ? LineKind::DumpRomFinished : LineKind::DumpSramFinished;
        if (kind != want)
            break;
        if (m_cancelled || m_writeFailed) {
            finish(false);
        } else if (m_outOfSync || m_received != m_expected) {
            fail(tr("The data from the device is incomplete or out of sync (%1 of %2 bytes). The file was not saved.")
                     .arg(m_received).arg(m_expected));
        } else {
            finish(true, tr("Read complete: %1 bytes.").arg(m_received));
        }
        break;
    }
    case Op::WriteRom:
        if (kind == LineKind::RomBlockAck)
            onWriteAck();
        else if (kind == LineKind::BuffersCleared && m_clearSent)
            finishWrite();
        else if (kind == LineKind::EraseFailed && line.contains(QLatin1String("WRITE")))
            failWrite(line);
        break;
    case Op::WriteSave:
        if (kind == LineKind::SramBlockAck)
            onWriteAck();
        else if (kind == LineKind::BuffersCleared && m_clearSent)
            finishWrite();
        break;
    case Op::EraseAll:
        if (kind == LineKind::EraseDone)
            finish(true, tr("Erase complete."));
        else if (kind == LineKind::EraseFailed)
            fail(tr("The cartridge did not finish erasing in time. It may be faulty or not a flash cartridge."));
        break;
    case Op::EraseBySize:
        if (kind == LineKind::EraseDone)
            finish(true, tr("Erase complete."));
        else if (kind == LineKind::EraseFailed)
            fail(tr("A sector did not finish erasing in time. The cartridge may be faulty or not a flash cartridge."));
        break;
    case Op::None:
        break;
    }
}

// ---------------------------------------------------------------- writing

void DeviceSession::sendNextBlock()
{
    QByteArray block = m_in->read(Protocol::kBlockSize);
    if (block.isEmpty()) {
        fail(tr("Could not read block %1 from the file.").arg(m_nextBlock + 1));
        return;
    }
    QByteArray packet = Protocol::padBlock(block);
    packet.append(Protocol::buildWriteBlockCommand(m_blockCmd, m_nextBlock));
    m_awaitingAck = true;
    ++m_nextBlock;
    send(packet);
}

void DeviceSession::onWriteAck()
{
    if (!m_awaitingAck) {
        emit message(Severity::Warning, tr("Ignoring an unexpected confirmation from the device."));
        return;
    }
    m_awaitingAck = false;
    ++m_acked;
    setProgress(static_cast<int>(static_cast<qint64>(m_acked) * 100 / m_totalBlocks));
    if (m_cancelled || m_acked >= m_totalBlocks)
        finishWrite();
    else
        sendNextBlock();
}

// New firmware only: "ADD:0x... WRITE TIMEOUT" means the chip did not finish programming a word.
// Firmware 1.0 never says this, it just stops answering and the idle timeout reports it.
void DeviceSession::failWrite(const QString &line)
{
    emit message(Severity::Error,
                 tr("The cartridge did not finish programming a word (\"%1\"). It was probably not erased first, "
                    "is not a flash cartridge, or the chip is not compatible. Erase it and try again.")
                     .arg(line.trimmed()));
    // Try to leave the device tidy: drop whatever it still holds.
    if (m_io && m_io->isOpen())
        m_io->write(Protocol::buildCommand(Protocol::ClearBuffers));
    finish(false);
}

// Sends the "clear buffers" command, then finishes when the device confirms (or goes quiet).
void DeviceSession::finishWrite()
{
    if (m_op == Op::None)
        return;
    if (!m_clearSent) {
        m_clearSent = true;
        if (!send(Protocol::buildCommand(Protocol::ClearBuffers)))
            return;
        // Wait for "BUFF IS CLEAR"; the idle timeout covers firmware that does not send it.
        m_idle.start(2000);
        return;
    }
    if (m_cancelled) {
        emit message(Severity::Warning, tr("Cancelled after %1 of %2 blocks. The cartridge now holds a partial image.")
                         .arg(m_acked).arg(m_totalBlocks));
        finish(false);
    } else if (m_op == Op::WriteRom && m_verify) {
        emit message(Severity::Success, tr("Game written: %1 blocks.").arg(m_totalBlocks));
        startVerify();
    } else {
        finish(true, m_op == Op::WriteRom ? tr("Game written: %1 blocks.").arg(m_totalBlocks)
                                          : tr("Save written: %1 blocks.").arg(m_totalBlocks));
    }
}

// Reads the cartridge back with the normal ROM dump command and compares it with the file.
void DeviceSession::startVerify()
{
    const Protocol::RomSize *size = nullptr;
    for (const auto &s : Protocol::romSizes()) {
        if (static_cast<qint64>(s.bytes) >= m_in->size()) {
            size = &s;
            break;
        }
    }
    if (!size || !m_in->seek(0)) {
        fail(tr("Could not start the verification of the write."));
        return;
    }
    emit message(Severity::Info, tr("Verifying: reading the cartridge back (%1)...").arg(size->label));
    m_op = Op::VerifyRom;
    m_verifier = std::make_unique<RomVerifier>(m_in);
    m_out = m_verifier.get();
    m_expected = size->bytes;
    m_received = 0;
    m_dumpComplete = false;
    m_outOfSync = false;
    m_writeFailed = false;
    m_parser.reset();
    m_parser.expectDump(size->bytes);
    m_lastPercent = -2;
    setProgress(0);
    m_idleMs = m_idleOverrideMs > 0 ? m_idleOverrideMs : 10000;
    m_idle.start(m_idleMs);
    QByteArray args;
    args.append(static_cast<char>(size->code));
    send(Protocol::buildCommand(Protocol::DumpRom, args));
}

void DeviceSession::finishVerify()
{
    if (!m_verifier || m_verifier->ok()) {
        finish(true, tr("Verified: the %1 bytes read back match the file.").arg(m_verifier ? m_verifier->compared() : 0));
        return;
    }
    fail(tr("Verification failed: at offset 0x%1 the cartridge has 0x%2 but the file has 0x%3 (%4 bytes differ in total). "
            "The cartridge may not have been erased first, may not be a flash cartridge, or the chip may be too slow for the firmware. "
            "Erase it and write again.")
             .arg(QString::number(m_verifier->firstOffset(), 16).toUpper())
             .arg(m_verifier->actualByte(), 2, 16, QLatin1Char('0'))
             .arg(m_verifier->expectedByte(), 2, 16, QLatin1Char('0'))
             .arg(m_verifier->mismatches()));
}

// Says in plain words what the chip is. Firmware 1.0 only names one chip and calls every other
// one "NO FIND NGP CARD"; that is not an error, so the user is told what it really means.
void DeviceSession::reportChip(const QString &firmwareLine)
{
    const bool firmwareKnows = firmwareLine.contains(QLatin1String("MD FLASH CART"));
    const ChipDb::Match m = ChipDb::identify(m_flashIdLine);
    if (m.chip) {
        const QString name = QString::fromLatin1(m.chip->name);
        if (firmwareKnows) {
            QString text = tr("Flash chip: %1 (ID %2). Supported.").arg(name, m.idText);
            if (!m.chip->confirmed)
                text += QLatin1Char(' ') + tr("This ID is not confirmed against a datasheet.");
            emit message(Severity::Success, text);
        } else {
            QString text = tr("Flash chip: %1 (ID %2). Firmware 1.0 does not list this chip, but it uses the same commands, "
                              "so reading is fine and writing should work (not tested on hardware).").arg(name, m.idText);
            if (!m.chip->confirmed)
                text += QLatin1Char(' ') + tr("This ID is not confirmed against a datasheet.");
            emit message(Severity::Info, text);
        }
        return;
    }
    if (firmwareKnows) {
        emit message(Severity::Success, tr("Flash chip recognised by the firmware (ID %1).").arg(m.idText));
        return;
    }
    if (m.parsed) {
        emit message(Severity::Warning,
                     tr("Chip not in the list (ID %1). It may be an original game cartridge (not flash), an empty slot, "
                        "or a flash chip this program does not know. Reading works with original cartridges. "
                        "Writing needs a flash chip that uses the standard AMD/JEDEC commands. "
                        "\"NO FIND NGP CARD\" only means the firmware did not recognise the chip.").arg(m.idText));
    }
}

void DeviceSession::setProgress(int percent)
{
    percent = percent < 0 ? -1 : qMin(percent, 100);
    if (percent == m_lastPercent)
        return;
    m_lastPercent = percent;
    emit progress(percent);
}
