#include "clirunner.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>

#include "chipdb.h"
#include "devicesession.h"
#include "filecompare.h"
#include "protocol.h"
#include "romimage.h"

namespace {

constexpr int kBaudRate = 1000000;
constexpr quint16 kStmVendorId = 0x0483;
constexpr quint16 kStmProductId = 0x5740;

struct Options {
    QString command;
    QStringList positional;
    QString port;
    QString out;
    QString size;
    QString mapper;         // read-rom: "ssf2" reads a bank-switched cartridge page by page
    bool json = false;
    bool yes = false;
    bool verify = true;     // write-rom reads the cartridge back unless --no-verify
    bool quiet = false;
    bool dryRun = false;
    int timeoutMs = -1;
    QString error;
};

Options parse(const QStringList &args)
{
    Options o;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args.at(i);
        auto value = [&](QString *dst) {
            if (i + 1 >= args.size()) {
                o.error = QStringLiteral("%1 needs a value").arg(a);
                return;
            }
            *dst = args.at(++i);
        };
        if (a == QLatin1String("--port") || a == QLatin1String("-p")) value(&o.port);
        else if (a == QLatin1String("--out") || a == QLatin1String("-o")) value(&o.out);
        else if (a == QLatin1String("--size") || a == QLatin1String("-s")) value(&o.size);
        else if (a == QLatin1String("--mapper")) value(&o.mapper);
        else if (a == QLatin1String("--timeout")) {
            QString t;
            value(&t);
            if (o.error.isEmpty()) {   // value() sets the error when the argument is missing
                bool ok = false;
                const int sec = t.toInt(&ok);
                if (!ok || sec <= 0) o.error = QStringLiteral("--timeout needs seconds > 0");
                else o.timeoutMs = sec * 1000;
            }
        }
        else if (a == QLatin1String("--json")) o.json = true;
        else if (a == QLatin1String("--yes") || a == QLatin1String("-y")) o.yes = true;
        else if (a == QLatin1String("--no-verify")) o.verify = false;
        else if (a == QLatin1String("--verify")) o.verify = true;
        else if (a == QLatin1String("--quiet") || a == QLatin1String("-q")) o.quiet = true;
        else if (a == QLatin1String("--dry-run")) o.dryRun = true;
        else if (a.startsWith(QLatin1Char('-')) && a.size() > 1) o.error = QStringLiteral("unknown option %1").arg(a);
        else if (o.command.isEmpty()) o.command = a;
        else o.positional << a;
        if (!o.error.isEmpty())
            break;
    }
    return o;
}

const Protocol::RomSize *findRomSize(const QString &label, bool allow8M)
{
    for (const Protocol::RomSize &s : Protocol::romSizes())
        if (s.label.compare(label, Qt::CaseInsensitive) == 0)
            return &s;
    if (allow8M && Protocol::extendedRomSize().label.compare(label, Qt::CaseInsensitive) == 0)
        return &Protocol::extendedRomSize();
    return nullptr;
}

const Protocol::SramSize *findSramSize(const QString &label)
{
    for (const Protocol::SramSize &s : Protocol::sramSizes())
        if (s.label.compare(label, Qt::CaseInsensitive) == 0)
            return &s;
    return nullptr;
}

QString sev(DeviceSession::Severity s)
{
    switch (s) {
    case DeviceSession::Severity::Success: return QStringLiteral("ok");
    case DeviceSession::Severity::Warning: return QStringLiteral("warning");
    case DeviceSession::Severity::Error: return QStringLiteral("error");
    default: return QStringLiteral("info");
    }
}

} // namespace

CliRunner::CliRunner(QTextStream &out, QTextStream &err) : m_out(out), m_err(err) {}

QString CliRunner::usage()
{
    return QStringLiteral(
        "usage: mdrewriter-cli [options] <command> [args]\n"
        "\n"
        "Without device:\n"
        "  ports                     list serial ports (the firmware shows as 0483:5740)\n"
        "  chips                     flash chips the program can name from the FLASHID line\n"
        "  rominfo <file>            what would be written: size, SMD conversion, header warnings\n"
        "  compare <a> <b>           byte compare of two files\n"
        "\n"
        "With device (--port, or the only port with the firmware USB id):\n"
        "  info                      connect, print firmware version\n"
        "  detect                    read the flash chip ID (read only)\n"
        "  read-rom --size 512K|1M|2M|4M|8M --out <file>      dump the ROM (8M needs firmware 1.1+)\n"
        "  read-rom --mapper ssf2 --size 512K..32M --out <file>   dump an SSF2 bank-switched cartridge (firmware 1.1+, size in 512K steps)\n"
        "  read-save --size 8K|32K --out <file>               dump the save RAM\n"
        "  verify <file> [--size S]  read the cartridge back and compare with <file> (read only)\n"
        "  write-rom <file> --yes    check chip, then write (erase first: erase <size> --yes); reads back unless --no-verify\n"
        "  write-save <file> --yes   overwrite the save RAM\n"
        "  erase all|512K|1M|2M|4M|8M --yes   (8M = whole chip)\n"
        "\n"
        "Options:\n"
        "  --port, -p <name>   serial port (ttyACM0, /dev/ttyACM0, COM3)\n"
        "  --json              one JSON object on stdout; messages stay on stderr\n"
        "  --yes, -y           allow commands that erase or overwrite\n"
        "  --dry-run           check files and arguments, do not touch the device\n"
        "  --timeout <s>       give up when the device is silent for this long\n"
        "  --quiet, -q         no progress on stderr\n"
        "\n"
        "Exit codes: 0 ok, 1 operation failed, 2 usage, 3 no device, 4 needs --yes, 5 file error.\n"
        "Existing output files are never overwritten by read commands unless the path is new\n"
        "or --yes is given.\n");
}

int CliRunner::run(const QStringList &args, QIODevice *injected)
{
    const Options o = parse(args);
    QJsonObject result;

    auto finish = [&](int code, const QString &errorText = QString()) {
        if (!errorText.isEmpty()) {
            m_err << "error: " << errorText << "\n";
            result.insert(QStringLiteral("error"), errorText);
        }
        result.insert(QStringLiteral("exit"), code);
        result.insert(QStringLiteral("ok"), code == Ok);
        if (o.json)
            m_out << QJsonDocument(result).toJson(QJsonDocument::Compact) << "\n";
        m_out.flush();
        m_err.flush();
        return code;
    };

    if (!o.error.isEmpty()) {
        m_err << "error: " << o.error << "\n\n" << usage();
        return Usage;
    }
    if (o.command.isEmpty() || o.command == QLatin1String("help")) {
        m_out << usage();
        return o.command.isEmpty() ? Usage : Ok;
    }
    result.insert(QStringLiteral("command"), o.command);

    // ---------------------------------------------------------------- no device
    if (o.command == QLatin1String("ports")) {
        QJsonArray list;
        for (const QSerialPortInfo &i : QSerialPortInfo::availablePorts()) {
            QJsonObject p;
            p.insert(QStringLiteral("name"), i.portName());
            p.insert(QStringLiteral("location"), i.systemLocation());
            p.insert(QStringLiteral("description"), i.description());
            p.insert(QStringLiteral("vid"), i.hasVendorIdentifier() ? QString::number(i.vendorIdentifier(), 16).rightJustified(4, '0') : QString());
            p.insert(QStringLiteral("pid"), i.hasProductIdentifier() ? QString::number(i.productIdentifier(), 16).rightJustified(4, '0') : QString());
            p.insert(QStringLiteral("mdrewriter"), i.vendorIdentifier() == kStmVendorId && i.productIdentifier() == kStmProductId);
            list.append(p);
            if (!o.json)
                m_out << i.portName() << "\t" << i.description() << "\t"
                      << (p.value("vid").toString() + ":" + p.value("pid").toString())
                      << (p.value("mdrewriter").toBool() ? "\t<- md_rewriter" : "") << "\n";
        }
        result.insert(QStringLiteral("ports"), list);
        return finish(Ok);
    }
    if (o.command == QLatin1String("chips")) {
        int n = 0;
        const ChipDb::Chip *c = ChipDb::chips(&n);
        QJsonArray list;
        for (int i = 0; i < n; ++i) {
            QJsonObject j;
            j.insert(QStringLiteral("id"), QStringLiteral("%1%2").arg(c[i].manufacturer, 2, 16, QLatin1Char('0')).arg(c[i].device, 2, 16, QLatin1Char('0')).toUpper());
            j.insert(QStringLiteral("name"), QString::fromLatin1(c[i].name));
            j.insert(QStringLiteral("confirmed"), c[i].confirmed);
            const char *cs = c[i].commandSet == ChipDb::CommandSet::Amd555 ? "amd-555h"
                             : (c[i].commandSet == ChipDb::CommandSet::Amd5555 ? "amd-5555h"
                             : (c[i].commandSet == ChipDb::CommandSet::Amd555Block50 ? "amd-555h-block50" : "intel"));
            j.insert(QStringLiteral("commandSet"), QString::fromLatin1(cs));
            list.append(j);
            if (!o.json)
                m_out << j.value("id").toString() << "  " << j.value("name").toString() << (c[i].confirmed ? "" : "  (not confirmed)")
                      << (c[i].commandSet == ChipDb::CommandSet::Amd555 ? "" : "  [needs firmware 1.1]") << "\n";
        }
        result.insert(QStringLiteral("chips"), list);
        return finish(Ok);
    }
    if (o.command == QLatin1String("rominfo") || o.command == QLatin1String("compare")) {
        const int need = o.command == QLatin1String("compare") ? 2 : 1;
        if (o.positional.size() != need)
            return finish(Usage, QStringLiteral("%1 needs %2 file argument(s)").arg(o.command).arg(need));
        if (need == 1) {
            QFile f(o.positional.at(0));
            if (!f.open(QIODevice::ReadOnly))
                return finish(FileError, QStringLiteral("cannot read %1: %2").arg(f.fileName(), f.errorString()));
            const RomImage::Result r = RomImage::prepare(f.readAll());
            result.insert(QStringLiteral("writable"), r.ok);
            result.insert(QStringLiteral("bytes"), r.data.size());
            result.insert(QStringLiteral("infos"), QJsonArray::fromStringList(r.infos));
            result.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(r.warnings));
            if (!o.json) {
                m_out << (r.ok ? "writable, " : "NOT writable: ") << (r.ok ? QString::number(r.data.size()) + " bytes" : r.error) << "\n";
                for (const QString &s : r.infos) m_out << "info: " << s << "\n";
                for (const QString &s : r.warnings) m_out << "warning: " << s << "\n";
            }
            return finish(r.ok ? Ok : Failed, r.ok ? QString() : r.error);
        }
        QFile a(o.positional.at(0)), b(o.positional.at(1));
        if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly))
            return finish(FileError, QStringLiteral("cannot read %1").arg(a.isOpen() ? b.fileName() : a.fileName()));
        FileComparer cmp(&a, &b);
        while (!cmp.step()) {}
        result.insert(QStringLiteral("differences"), static_cast<double>(cmp.differenceCount()));
        result.insert(QStringLiteral("sizeA"), static_cast<double>(cmp.sizeA()));
        result.insert(QStringLiteral("sizeB"), static_cast<double>(cmp.sizeB()));
        QJsonArray first;
        for (quint64 off : cmp.firstDifferences()) first.append(static_cast<double>(off));
        result.insert(QStringLiteral("firstDifferences"), first);
        if (!o.json)
            m_out << (cmp.differenceCount() ? QStringLiteral("%1 byte(s) differ").arg(cmp.differenceCount()) : QStringLiteral("identical")) << "\n";
        return finish(cmp.failed() ? FileError : (cmp.differenceCount() ? Failed : Ok));
    }

    // ---------------------------------------------------------------- with device
    static const QStringList deviceCommands = {
        QStringLiteral("info"), QStringLiteral("detect"), QStringLiteral("read-rom"), QStringLiteral("read-save"),
        QStringLiteral("verify"), QStringLiteral("write-rom"), QStringLiteral("write-save"), QStringLiteral("erase")
    };
    if (!deviceCommands.contains(o.command))
        return finish(Usage, QStringLiteral("unknown command %1").arg(o.command));

    const bool destructive = o.command == QLatin1String("write-rom") || o.command == QLatin1String("write-save")
                             || o.command == QLatin1String("erase");
    if (destructive && !o.yes && !o.dryRun)
        return finish(NeedsConfirm, QStringLiteral("%1 changes the cartridge: repeat with --yes (use --dry-run to only check)").arg(o.command));

    // Arguments and files are checked before the port is touched.
    const Protocol::RomSize *romSize = nullptr;
    const Protocol::SramSize *sramSize = nullptr;
    QByteArray image;                 // ROM to write / to compare with
    QByteArray saveImage;
    const bool needsRead = o.command == QLatin1String("read-rom") || o.command == QLatin1String("read-save");
    int mapperPages = 0;              // read-rom --mapper ssf2: number of 512 KiB pages
    if (!o.mapper.isEmpty() && o.command != QLatin1String("read-rom"))
        return finish(Usage, QStringLiteral("--mapper is only for read-rom"));
    if (o.command == QLatin1String("read-rom") && !o.mapper.isEmpty()) {
        if (o.mapper.compare(QLatin1String("ssf2"), Qt::CaseInsensitive) != 0)
            return finish(Usage, QStringLiteral("unknown --mapper %1 (only ssf2)").arg(o.mapper));
        static const QRegularExpression re(QStringLiteral("^(\\d+)([KkMm])$"));
        const QRegularExpressionMatch m = re.match(o.size);
        const qint64 bytes = m.hasMatch() ? m.captured(1).toLongLong() * (m.captured(2).toUpper() == QLatin1String("M") ? 1048576 : 1024) : 0;
        if (bytes <= 0 || bytes % 0x80000 != 0 || bytes / 0x80000 > 64)
            return finish(Usage, QStringLiteral("--mapper ssf2 needs --size in 512K steps, 512K to 32M (for example 5M)"));
        mapperPages = static_cast<int>(bytes / 0x80000);
    } else if (o.command == QLatin1String("read-rom")) {
        romSize = findRomSize(o.size, true);
        if (!romSize) return finish(Usage, QStringLiteral("read-rom needs --size 512K, 1M, 2M, 4M or 8M"));
    }
    if (o.command == QLatin1String("read-save")) {
        sramSize = findSramSize(o.size);
        if (!sramSize) return finish(Usage, QStringLiteral("read-save needs --size 8K or 32K"));
    }
    if (needsRead) {
        if (o.out.isEmpty()) return finish(Usage, QStringLiteral("%1 needs --out <file>").arg(o.command));
        if (QFile::exists(o.out) && !o.yes)
            return finish(NeedsConfirm, QStringLiteral("%1 exists and would be overwritten: use another name or --yes").arg(o.out));
    }
    if (o.command == QLatin1String("write-rom") || o.command == QLatin1String("verify")) {
        if (o.positional.size() != 1) return finish(Usage, QStringLiteral("%1 needs one ROM file").arg(o.command));
        QFile f(o.positional.at(0));
        if (!f.open(QIODevice::ReadOnly)) return finish(FileError, QStringLiteral("cannot read %1: %2").arg(f.fileName(), f.errorString()));
        const RomImage::Result r = RomImage::prepare(f.readAll());
        if (!r.ok) return finish(FileError, r.error);
        image = r.data;
        for (const QString &w : r.warnings) m_err << "warning: " << w << "\n";
        result.insert(QStringLiteral("bytes"), image.size());
        if (o.command == QLatin1String("verify")) {
            romSize = o.size.isEmpty() ? nullptr : findRomSize(o.size, true);
            if (!o.size.isEmpty() && !romSize) return finish(Usage, QStringLiteral("unknown --size %1").arg(o.size));
            if (!romSize) {
                const qint64 cover = RomImage::coveringReadSize(image.size());
                for (const Protocol::RomSize &s : Protocol::romSizes()) if (s.bytes == cover) romSize = &s;
                if (!romSize && cover == Protocol::extendedRomSize().bytes) romSize = &Protocol::extendedRomSize();
            }
        }
    }
    if (o.command == QLatin1String("write-save")) {
        if (o.positional.size() != 1) return finish(Usage, QStringLiteral("write-save needs one save file"));
        QFile f(o.positional.at(0));
        if (!f.open(QIODevice::ReadOnly)) return finish(FileError, QStringLiteral("cannot read %1: %2").arg(f.fileName(), f.errorString()));
        saveImage = f.readAll();
        if (!Protocol::isWritableSramSize(saveImage.size()))
            return finish(FileError, QStringLiteral("save file must be 8 KiB or 32 KiB (it has %1 bytes)").arg(saveImage.size()));
    }
    const Protocol::RomSize *eraseSize = nullptr;
    bool eraseAll = false;
    if (o.command == QLatin1String("erase")) {
        if (o.positional.size() != 1) return finish(Usage, QStringLiteral("erase needs all, 512K, 1M, 2M, 4M or 8M"));
        if (o.positional.at(0).compare(QLatin1String("all"), Qt::CaseInsensitive) == 0) eraseAll = true;
        else {
            eraseSize = findRomSize(o.positional.at(0), true);
            if (!eraseSize) return finish(Usage, QStringLiteral("erase needs all, 512K, 1M, 2M, 4M or 8M"));
        }
    }
    if (o.dryRun) {
        result.insert(QStringLiteral("dryRun"), true);
        if (!o.json) m_out << "dry run: arguments and files are fine, device not touched\n";
        return finish(Ok);
    }

    // Open the port.
    QSerialPort serial;
    QIODevice *dev = injected;
    if (!dev) {
        QString location = o.port;
        if (location.isEmpty()) {
            QStringList found;
            for (const QSerialPortInfo &i : QSerialPortInfo::availablePorts())
                if (i.vendorIdentifier() == kStmVendorId && i.productIdentifier() == kStmProductId)
                    found << i.systemLocation();
            const QString extra = qEnvironmentVariable("MD_REWRITER_EXTRA_PORT");
            if (found.isEmpty() && !extra.isEmpty()) found << extra;
            if (found.size() != 1)
                return finish(NoDevice, found.isEmpty() ? QStringLiteral("no md_rewriter port found: plug it in or pass --port (see: ports)")
                                                        : QStringLiteral("more than one md_rewriter port: pass --port"));
            location = found.first();
        }
        serial.setPortName(location);
        serial.setBaudRate(kBaudRate);
        if (!serial.open(QIODevice::ReadWrite))
            return finish(NoDevice, Protocol::describeOpenError(QSerialPortInfo(serial).portName(), serial.error(), serial.errorString()));
        dev = &serial;
        result.insert(QStringLiteral("port"), location);
    }

    DeviceSession session;
    session.setDevice(dev);
    if (o.timeoutMs > 0) session.setIdleTimeoutMs(o.timeoutMs);
    QJsonArray messages;
    QString deviceText;
    int lastPercent = -3;
    QObject::connect(&session, &DeviceSession::message, [&](DeviceSession::Severity s, const QString &t) {
        QJsonObject m;
        m.insert(QStringLiteral("severity"), sev(s));
        m.insert(QStringLiteral("text"), t);
        messages.append(m);
        m_err << sev(s) << ": " << t << "\n";
        m_err.flush();
    });
    QObject::connect(&session, &DeviceSession::deviceText, [&](const QString &t) { deviceText += t; });
    QObject::connect(&session, &DeviceSession::progress, [&](int p) {
        if (o.quiet || p < 0 || p == lastPercent || (p % 10 != 0 && p != 100)) return;
        lastPercent = p;
        m_err << "progress: " << p << "%\n";
        m_err.flush();
    });

    // Runs one operation to the end of the event loop. Returns false if it could not start.
    bool opOk = false, opCancelled = false, started = false;
    auto runOp = [&](auto start) {
        QEventLoop loop;
        bool done = false;
        QObject::connect(&session, &DeviceSession::finished, &loop, [&](DeviceSession::Op, bool ok, bool c) {
            opOk = ok; opCancelled = c; done = true; loop.quit();
        });
        started = start();
        if (!started) return false;
        if (!done) loop.exec();
        return true;
    };

    // Connect first, always. Firmware 1.0 can ignore a connect that arrives right after the port was
    // reopened (seen on the bench: `detect` and then `read-rom` as two processes in a row), so one retry.
    runOp([&] { return session.connectDevice(); });
    if (started && !opOk && !opCancelled) {
        const QString retryText = QStringLiteral("No answer to the first connect, trying once more.");
        messages.append(QJsonObject{{QStringLiteral("severity"), QStringLiteral("warning")}, {QStringLiteral("text"), retryText}});
        m_err << "warning: " << retryText << "\n";
        m_err.flush();
        runOp([&] { return session.connectDevice(); });
    }
    if (!started || !opOk) {
        result.insert(QStringLiteral("messages"), messages);
        return finish(NoDevice, QStringLiteral("no answer to connect: wrong port, cable or firmware"));
    }
    result.insert(QStringLiteral("firmware"), session.firmwareVersion().isEmpty() ? QStringLiteral("1.0") : session.firmwareVersion());
    if (!o.json && o.command == QLatin1String("info"))
        m_out << "firmware " << result.value("firmware").toString() << "\n";

    int code = Ok;
    QString err;
    auto check = [&](bool okRun, const QString &what) {
        if (!okRun || !opOk) { code = Failed; err = what; }
    };

    if (o.command == QLatin1String("info")) {
        // nothing more
    } else if (o.command == QLatin1String("detect")) {
        check(runOp([&] { return session.detect(); }), QStringLiteral("detect failed"));
        QString idLine;
        for (const QString &l : deviceText.split('\n')) if (l.startsWith(QLatin1String("FLASHID:"))) idLine = l.trimmed();
        const ChipDb::Match m = ChipDb::identify(idLine);
        result.insert(QStringLiteral("flashId"), m.idText);
        result.insert(QStringLiteral("chip"), m.chip ? QString::fromLatin1(m.chip->name) : QString());
        if (idLine.isEmpty()) {
            code = Failed;
            err = QStringLiteral("no FLASHID line from the device");
        } else {
            result.insert(QStringLiteral("firmwareKnowsChip"), !deviceText.contains(QLatin1String("NO FIND NGP CARD")));
        }
        if (!o.json) m_out << "flash id: " << (m.parsed ? m.idText : QStringLiteral("none")) << "  chip: " << (m.chip ? QString::fromLatin1(m.chip->name) : QStringLiteral("unknown")) << "\n";
    } else if (o.command == QLatin1String("read-rom") || o.command == QLatin1String("read-save")) {
        if (mapperPages && !session.canRead8M()) {
            code = Failed; err = QStringLiteral("--mapper ssf2 needs firmware 1.1 or newer (this one is 1.0)");
        } else if (romSize && romSize->bytes == Protocol::extendedRomSize().bytes && !session.canRead8M()) {
            code = Failed; err = QStringLiteral("8M needs firmware 1.1 or newer (this one is 1.0)");
        } else {
            QSaveFile f(o.out);
            if (!f.open(QIODevice::WriteOnly)) return finish(FileError, QStringLiteral("cannot write %1: %2").arg(o.out, f.errorString()));
            const bool rom = romSize != nullptr || mapperPages > 0;
            check(runOp([&] {
                      if (mapperPages) return session.readRomSsf2(mapperPages, &f);
                      return romSize ? session.readRom(*romSize, &f) : session.readSave(*sramSize, &f);
                  }), QStringLiteral("read failed"));
            if (code == Ok && !f.commit()) return finish(FileError, QStringLiteral("cannot finish %1").arg(o.out));
            if (code != Ok) f.cancelWriting();
            result.insert(QStringLiteral("out"), o.out);
            if (mapperPages) result.insert(QStringLiteral("mapper"), QStringLiteral("ssf2"));
            result.insert(QStringLiteral("bytes"), static_cast<double>(mapperPages ? static_cast<qint64>(mapperPages) * 0x80000 : (rom ? romSize->bytes : sramSize->bytes)));
        }
    } else if (o.command == QLatin1String("verify")) {
        if (!romSize) { code = Usage; err = QStringLiteral("file larger than 8 MiB"); }
        else {
            QByteArray back;
            QBuffer buf(&back);
            buf.open(QIODevice::WriteOnly);
            check(runOp([&] { return session.readRom(*romSize, &buf); }), QStringLiteral("read failed"));
            if (code == Ok) {
                const bool same = back.left(image.size()) == image;
                result.insert(QStringLiteral("identical"), same);
                if (!same) { code = Failed; err = QStringLiteral("cartridge content differs from %1").arg(o.positional.at(0)); }
            }
        }
    } else if (o.command == QLatin1String("write-rom")) {
        check(runOp([&] { return session.detect(); }), QStringLiteral("cartridge pre-write detection failed"));
        QString idLine;
        for (const QString &l : deviceText.split('\n'))
            if (l.startsWith(QLatin1String("FLASHID:"))) idLine = l.trimmed();
        const ChipDb::Match match = ChipDb::identify(idLine);
        const quint32 capacity = ChipDb::capacityBytes(match);
        const bool firmwareRecognized = deviceText.contains(QLatin1String(" MD FLASH CART"));
        result.insert(QStringLiteral("flashId"), match.idText);
        result.insert(QStringLiteral("chip"), match.chip ? QString::fromLatin1(match.chip->name) : QString());
        if (code == Ok && image.size() > 0x400000 && !session.canRead8M()) {
            code = Failed;
            err = QStringLiteral("this firmware cannot write ROMs larger than 4 MiB");
        } else if (code == Ok && !match.parsed) {
            code = Failed;
            err = QStringLiteral("the device did not identify a flash chip; ROM was not written");
        } else if (code == Ok && capacity && static_cast<quint64>(image.size()) > capacity) {
            code = Failed;
            err = QStringLiteral("ROM is larger than the detected flash chip capacity (%1 bytes)").arg(capacity);
        } else if (code == Ok && match.chip && match.chip->commandSet != ChipDb::CommandSet::Amd555
                   && session.firmwareVersion().isEmpty()) {
            code = Failed;
            err = QStringLiteral("the detected flash chip needs newer firmware commands");
        }
        if (code == Ok) {
            result.insert(QStringLiteral("preflight"), match.chip && capacity && firmwareRecognized
                              ? QStringLiteral("compatible") : QStringLiteral("uncertain"));
            QBuffer buf(&image);
            buf.open(QIODevice::ReadOnly);
            check(runOp([&] { return session.writeRom(&buf, o.verify); }), QStringLiteral("write failed"));
        }
        result.insert(QStringLiteral("verified"), o.verify && code == Ok);
    } else if (o.command == QLatin1String("write-save")) {
        QBuffer buf(&saveImage);
        buf.open(QIODevice::ReadOnly);
        check(runOp([&] { return session.writeSave(&buf); }), QStringLiteral("save write failed"));
    } else if (o.command == QLatin1String("erase")) {
        check(runOp([&] { return eraseAll ? session.eraseAll() : session.eraseBySize(*eraseSize); }), QStringLiteral("erase failed"));
    }
    if (opCancelled) { code = Failed; err = QStringLiteral("cancelled"); }

    result.insert(QStringLiteral("messages"), messages);
    return finish(code, err);
}
