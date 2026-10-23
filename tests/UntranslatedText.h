#pragma once

// Collects the text a widget tree shows - labels, buttons, group titles, list,
// combo, tab, tree and table entries, headers, placeholders, tooltips, spin
// box affixes and menu actions - that still holds English words. Used with a
// non-English catalogue installed: what it returns is text the user sees in
// English although the interface is set to another language.

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocument>
#include <QTreeWidget>
#include <QWidget>

namespace openvegas::tests {

// Words that stay as they are in every language: product and format names,
// units and abbreviations.
inline const QSet<QString>& untranslatedWordsAllowed()
{
    static const QSet<QString> words = {
        QStringLiteral("VEGAS"), QStringLiteral("Effects"), QStringLiteral("OpenVegas"),
        QStringLiteral("FXhome"), QStringLiteral("HitFilm"), QStringLiteral("Imerge"),
        QStringLiteral("hfpl"), QStringLiteral("HFPL"), QStringLiteral("hfpr"), QStringLiteral("vegfx"),
        QStringLiteral("GPU"), QStringLiteral("CPU"), QStringLiteral("RAM"), QStringLiteral("MSAA"),
        QStringLiteral("RMS"), QStringLiteral("OpenGL"), QStringLiteral("HDR"), QStringLiteral("SDR"),
        QStringLiteral("LUT"), QStringLiteral("URL"), QStringLiteral("UI"), QStringLiteral("ID"),
        QStringLiteral("MB"), QStringLiteral("GB"), QStringLiteral("KB"), QStringLiteral("px"),
        QStringLiteral("fps"), QStringLiteral("Hz"), QStringLiteral("kHz"), QStringLiteral("dB"),
        QStringLiteral("ms"), QStringLiteral("Qt"), QStringLiteral("VLC"), QStringLiteral("libVLC"),
        QStringLiteral("FFmpeg"), QStringLiteral("Windows"), QStringLiteral("macOS"),
        QStringLiteral("Linux"), QStringLiteral("NVIDIA"), QStringLiteral("AMD"), QStringLiteral("Intel"),
        QStringLiteral("RGB"), QStringLiteral("RGBA"), QStringLiteral("HSV"), QStringLiteral("YUV"),
        QStringLiteral("XYZ"), QStringLiteral("UV"), QStringLiteral("2D"), QStringLiteral("3D"),
        QStringLiteral("HD"), QStringLiteral("UHD"), QStringLiteral("SD"), QStringLiteral("DCI"),
        QStringLiteral("PAL"), QStringLiteral("NTSC"), QStringLiteral("MP4"), QStringLiteral("H264"),
        QStringLiteral("HEVC"), QStringLiteral("AVC"), QStringLiteral("AAC"), QStringLiteral("PNG"),
        QStringLiteral("JPEG"), QStringLiteral("EXR"), QStringLiteral("TIFF"), QStringLiteral("WAV"),
        QStringLiteral("OBJ"), QStringLiteral("FBX"), QStringLiteral("glTF"), QStringLiteral("Alembic"),
        QStringLiteral("Ctrl"), QStringLiteral("Shift"), QStringLiteral("Alt"), QStringLiteral("Del"),
        QStringLiteral("Esc"), QStringLiteral("Tab"), QStringLiteral("Space"), QStringLiteral("Home"),
        QStringLiteral("End"), QStringLiteral("PgUp"), QStringLiteral("PgDown"), QStringLiteral("Enter"),
        QStringLiteral("Backspace"), QStringLiteral("Ins"), QStringLiteral("Up"), QStringLiteral("Down"),
        QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Return"),
        QStringLiteral("x"), QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
        QStringLiteral("OK"), QStringLiteral("DPI"), QStringLiteral("QuickTime"),
        QStringLiteral("CSAA"), QStringLiteral("xQ"), QStringLiteral("OS"),
        // A timecode's fields (HH:MM:SS:FF).
        QStringLiteral("HH"), QStringLiteral("MM"), QStringLiteral("SS"), QStringLiteral("FF"),
        // The name of the plugins' folder in the installation.
        QStringLiteral("Plugins"),
    };
    return words;
}

inline QString plainText(const QString& text)
{
    if (!Qt::mightBeRichText(text)) return text;
    QTextDocument document;
    document.setHtml(text);
    return document.toPlainText();
}

inline bool showsEnglish(const QString& shown)
{
    static const QRegularExpression word(QStringLiteral("[A-Za-z][A-Za-z0-9]*"));
    // Paths, file patterns, key sequences on their own and numbers carry no
    // words to translate.
    QString text = plainText(shown);
    // File patterns, paths up to the next colon, links, colours and UUIDs.
    text.remove(QRegularExpression(QStringLiteral(
        "\\(\\*\\.[^)]*\\)|\\*\\.[A-Za-z0-9]+|[A-Za-z]:[\\\\/][^:]*|https?://\\S+"
        "|#[0-9A-Fa-f]{6,8}\\b"
        "|\\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\b")));
    auto it = word.globalMatch(text);
    while (it.hasNext()) {
        const QString w = it.next().captured();
        if (w.size() < 2) continue;
        if (untranslatedWordsAllowed().contains(w)) continue;
        return true;
    }
    return false;
}

inline void collectActions(const QList<QAction*>& actions, const QString& where, QStringList& found)
{
    for (QAction* action : actions) {
        if (action->isSeparator()) continue;
        if (showsEnglish(action->text())) found << where + QStringLiteral(" action: ") + action->text();
        if (QMenu* menu = action->menu()) collectActions(menu->actions(), where, found);
    }
}

// What `root` and its children show in English, each "Class objectName: text".
// Widgets named in `skipped` show the user's data - device, file and
// composition names, format names - not the interface's.
inline QStringList englishShownIn(QWidget* root, const QStringList& skipped = {})
{
    QStringList found;
    QList<QWidget*> widgets = root->findChildren<QWidget*>();
    widgets.prepend(root);
    for (QWidget* widget : std::as_const(widgets)) {
        if (skipped.contains(widget->objectName())) continue;
        const QString where = QString::fromLatin1(widget->metaObject()->className())
            + QLatin1Char(' ') + widget->objectName();
        const auto check = [&](const QString& what, const QString& text) {
            if (!text.isEmpty() && showsEnglish(text)) found << where + QLatin1Char(' ') + what + QStringLiteral(": ") + text;
        };
        check(QStringLiteral("tooltip"), widget->toolTip());
        if (widget->isWindow()) check(QStringLiteral("title"), widget->windowTitle());
        if (auto* label = qobject_cast<QLabel*>(widget)) check(QStringLiteral("text"), label->text());
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) check(QStringLiteral("text"), button->text());
        if (auto* group = qobject_cast<QGroupBox*>(widget)) check(QStringLiteral("title"), group->title());
        if (auto* line = qobject_cast<QLineEdit*>(widget)) check(QStringLiteral("placeholder"), line->placeholderText());
        if (auto* spin = qobject_cast<QSpinBox*>(widget)) {
            check(QStringLiteral("suffix"), spin->suffix());
            check(QStringLiteral("prefix"), spin->prefix());
            check(QStringLiteral("special"), spin->specialValueText());
        }
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget)) {
            check(QStringLiteral("suffix"), spin->suffix());
            check(QStringLiteral("prefix"), spin->prefix());
            check(QStringLiteral("special"), spin->specialValueText());
        }
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            if (!combo->isEditable())
                for (int i = 0; i < combo->count(); ++i) check(QStringLiteral("item"), combo->itemText(i));
        }
        if (auto* tabs = qobject_cast<QTabBar*>(widget))
            for (int i = 0; i < tabs->count(); ++i) {
                check(QStringLiteral("tab"), tabs->tabText(i));
                check(QStringLiteral("tab tooltip"), tabs->tabToolTip(i));
            }
        if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
            if (QAbstractItemModel* model = view->model()) {
                const int columns = model->columnCount();
                for (int c = 0; c < columns; ++c)
                    check(QStringLiteral("header"), model->headerData(c, Qt::Horizontal).toString());
                // Top-level rows only: entries built from user data (media,
                // layers) are not the interface's.
                if (qobject_cast<QListWidget*>(widget) || qobject_cast<QTreeWidget*>(widget))
                    for (int r = 0; r < model->rowCount(); ++r)
                        check(QStringLiteral("item"), model->index(r, 0).data().toString());
            }
        }
        collectActions(widget->actions(), where, found);
    }
    found.removeDuplicates();
    return found;
}

} // namespace openvegas::tests
