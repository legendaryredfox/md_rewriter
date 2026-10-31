#include "helpdialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QRegularExpression>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kAnchorRole = Qt::UserRole;
const char kPrefix[] = ":/help/help_";
}

HelpDialog::HelpDialog(QWidget *parent, const QLocale &locale)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("helpDialog"));
    setWindowTitle(tr("MD Rewriter - User guide"));
    resize(900, 640);

    m_toc = new QListWidget(this);
    m_toc->setObjectName(QStringLiteral("helpContents"));
    m_toc->setAccessibleName(tr("Contents"));
    m_toc->setMinimumWidth(190);
    m_toc->setTextElideMode(Qt::ElideRight);
    m_toc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_find = new QLineEdit(this);
    m_find->setObjectName(QStringLiteral("helpFind"));
    m_find->setPlaceholderText(tr("Find in the guide..."));
    m_find->setClearButtonEnabled(true);
    m_find->setAccessibleName(tr("Find in the guide"));

    m_view = new QTextBrowser(this);
    m_view->setObjectName(QStringLiteral("helpView"));
    m_view->setOpenLinks(false);
    m_view->setAccessibleName(tr("User guide"));

    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_find);
    leftLayout->addWidget(m_toc, 1);

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(left);
    split->addWidget(m_view);
    split->setStretchFactor(1, 1);
    split->setSizes({270, 630});

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(split);

    connect(m_toc, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            m_view->scrollToAnchor(item->data(kAnchorRole).toString());
    });
    connect(m_view, &QTextBrowser::anchorClicked, this, &HelpDialog::onAnchorClicked);
    connect(m_find, &QLineEdit::textChanged, this, &HelpDialog::onFind);
    connect(m_find, &QLineEdit::returnPressed, this, &HelpDialog::onFindNext);

    loadGuide(locale);
}

QStringList HelpDialog::availableLanguages()
{
    QStringList langs;
    const QStringList files = QDir(QStringLiteral(":/help")).entryList({QStringLiteral("help_*.html")});
    for (const QString &f : files)
        langs << f.mid(5, f.size() - 5 - 5);   // strip "help_" and ".html"
    langs.sort();
    return langs;
}

QString HelpDialog::resourceFor(const QLocale &locale)
{
    const QStringList available = availableLanguages();
    const QStringList wanted = locale.uiLanguages();
    for (const QString &raw : wanted) {
        QString code = raw;
        code.replace(QLatin1Char('-'), QLatin1Char('_'));
        if (available.contains(code))
            return QString::fromLatin1(kPrefix) + code + QStringLiteral(".html");
        const QString base = code.section(QLatin1Char('_'), 0, 0);
        if (available.contains(base))
            return QString::fromLatin1(kPrefix) + base + QStringLiteral(".html");
        for (const QString &a : available)
            if (a.startsWith(base + QLatin1Char('_')))
                return QString::fromLatin1(kPrefix) + a + QStringLiteral(".html");
    }
    return QString::fromLatin1(kPrefix) + QStringLiteral("en.html");
}

QStringList HelpDialog::anchorsOf(const QString &html)
{
    static const QRegularExpression re(QStringLiteral("<h[23] id=\"([^\"]+)\""));
    QStringList out;
    auto it = re.globalMatch(html);
    while (it.hasNext())
        out << it.next().captured(1);
    return out;
}

QString HelpDialog::styleSheet() const
{
    const QPalette p = palette();
    const QString text = p.color(QPalette::Text).name();
    const QString link = p.color(QPalette::Link).name();
    const QString panel = p.color(QPalette::AlternateBase).name();
    const QString head = p.color(QPalette::Button).name();
    return QStringLiteral(
        "body { color: %1; }"
        "a { color: %2; }"
        "h1 { margin-top: 0; }"
        "h2 { margin-top: 22px; }"
        "h3 { margin-top: 14px; }"
        "code { background-color: %3; font-family: monospace; }"
        "th { background-color: %4; }"
        "td.callout { background-color: %3; }")
        .arg(text, link, panel, head);
}

void HelpDialog::loadGuide(const QLocale &locale)
{
    QFile f(resourceFor(locale));
    QString html;
    if (f.open(QIODevice::ReadOnly))
        html = QString::fromUtf8(f.readAll());
    m_view->document()->setDefaultStyleSheet(styleSheet());
    m_view->setHtml(html);

    // Contents: every h2 and h3 with an id.
    static const QRegularExpression re(QStringLiteral("<(h[23]) id=\"([^\"]+)\">(.*?)</h[23]>"),
                                       QRegularExpression::DotMatchesEverythingOption);
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        QString title = m.captured(3);
        title.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
        auto *item = new QListWidgetItem((m.captured(1) == QLatin1String("h3") ? QStringLiteral("    ") : QString()) + title);
        item->setData(kAnchorRole, m.captured(2));
        item->setToolTip(title);
        if (m.captured(1) == QLatin1String("h2")) {
            QFont fnt = item->font();
            fnt.setBold(true);
            item->setFont(fnt);
        }
        m_toc->addItem(item);
    }
}

void HelpDialog::showSection(const QString &anchor)
{
    for (int i = 0; i < m_toc->count(); ++i) {
        if (m_toc->item(i)->data(kAnchorRole).toString() == anchor) {
            m_toc->setCurrentRow(i);
            return;
        }
    }
    m_view->scrollToAnchor(anchor);
}

QString HelpDialog::plainText() const
{
    return m_view->toPlainText();
}

void HelpDialog::onAnchorClicked(const QUrl &url)
{
    if (url.scheme().isEmpty() && !url.fragment().isEmpty()) {
        showSection(url.fragment());
    } else if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) {
        QDesktopServices::openUrl(url);
    }
}

void HelpDialog::onFind()
{
    m_view->moveCursor(QTextCursor::Start);
    if (!m_find->text().isEmpty())
        m_view->find(m_find->text());
}

void HelpDialog::onFindNext()
{
    if (m_find->text().isEmpty())
        return;
    if (!m_view->find(m_find->text())) {
        m_view->moveCursor(QTextCursor::Start);
        m_view->find(m_find->text());
    }
}
