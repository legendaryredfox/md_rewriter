#ifndef HELPDIALOG_H
#define HELPDIALOG_H

#include <QDialog>
#include <QLocale>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QTextBrowser;

// The built-in user guide: an HTML document per language, embedded as a resource.
class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(QWidget *parent = nullptr, const QLocale &locale = QLocale());

    // ":/help/help_pt_BR.html" for a Brazilian Portuguese locale, English when there is no match.
    static QString resourceFor(const QLocale &locale);
    // Language codes that have a guide, e.g. "en", "pt_BR".
    static QStringList availableLanguages();
    // Anchors (h2/h3 ids) of a guide, in document order.
    static QStringList anchorsOf(const QString &html);

    void showSection(const QString &anchor);
    QString plainText() const;

private slots:
    void onAnchorClicked(const QUrl &url);
    void onFind();
    void onFindNext();

private:
    void loadGuide(const QLocale &locale);
    QString styleSheet() const;

    QTextBrowser *m_view = nullptr;
    QListWidget *m_toc = nullptr;
    QLineEdit *m_find = nullptr;
};

#endif // HELPDIALOG_H
