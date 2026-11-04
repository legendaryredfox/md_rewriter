#ifndef CLIRUNNER_H
#define CLIRUNNER_H

#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QTextStream>

// Command line front end of md_rewriter. Same DeviceSession as the GUI, so the wire behaviour is
// identical. Made for scripts and AI agents: stable exit codes, `--json` result on stdout,
// progress and messages on stderr, and no destructive operation without `--yes`.
//
// Run with an already open device (tests, fake firmware) or let it open the serial port.
class CliRunner
{
public:
    enum Exit {
        Ok = 0,
        Failed = 1,        // the operation ran and failed (device error, verify mismatch, files differ)
        Usage = 2,         // bad arguments
        NoDevice = 3,      // no port found, cannot open, or no answer to connect
        NeedsConfirm = 4,  // destructive command without --yes
        FileError = 5      // cannot read or write a local file
    };

    CliRunner(QTextStream &out, QTextStream &err);

    // `args` without the program name. `device` is optional: when null, --port (or the only
    // port with the firmware's USB id) is opened.
    int run(const QStringList &args, QIODevice *device = nullptr);

    static QString usage();

private:
    QTextStream &m_out;
    QTextStream &m_err;
};

#endif // CLIRUNNER_H
