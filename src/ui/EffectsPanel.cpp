#include "ui/EffectsPanel.h"
#include "ui_Effects.h"
#include "ui/EffectPlacement.h"
#include "ui/EffectsFavorites.h"

#include <QApplication>
#include <QBrush>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUrl>
#include "app/Settings.h"
#include <QCoreApplication>
#include <QMenu>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace openvegas {
namespace ui {

EffectsPanel::EffectsPanel(QWidget* parent)
    : QDockWidget(parent)
{
    Ui::EffectsPanel form;
    form.setupUi(this);
    m_search = form.effects_panel_search;
    m_showAll = form.effects_panel_show_all;
    m_tree = form.effectsTree;
    m_itemCount = form.effects_panel_count;
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    connect(m_showAll, &QToolButton::clicked, this, &EffectsPanel::showAll);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 22);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (!item) {
            return;
        }
        const QVariant id = item->data(0, Qt::UserRole);
        if (column == 1 && id.isValid() && presetPathOf(item).isEmpty()) {
            toggleFavourite(id.toString());
            return;
        }
        if (const plugin::EffectSpec* spec = specForItem(item)) {
            emit effectSelected(*spec);
        }
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int column) {
                if (column == 1) {
                    return; // the favourites star column
                }
                const QString preset = presetPathOf(item);
                if (!preset.isEmpty()) {
                    emit presetActivated(preset);
                    return;
                }
                if (const plugin::EffectSpec* spec = specForItem(item)) {
                    emit effectActivated(*spec);
                }
            });
    // Rows can also be dragged onto a clip in the timeline; a transition then
    // lands on the clip edge nearer the drop point.
    m_tree->viewport()->installEventFilter(this);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &EffectsPanel::showContextMenu);

    rebuildTree();
}

bool EffectsPanel::eventFilter(QObject* object, QEvent* event)
{
    if (m_tree && object == m_tree->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) m_dragStart = mouse->position().toPoint();
        } else if (event->type() == QEvent::MouseMove) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint pos = mouse->position().toPoint();
            if ((mouse->buttons() & Qt::LeftButton)
                && (pos - m_dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                QTreeWidgetItem* dragged = m_tree->itemAt(m_dragStart);
                const QString preset = presetPathOf(dragged);
                if (!preset.isEmpty()) {
                    // The reference drags a preset as its own resource kind;
                    // here the effect drop takes it by the prefix.
                    auto* mime = new QMimeData;
                    mime->setData(QString::fromLatin1(kEffectMimeType),
                                  (QStringLiteral("preset:") + preset).toUtf8());
                    mime->setText(dragged->text(0));
                    auto* drag = new QDrag(this);
                    drag->setMimeData(mime);
                    drag->exec(Qt::CopyAction);
                    return true;
                }
                const plugin::EffectSpec* spec = specForItem(dragged);
                if (spec) {
                    auto* mime = new QMimeData;
                    mime->setData(QString::fromLatin1(kEffectMimeType), spec->id.value().toUtf8());
                    mime->setText(spec->displayName);
                    auto* drag = new QDrag(this);
                    drag->setMimeData(mime);
                    drag->exec(Qt::CopyAction);
                    return true;
                }
            }
        }
    }
    return QDockWidget::eventFilter(object, event);
}

const plugin::EffectSpec* EffectsPanel::specForItem(const QTreeWidgetItem* item) const
{
    if (!item) {
        return nullptr;
    }
    const QString id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) {
        return nullptr; // a category row
    }
    for (const plugin::EffectSpec& spec : m_effects) {
        if (spec.id.value() == id) {
            return &spec;
        }
    }
    return nullptr;
}

void EffectsPanel::applyFilter()
{
    if (!m_tree) {
        return;
    }
    const QString filter = m_search ? m_search->text().trimmed().toLower() : QString();
    for (int t = 0; t < m_tree->topLevelItemCount(); ++t) {
        QTreeWidgetItem* root = m_tree->topLevelItem(t);
        if (!root) {
            continue;
        }
        bool rootVisible = filter.isEmpty();
        for (int c = 0; c < root->childCount(); ++c) {
            QTreeWidgetItem* child = root->child(c);
            // Name, category and the plugin's own keywords, so a search for
            // "greenscreen" finds Chroma Key.
            const QString haystack = child->data(0, Qt::UserRole + 1).toString();
            const bool match =
                filter.isEmpty() || child->text(0).toLower().contains(filter)
                || (!haystack.isEmpty() && haystack.contains(filter));
            child->setHidden(!match);
            if (match) {
                rootVisible = true;
            }
        }
        root->setHidden(!rootVisible);
        if (rootVisible) {
            root->setExpanded(true);
        }
    }
    updateItemCount();
}

void EffectsPanel::setEffects(const QVector<plugin::EffectSpec>& effects)
{
    m_effects = effects;
    rebuildTree();
}

void EffectsPanel::showAll()
{
    if (m_search) {
        m_search->clear();
    }
    applyFilter();
}

// One effect row: name in column 0, favourite star in column 1. The reference
// draws a star on every effect line and fills it once the effect is favourited.
// The reference marks each effect in the browser with an icon for the plugin
// family it belongs to - its resources carry effect-2d-plugin,
// effect-geometry-plugin, effect-behavior-plugin, effect-ofx-plugin and
// effect-ae-plugin. Our PluginKind already distinguishes the first three; the
// families this port has no host for (OFX, After Effects) are simply not
// produced yet, and everything else falls back to a generic effect mark.
QIcon EffectsPanel::iconForKind(plugin::PluginKind kind)
{
    QString name;
    switch (kind) {
    case plugin::PluginKind::Effect2D:        name = QStringLiteral("effect-2d"); break;
    case plugin::PluginKind::GeometryEffect:  name = QStringLiteral("effect-geometry"); break;
    case plugin::PluginKind::BehaviorEffect:  name = QStringLiteral("effect-behavior"); break;
    case plugin::PluginKind::AudioEffect:     name = QStringLiteral("effect-audio"); break;
    case plugin::PluginKind::AudioTransition:
    case plugin::PluginKind::VideoTransition:
    case plugin::PluginKind::Transitions:     name = QStringLiteral("effect-transition"); break;
    default:                                  name = QStringLiteral("effect-generic"); break;
    }
    // Cached: the browser rebuilds its whole tree on every filter change, and
    // an SVG re-read per row for hundreds of plugins is wasted work.
    static QHash<QString, QIcon> cache;
    auto it = cache.constFind(name);
    if (it != cache.constEnd()) {
        return it.value();
    }
    const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(name));
    cache.insert(name, icon);
    return icon;
}

QTreeWidgetItem* EffectsPanel::addEffectRow(QTreeWidgetItem* parent,
                                            const plugin::EffectSpec& spec)
{
    const QString id = spec.id.value();
    auto* item = new QTreeWidgetItem(parent, {spec.displayName, QString()});
    item->setData(0, Qt::UserRole, id);
    item->setIcon(0, iconForKind(spec.kind));
    // Native plugins declare their own identifier, category and vendor; showing
    // them here is the only place that recovered metadata is visible.
    QStringList lines;
    lines << QStringLiteral("<b>%1</b>").arg(spec.displayName.toHtmlEscaped());
    const QString categoryLine =
        spec.subCategory.isEmpty()
            ? spec.category
            : QStringLiteral("%1 / %2").arg(spec.category, spec.subCategory);
    lines << QStringLiteral("%1 &middot; %2").arg(categoryLine.toHtmlEscaped(),
                                                 spec.kindName().toHtmlEscaped());
    if (id != spec.displayName) {
        lines << QStringLiteral("<code>%1</code>").arg(id.toHtmlEscaped());
    }
    if (!spec.description.isEmpty()) {
        lines << spec.description.toHtmlEscaped();
    }
    if (!spec.renderable && !spec.unavailableReason.isEmpty()) {
        lines << QStringLiteral("<i>%1</i>").arg(spec.unavailableReason.toHtmlEscaped());
    }
    item->setToolTip(0, lines.join(QStringLiteral("<br>")));
    // The search matches the keywords a plugin carries in braces after its name
    // as well as the name itself - "greenscreen" has to find Chroma Key, which
    // is the whole point of the reference shipping them.
    QStringList haystack;
    haystack << spec.displayName << spec.category << spec.subCategory << spec.keywords;
    item->setData(0, Qt::UserRole + 1, haystack.join(QLatin1Char(' ')).toLower());
    // An effect that cannot run is drawn as unavailable rather than looking like
    // any other row that does nothing when clicked.
    if (!spec.renderable) {
        QFont font = item->font(0);
        font.setItalic(true);
        item->setFont(0, font);
        item->setForeground(0, QBrush(QColor(150, 150, 155)));
    }
    const bool starred = m_favourites.contains(id);
    // U+2605 BLACK STAR / U+2606 WHITE STAR, spelled numerically so the source
    // stays pure ASCII for MSVC.
    item->setText(1, QString(starred ? QChar(0x2605) : QChar(0x2606)));
    item->setToolTip(1, starred ? tr("Remove from Favorites") : tr("Add to Favorites"));
    item->setTextAlignment(1, Qt::AlignCenter);
    return item;
}

void EffectsPanel::rebuildTree()
{
    if (!m_tree) {
        return;
    }
    m_tree->clear();

    QHash<QString, QTreeWidgetItem*> categoryRoots;
    for (const plugin::EffectSpec& spec : m_effects) {
        QTreeWidgetItem* root = categoryRoots.value(spec.category);
        if (!root) {
            root = new QTreeWidgetItem(m_tree, {spec.category});
            root->setFlags(Qt::ItemIsEnabled);
            categoryRoots.insert(spec.category, root);
            m_tree->addTopLevelItem(root);
        }
        root->addChild(addEffectRow(root, spec));
    }

    // The presets in their two categories (the reference's 2D Presets and
    // 3D Presets); a 3D preset makes a 3D effect layer this port lacks.
    for (const bool threeD : {false, true}) {
        QTreeWidgetItem* root = nullptr;
        for (const project::EffectPreset& preset : std::as_const(m_presets)) {
            if (preset.threeD != threeD) continue;
            if (!root) {
                root = new QTreeWidgetItem(m_tree, {threeD ? tr("3D Presets") : tr("2D Presets")});
                root->setFlags(Qt::ItemIsEnabled);
            }
            auto* item = new QTreeWidgetItem(root, {preset.name, QString()});
            item->setData(0, Qt::UserRole, QStringLiteral("preset:") + preset.path);
            item->setData(0, Qt::UserRole + 1, preset.name.toLower());
            item->setData(0, Qt::UserRole + 3, preset.path);
            item->setIcon(0, iconForKind(plugin::PluginKind::Effects));
            item->setToolTip(0, QDir::toNativeSeparators(preset.path));
            if (threeD) {
                QFont font = item->font(0);
                font.setItalic(true);
                item->setFont(0, font);
                item->setForeground(0, QBrush(QColor(150, 150, 155)));
            }
        }
    }

    m_tree->sortItems(0, Qt::AscendingOrder);

    // Reference order: Favorites first, then Recents (the order they were
    // used in), then the effect categories.
    const auto pinned = [this](const QString& name, const QStringList& ids, int at) {
        if (ids.isEmpty()) return at;
        auto* root = new QTreeWidgetItem({name});
        root->setFlags(Qt::ItemIsEnabled);
        root->setData(0, Qt::UserRole + 2, true);
        m_tree->insertTopLevelItem(at, root);
        for (const QString& id : ids) {
            for (const plugin::EffectSpec& spec : m_effects) {
                if (spec.id.value() == id) {
                    addEffectRow(root, spec);
                    break;
                }
            }
        }
        return at + 1;
    };
    int at = pinned(tr("Favorites"), favourites(), 0);
    pinned(tr("Recents"), recents(), at);
    m_tree->expandAll();
    updateItemCount();
}

void EffectsPanel::toggleFavourite(const QString& effectId)
{
    if (effectId.isEmpty()) {
        return;
    }
    if (m_favoritesManager) {
        m_favoritesManager->toggleFavorite(effectId);   // favoritesChanged rebuilds
        return;
    }
    if (m_favourites.contains(effectId)) {
        m_favourites.remove(effectId);
    } else {
        m_favourites.insert(effectId);
    }
    rebuildTree();
    applyFilter();
}

QStringList EffectsPanel::favourites() const
{
    if (m_favoritesManager) return m_favoritesManager->favorites();
    QStringList ids(m_favourites.cbegin(), m_favourites.cend());
    ids.sort();
    return ids;
}

void EffectsPanel::setFavourites(const QStringList& ids)
{
    if (m_favoritesManager) {
        for (const QString& id : m_favoritesManager->favorites())
            if (!ids.contains(id)) m_favoritesManager->setFavorite(id, false);
        for (const QString& id : ids) m_favoritesManager->setFavorite(id, true);
        return;
    }
    m_favourites = QSet<QString>(ids.cbegin(), ids.cend());
    rebuildTree();
}

QStringList EffectsPanel::recents() const
{
    return m_favoritesManager ? m_favoritesManager->recents() : QStringList();
}

void EffectsPanel::setFavoritesManager(EffectsFavorites* favorites)
{
    if (m_favoritesManager) disconnect(m_favoritesManager, nullptr, this, nullptr);
    m_favoritesManager = favorites;
    const auto refresh = [this] {
        const QStringList ids = favourites();
        m_favourites = QSet<QString>(ids.cbegin(), ids.cend());
        rebuildTree();
        applyFilter();
    };
    if (m_favoritesManager) {
        connect(m_favoritesManager, &EffectsFavorites::favoritesChanged, this, refresh);
        connect(m_favoritesManager, &EffectsFavorites::recentsChanged, this, refresh);
    }
    refresh();
}

QString EffectsPanel::aboutText(const plugin::EffectSpec& spec)
{
    QString text = (spec.displayName.isEmpty() ? spec.name : spec.displayName)
                   + QCoreApplication::translate("QObject", ", V%1.%2")
                         .arg(spec.version.major()).arg(spec.version.minor());
    if (!spec.author.isEmpty() || !spec.copyright.isEmpty()) {
        text += QLatin1Char('\n');
        if (!spec.author.isEmpty()) text += QLatin1Char('\n') + spec.author;
        if (!spec.copyright.isEmpty()) text += QLatin1Char('\n') + spec.copyright;
    }
    return text;
}

QString EffectsPanel::presetPathOf(const QTreeWidgetItem* item)
{
    return item ? item->data(0, Qt::UserRole + 3).toString() : QString();
}

void EffectsPanel::setPresetsDirectory(const QString& directory)
{
    m_presetsDirectory = directory;
    reloadPresets();
}

void EffectsPanel::reloadPresets()
{
    m_presets = project::scanEffectPresets(m_presetsDirectory);
    rebuildTree();
    applyFilter();
}

bool EffectsPanel::importPresetFiles(const QStringList& files)
{
    if (m_presetsDirectory.isEmpty() || !QDir().mkpath(m_presetsDirectory)) return files.isEmpty();
    bool ok = true;
    for (const QString& file : files) {
        // FUN_140672480: the file's name in the presets folder, made free,
        // then the copy is read as a preset.
        const QString target = project::uniqueFilePath(
            QDir(m_presetsDirectory).filePath(QFileInfo(file).fileName()));
        project::EffectPreset preset;
        const bool copied = QFile::copy(file, target);
        if (copied && !project::readEffectPreset(target, &preset)) QFile::remove(target);
        ok = ok && copied && QFileInfo::exists(target);
    }
    reloadPresets();
    return ok;
}

bool EffectsPanel::exportPresetFiles(const QStringList& presetPaths, const QString& directory)
{
    bool ok = !directory.isEmpty();
    for (const QString& path : presetPaths) {
        if (!QFileInfo::exists(path)) { ok = false; continue; }
        ok = QFile::copy(path, project::uniqueFilePath(QDir(directory).filePath(QFileInfo(path).fileName()))) && ok;
    }
    return ok;
}

bool EffectsPanel::renamePreset(const QString& presetPath, const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\'))) return false;
    // The model's edit (FUN_14067a1d0): "<name>.hfpr" in the same folder.
    const QString target = QFileInfo(presetPath).absoluteDir().filePath(trimmed + QStringLiteral(".hfpr"));
    if (QFileInfo(target).absoluteFilePath() == QFileInfo(presetPath).absoluteFilePath()) return true;
    if (QFileInfo::exists(target)) return false;
    const bool ok = QFile::rename(presetPath, target);
    reloadPresets();
    return ok;
}

bool EffectsPanel::removePresets(const QStringList& presetPaths)
{
    bool ok = true;
    for (const QString& path : presetPaths) ok = QFile::remove(path) && ok;
    reloadPresets();
    return ok;
}

void EffectsPanel::showContextMenu(const QPoint& position)
{
    QTreeWidgetItem* clicked = m_tree->itemAt(position);
    const plugin::EffectSpec* spec = specForItem(clicked);
    const QString id = spec ? spec->id.value() : QString();
    const bool favourite = !id.isEmpty() && m_favourites.contains(id);
    const auto label = [](const char* text) { return QCoreApplication::translate("EffectsActions", text); };
    // The preset rows chosen, and whether all of them may be changed.
    QStringList chosen;
    for (QTreeWidgetItem* item : m_tree->selectedItems())
        if (!presetPathOf(item).isEmpty()) chosen.append(presetPathOf(item));
    if (!presetPathOf(clicked).isEmpty() && !chosen.contains(presetPathOf(clicked))) chosen = {presetPathOf(clicked)};
    bool editable = !chosen.isEmpty();
    for (const project::EffectPreset& preset : std::as_const(m_presets))
        if (chosen.contains(preset.path) && !preset.editable) editable = false;
    QMenu menu(this);
    menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Reload All Presets")), this,
                   &EffectsPanel::reloadPresets);
    menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Import Presets...")), this, [this] {
        // RecentFolders/ImportPresets, the desktop the first time.
        QSettings settings = app::Settings::optionSettings();
        QString folder = settings.value(QStringLiteral("RecentFolders/ImportPresets")).toString();
        if (folder.isEmpty()) folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QCoreApplication::translate("EffectsPanel", "Import Presets"), folder,
            QStringLiteral("%1 (*.hfpr)")
                .arg(QCoreApplication::translate("biff::ui::BiffFileFilter", "Presets")));
        if (files.isEmpty()) return;
        settings.setValue(QStringLiteral("RecentFolders/ImportPresets"), QFileInfo(files.last()).absolutePath());
        if (!importPresetFiles(files))
            QMessageBox::warning(this, QString(), QCoreApplication::translate(
                                                      "EffectsPanel", "One or more presets failed to import."));
    });
    QAction* exportAction = menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Export Presets...")), this,
                                           [this, chosen] {
        QSettings settings = app::Settings::optionSettings();
        QString folder = settings.value(QStringLiteral("RecentFolders/ExportPresets")).toString();
        if (folder.isEmpty()) folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        const QString directory = QFileDialog::getExistingDirectory(
            this, QCoreApplication::translate("EffectsPanel", "Choose a folder to export chosen presets into"),
            folder, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (directory.isEmpty()) return;
        settings.setValue(QStringLiteral("RecentFolders/ExportPresets"), directory);
        if (!exportPresetFiles(chosen, directory))
            QMessageBox::warning(this, QString(), QCoreApplication::translate(
                                                      "EffectsPanel", "One or more presets failed to export."));
    });
    exportAction->setEnabled(!chosen.isEmpty());
    QAction* add = menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Add to Favorites")), this, [this, id] { toggleFavourite(id); });
    add->setEnabled(!id.isEmpty() && !favourite);
    QAction* remove = menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Remove from Favorites")), this, [this, id] { toggleFavourite(id); });
    remove->setEnabled(favourite);
    QAction* clear = menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Clear Recents")), this, [this] {
        if (m_favoritesManager) m_favoritesManager->clearRecents();
    });
    clear->setEnabled(!recents().isEmpty());
    QAction* about = menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "About...")), this, [this, spec] {
        QMessageBox box(this);
        box.setObjectName(QStringLiteral("effectsPanelAbout"));
        box.setIcon(QMessageBox::NoIcon);
        box.setStandardButtons(QMessageBox::Ok);
        box.setDefaultButton(QMessageBox::Ok);
        box.setText(aboutText(*spec));
        box.exec();
    });
    about->setEnabled(spec != nullptr);
    QAction* rename = menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Rename")), this, [this, chosen, label] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, label(QT_TRANSLATE_NOOP("EffectsActions", "Rename")),
                                                   QString(), QLineEdit::Normal,
                                                   QFileInfo(chosen.first()).completeBaseName(), &ok);
        if (ok) renamePreset(chosen.first(), name);
    });
    rename->setEnabled(editable && chosen.size() == 1);
    QAction* removePreset = menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Remove")), this, [this, chosen] {
        const QString question = chosen.size() == 1
            ? QCoreApplication::translate("EffectsPanel",
                                          "Are you sure you want to delete this preset? You cannot undo this action.")
            : QCoreApplication::translate("EffectsPanel",
                                          "Are you sure you want to delete these presets? You cannot undo this action.");
        if (QMessageBox::question(this, QString(), question) == QMessageBox::Yes) removePresets(chosen);
    });
    removePreset->setEnabled(editable);
    QAction* location = menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Open File Location...")), this,
                                       [chosen] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(chosen.first()).absolutePath()));
    });
    location->setEnabled(chosen.size() == 1);
    menu.addSeparator();
    menu.addAction(label(QT_TRANSLATE_NOOP("EffectsActions", "Collapse All")), m_tree, &QTreeWidget::collapseAll);
    menu.exec(m_tree->viewport()->mapToGlobal(position));
}

// Footer counter, matching the reference "424 item(s)" readout. Counts the
// effect rows currently visible, so it tracks the search filter.
void EffectsPanel::updateItemCount()
{
    if (!m_itemCount || !m_tree) {
        return;
    }
    int visible = 0;
    for (int t = 0; t < m_tree->topLevelItemCount(); ++t) {
        QTreeWidgetItem* root = m_tree->topLevelItem(t);
        // Favorites and Recents repeat effects their categories list.
        if (!root || root->isHidden() || root->data(0, Qt::UserRole + 2).toBool()) {
            continue;
        }
        for (int c = 0; c < root->childCount(); ++c) {
            if (!root->child(c)->isHidden()) {
                ++visible;
            }
        }
    }
    m_itemCount->setText(tr("%n item(s)", "", visible));
}

} // namespace ui
} // namespace openvegas
