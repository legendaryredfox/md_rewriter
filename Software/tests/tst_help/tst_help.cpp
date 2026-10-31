// The built-in user guide: structure, language selection, and drift against the real UI strings.
#include <QApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QSet>
#include <QStyleFactory>
#include <QTextBrowser>
#include <QtTest>

#include "helpdialog.h"

namespace {

QString readResource(const QString &lang)
{
    QFile f(QStringLiteral(":/help/help_%1.html").arg(lang));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// source -> translation of the MainWindow/DeviceSession/Protocol contexts of the pt_BR file
QMap<QString, QString> loadTs()
{
    QMap<QString, QString> m;
    QFile f(QStringLiteral(SRCDIR "/translations/MDRewriter_pt_BR.ts"));
    if (!f.open(QIODevice::ReadOnly))
        return m;
    QDomDocument d;
    d.setContent(&f);
    const QDomNodeList msgs = d.elementsByTagName(QStringLiteral("message"));
    for (int i = 0; i < msgs.size(); ++i) {
        const QDomElement e = msgs.at(i).toElement();
        m.insert(e.firstChildElement(QStringLiteral("source")).text(),
                 e.firstChildElement(QStringLiteral("translation")).text());
    }
    return m;
}

QStringList codePhrases(const QString &html)
{
    static const QRegularExpression re(QStringLiteral("<code>(.*?)</code>"));
    QStringList out;
    auto it = re.globalMatch(html);
    while (it.hasNext())
        out << it.next().captured(1);
    return out;
}

QString unescape(QString s)
{
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&")).replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return s;
}

// "Cannot open ...: permission denied..." -> ["Cannot open", "permission denied"]
// The guide writes N, M, X, Y where the program has %1, %2: those split the phrase too.
QStringList fragments(const QString &phrase)
{
    static const QRegularExpression placeholder(QStringLiteral("\\b[NMXY]\\b|\\.\\.\\."));
    QStringList out;
    for (QString part : unescape(phrase).split(placeholder, Qt::SkipEmptyParts)) {
        part = part.trimmed();
        while (!part.isEmpty() && (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(':'))))
            part.chop(1);
        part = part.trimmed();
        if (part.size() >= 8)
            out << part;
    }
    return out;
}

} // namespace

class TstHelp : public QObject
{
    Q_OBJECT
private slots:
    void languagesAvailable()
    {
        const QStringList l = HelpDialog::availableLanguages();
        QVERIFY(l.contains("en"));
        QVERIFY(l.contains("pt_BR"));
    }

    void localeSelection_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<QString>("expected");
        QTest::newRow("pt_BR") << "pt_BR" << ":/help/help_pt_BR.html";
        QTest::newRow("pt_PT uses the Portuguese guide") << "pt_PT" << ":/help/help_pt_BR.html";
        QTest::newRow("pt") << "pt" << ":/help/help_pt_BR.html";
        QTest::newRow("en_US") << "en_US" << ":/help/help_en.html";
        QTest::newRow("ru falls back to English") << "ru_RU" << ":/help/help_en.html";
        QTest::newRow("zh falls back to English") << "zh_CN" << ":/help/help_en.html";
        QTest::newRow("C") << "C" << ":/help/help_en.html";
    }
    void localeSelection()
    {
        QFETCH(QString, locale);
        QFETCH(QString, expected);
        QCOMPARE(HelpDialog::resourceFor(QLocale(locale)), expected);
    }

    void everyLanguageHasTheSameStructure()
    {
        const QStringList langs = HelpDialog::availableLanguages();
        const QStringList reference = HelpDialog::anchorsOf(readResource("en"));
        QVERIFY(reference.size() >= 20);
        for (const QString &lang : langs) {
            const QString html = readResource(lang);
            QVERIFY2(!html.isEmpty(), qPrintable(lang));
            QCOMPARE(HelpDialog::anchorsOf(html), reference);
        }
    }

    void anchorsAreUniqueAndLinksResolve_data()
    {
        QTest::addColumn<QString>("lang");
        for (const QString &l : HelpDialog::availableLanguages())
            QTest::newRow(qPrintable(l)) << l;
    }
    void anchorsAreUniqueAndLinksResolve()
    {
        QFETCH(QString, lang);
        const QString html = readResource(lang);
        QSet<QString> ids;
        static const QRegularExpression idRe(QStringLiteral(" id=\"([^\"]+)\""));
        auto it = idRe.globalMatch(html);
        while (it.hasNext()) {
            const QString id = it.next().captured(1);
            QVERIFY2(!ids.contains(id), qPrintable("duplicate id " + id));
            ids.insert(id);
        }
        static const QRegularExpression hrefRe(QStringLiteral("href=\"#([^\"]+)\""));
        auto h = hrefRe.globalMatch(html);
        int internal = 0;
        while (h.hasNext()) {
            const QString target = h.next().captured(1);
            QVERIFY2(ids.contains(target), qPrintable("broken link #" + target));
            ++internal;
        }
        QVERIFY(internal >= 10);
        QVERIFY(html.startsWith(QStringLiteral("<h1 id=\"top\">")));
        QVERIFY(!html.contains(QStringLiteral("TODO")));
        QVERIFY(!html.contains(QStringLiteral("lorem"), Qt::CaseInsensitive));
        // balanced block tags (a stray tag would swallow the rest of the guide in the viewer)
        for (const char *tag : {"table", "tr", "td", "th", "ul", "ol", "li", "p", "h2", "h3", "code", "b"}) {
            const int opened = html.count(QRegularExpression(QStringLiteral("<%1[ >]").arg(QLatin1String(tag))));
            const int closed = html.count(QStringLiteral("</%1>").arg(QLatin1String(tag)));
            QVERIFY2(opened == closed, qPrintable(QStringLiteral("[%1] <%2> opened %3, closed %4").arg(lang, QLatin1String(tag)).arg(opened).arg(closed)));
        }
    }

    void dialogLoadsEachLanguage_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<QString>("phrase");
        QTest::newRow("en") << "en_US" << "Quick start";
        QTest::newRow("pt_BR") << "pt_BR" << "Início rápido";
    }
    void dialogLoadsEachLanguage()
    {
        QFETCH(QString, locale);
        QFETCH(QString, phrase);
        HelpDialog d(nullptr, QLocale(locale));
        QVERIFY(d.plainText().contains(phrase));
        auto *toc = d.findChild<QListWidget *>("helpContents");
        QVERIFY(toc);
        QCOMPARE(toc->count(), HelpDialog::anchorsOf(readResource(locale.startsWith("pt") ? "pt_BR" : "en")).size());
        // navigation by section does not crash and selects the entry
        d.showSection("troubleshooting");
        QVERIFY(toc->currentRow() >= 0);
    }

    void findBoxFindsText()
    {
        HelpDialog d(nullptr, QLocale("en_US"));
        d.show();
        auto *find = d.findChild<QLineEdit *>("helpFind");
        auto *view = d.findChild<QTextBrowser *>("helpView");
        QVERIFY(find && view);
        QTest::keyClicks(find, "ModemManager");
        QVERIFY(view->textCursor().hasSelection());
        QCOMPARE(view->textCursor().selectedText(), QString("ModemManager"));
        const int first = view->textCursor().position();
        QTest::keyClick(find, Qt::Key_Return);        // next match
        QVERIFY(view->textCursor().position() != first);
    }

    // ---- drift guards: what the guide quotes must exist in the program ----
    void quotedMessagesExistInTheProgram()
    {
        const QMap<QString, QString> ts = loadTs();
        QVERIFY2(ts.size() > 100, "translation file not found");
        QSet<QString> sources, translations;
        for (auto it = ts.begin(); it != ts.end(); ++it) {
            sources.insert(it.key());
            translations.insert(it.value());
        }
        const struct { const char *lang; const QSet<QString> *pool; } cases[] = {{"en", &sources}, {"pt_BR", &translations}};
        int checked = 0;
        for (const auto &c : cases) {
            const QStringList phrases = codePhrases(readResource(c.lang));
            for (const QString &phrase : phrases) {
                // only the program's own messages: skip file names, commands, firmware text
                if (!phrase.contains(QLatin1Char(' ')) || phrase.contains(QStringLiteral("sudo")) || phrase.contains(QStringLiteral("udevadm"))
                    || phrase.startsWith(QStringLiteral("FLASHID")) || phrase.startsWith(QStringLiteral("MX29")) || phrase.contains(QStringLiteral("NGP"))
                    || phrase.startsWith(QStringLiteral("PUSH")) || phrase.startsWith(QStringLiteral("USE TIME")) || phrase.startsWith(QStringLiteral("MD_REWRITER"))
                    || phrase.contains(QStringLiteral("/")) || phrase.startsWith(QStringLiteral("Alt")))
                    continue;
                for (const QString &frag : fragments(phrase)) {
                    bool found = false;
                    for (const QString &s : *c.pool)
                        if (s.contains(frag)) { found = true; break; }
                    QVERIFY2(found, qPrintable(QStringLiteral("[%1] the guide quotes a message the program does not have: \"%2\"").arg(c.lang, frag)));
                    ++checked;
                }
            }
        }
        QVERIFY2(checked >= 30, qPrintable(QString::number(checked)));
    }

    void shortcutsMatchTheRealMnemonics_data()
    {
        QTest::addColumn<QString>("lang");
        QTest::newRow("en") << "en";
        QTest::newRow("pt_BR") << "pt_BR";
    }
    void shortcutsMatchTheRealMnemonics()
    {
        QFETCH(QString, lang);
        const QMap<QString, QString> ts = loadTs();
        // mnemonic letter -> button text, from the source (en) or the translation (pt_BR)
        QMap<QChar, QString> keys;
        const QStringList buttons = {"&Refresh", "&Connect", "&Detect cartridge", "Co&mpare files…", "Read R&OM…", "&Write ROM…",
                                     "Read &save…", "Write sa&ve…", "Erase by si&ze…", "Erase &all…", "C&lear log", "Ca&ncel"};
        for (const QString &src : buttons) {
            QVERIFY2(ts.contains(src), qPrintable("button not in .ts: " + src));
            const QString text = lang == "en" ? src : ts.value(src);
            const int i = text.indexOf('&');
            QVERIFY2(i >= 0, qPrintable("no mnemonic in " + text));
            keys.insert(text.at(i + 1).toUpper(), text);
        }
        QCOMPARE(keys.size(), 12);                         // no two buttons share a key
        const QString html = readResource(lang);
        static const QRegularExpression re(QStringLiteral("<td>Alt\\+(\\w)</td>"));
        QSet<QChar> documented;
        auto it = re.globalMatch(html);
        while (it.hasNext())
            documented.insert(it.next().captured(1).at(0));
        QCOMPARE(documented.size(), 12);
        for (QChar k : documented)
            QVERIFY2(keys.contains(k), qPrintable(QStringLiteral("[%1] Alt+%2 is documented but is not a button key").arg(lang).arg(k)));
    }

    void screenshots()
    {
        const QString dir = qEnvironmentVariable("MD_SCREENSHOT_DIR");
        if (dir.isEmpty())
            QSKIP("set MD_SCREENSHOT_DIR to save screenshots");
        QDir().mkpath(dir);
        for (const QString &loc : {QStringLiteral("en_US"), QStringLiteral("pt_BR")}) {
            HelpDialog d(nullptr, QLocale(loc));
            d.show();
            d.showSection("recipe-flash");
            QTest::qWait(100);
            d.grab().save(QStringLiteral("%1/guide-%2.png").arg(dir, loc));
        }
        // dark palette
        qApp->setStyle(QStyleFactory::create("Fusion"));
        QPalette p;
        p.setColor(QPalette::Window, QColor(45, 45, 48)); p.setColor(QPalette::WindowText, Qt::white);
        p.setColor(QPalette::Base, QColor(30, 30, 30)); p.setColor(QPalette::AlternateBase, QColor(60, 60, 64));
        p.setColor(QPalette::Text, Qt::white); p.setColor(QPalette::Button, QColor(70, 70, 74));
        p.setColor(QPalette::ButtonText, Qt::white); p.setColor(QPalette::Link, QColor(110, 170, 255));
        qApp->setPalette(p);
        HelpDialog d(nullptr, QLocale("en_US"));
        d.show();
        d.showSection("messages");
        QTest::qWait(100);
        d.grab().save(QStringLiteral("%1/guide-dark.png").arg(dir));
    }
};

QTEST_MAIN(TstHelp)
#include "tst_help.moc"
