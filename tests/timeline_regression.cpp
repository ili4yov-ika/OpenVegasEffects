#include <QDialog>
#include <QTimer>
#include <QSettings>
#include <QScopeGuard>
#include "ui/TimelineWidget.h"
#include "ui/EffectInspector.h"
#include "ui/LayerPanel.h"
#include "ui/TrackPanel.h"
#include "ui/Preview360VideoPanel.h"
#include "ui/TextTransformOverlay.h"
#include "ui/NativeCustomUiOverlay.h"
#include "ui/NativeInstanceHost.h"
#include "ui/MediaSettingsDialog.h"
#include "media/VideoProbe.h"
#include "ui/Viewer360View.h"
#include "ui/TrimmerPanel.h"
#include "ui/HistoryPanel.h"
#include "ui/StartPanel.h"
#include "ui/LibraryPanel.h"
#include "ui/MediaPanel.h"
#include "ui/AudioMetersPanel.h"
#include "ui/ViewerWidget.h"
#include "ui/ViewerTransportBar.h"
#include "ui/DockTitleBar.h"
#include "ui/LayoutPanel.h"
#include "ui/LayoutTransformCommand.h"
#include <QScrollArea>
#include "ui/TimelineValueGraphView.h"
#include "ui/Theme.h"
#include "ui/TimelineParameterEditor.h"
#include "plugin/PluginManager.h"
#include "plugin/EffectRender.h"
#include "render/RenderManager.h"
#include "render/TextRender.h"
#include "project/VegfxSerializer.h"
#include "app/ProjectDefaults.h"
#include "composition/MotionTracker.h"
#include "composition/CompositionState.h"
#include "composition/Transition.h"
#include "plugin/NativeEffectRender.h"
#include "render/AudioExport.h"
#include "render/ExportJob.h"
#include "media/AudioWaveform.h"
#include "ui/EffectPlacement.h"
#include "ui/TextureWarning.h"
#include "ui/CameraRule.h"
#include "ui/AutoSave.h"
#include "ui/LearnSidebar.h"
#include "ui/ProjectSettingsDialog.h"
#include "ui/RecoveredProjectsDialog.h"
#include "ui/ImportCompositionDialog.h"
#include "ui/CompositionSettingsDialog.h"
#include "ui/ExportQueue.h"
#include "ui/ExportQueueView.h"
#include "app/AVTemplates.h"
#include "ui/PromptMessage.h"
#include "model3d/Renderer3D.h"
#include "render/FrameDiskCache.h"
#include "render/TextGeometry.h"
#include "render/VideoEncoder.h"
#include "plugin/NativePlugin.h"
#include "media/ProxyMedia.h"
#include <QProcess>
#include <QDirIterator>
#include <QMessageBox>
#include <QMimeData>
#include <QDataStream>
#include <QStandardPaths>
#include <QtEndian>
#include <QTemporaryDir>
#include <QtTest>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QFontDatabase>
#include <QFile>
#include <QSlider>
#include <QSpinBox>
#include <QScrollBar>
#include <QSplitter>
#include <QMenu>
#include <QMainWindow>
#include <QDockWidget>
#include <QColorDialog>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QPushButton>
#include <QUndoStack>
#include <QTreeWidgetItemIterator>
#include <QXmlStreamReader>
#include <QDomDocument>
using namespace openvegas;

static QString testArtifactPath(const QString& fileName)
{
    return QDir(QDir::tempPath()).filePath(QStringLiteral("openvegas-") + fileName);
}

class TestPlugins : public plugin::PluginManager {
public:
    plugin::EffectSpec effect;
    plugin::EffectSpec behavior;
    plugin::EffectSpec transition;
    TestPlugins() {
        transition.id = plugin::PluginId("test.cross"); transition.displayName = "Cross Dissolve";
        transition.kind = plugin::PluginKind::VideoTransition; transition.renderable = true;
        effect.id = plugin::PluginId("test.blur"); effect.displayName = "Blur";
        plugin::EffectParameterSpec p;
        p.name = "radius"; p.displayName = "Radius"; p.type = "double"; p.unit = "px"; p.defaultValue = "0"; p.minimum = 0; p.maximum = 400;
        effect.parameters.append(p);
        p.name = "iterations"; p.displayName = "Iterations"; p.type = "int"; p.unit.clear(); p.defaultValue = "2"; p.minimum = 1; p.maximum = 10;
        effect.parameters.append(p);
        p.name = "dimension"; p.displayName = "Dimension"; p.type = "enum"; p.defaultValue = "Horizontal & Vertical";
        p.choices = {"Horizontal & Vertical", "Horizontal", "Vertical"}; effect.parameters.append(p);
        p.choices.clear(); p.name = "clamp"; p.displayName = "Clamp to Edge"; p.type = "bool"; p.defaultValue = "true"; effect.parameters.append(p);
        p.name = "color"; p.displayName = "White balance"; p.type = "color"; p.defaultValue = "#ffffff"; effect.parameters.append(p);
        behavior.id = plugin::PluginId("test.behavior"); behavior.displayName = "Drift";
        behavior.kind = plugin::PluginKind::BehaviorEffect; behavior.renderable = true;
        p = {}; p.name = "amount"; p.displayName = "Amount"; p.type = "double";
        p.defaultValue = "1"; p.minimum = -10; p.maximum = 10;
        behavior.parameters.append(p);
    }
    void setPluginDirs(const QVector<QString>&) override {}
    void setCallbacks(const plugin::Callbacks&) override {}
    core::Result scan() override { return core::Result::ok(); }
    QVector<plugin::PluginId> allPluginIds() const override { return {effect.id, behavior.id}; }
    QVector<plugin::PluginId> pluginIdsByKind(plugin::PluginKind kind) const override {
        return kind == plugin::PluginKind::BehaviorEffect
            ? QVector<plugin::PluginId>{behavior.id} : QVector<plugin::PluginId>{effect.id};
    }
    QVector<plugin::PluginId> pluginIdsByCategory(const QString&) const override { return allPluginIds(); }
    std::shared_ptr<plugin::Plugin> plugin(const plugin::PluginId&) const override { return {}; }
    plugin::EffectSpec spec(const plugin::PluginId& id) const override {
        return id == behavior.id ? behavior : id == transition.id ? transition : effect;
    }
    std::shared_ptr<plugin::EffectInstance> createEffect(const plugin::PluginId&) override { return {}; }
};
class TimelineRegression : public QObject {
    Q_OBJECT
    TestPlugins plugins;
    std::shared_ptr<composition::Composition> comp;
    std::unique_ptr<ui::TimelineWidget> panel;
    QUndoStack history;
    QTreeWidgetItem* parameter(int p) {
        for (QTreeWidgetItemIterator it(tree()); *it; ++it)
            if ((*it)->data(0, Qt::UserRole + 3).isValid() && (*it)->data(0, Qt::UserRole + 3).toInt() == p) return *it;
        return nullptr;
    }
    ui::TimelineTree* tree() { return panel->findChild<ui::TimelineTree*>(); }
    // Clicks `button` on the next showPrompt box `key` (Adding3DCameras, ...)
    // once its modal loop runs; counts it in `shown`. A prompt silenced in
    // this user's Options never appears and answers by itself.
    static void answerPrompt(const QString& key, QMessageBox::StandardButton button, int* shown) {
        QTimer::singleShot(0, [key, button, shown] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box || box->objectName() != QStringLiteral("prompt") + key) return;
            if (shown) ++*shown;
            box->button(button)->click();
        });
    }
    template<class T> T* editor(int p) {
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* root = panel->findChild<QWidget*>(QStringLiteral("timelineParam_0_0_0_%1").arg(p));
        return root ? root->findChild<T*>() : nullptr;
    }
private slots:
    void init() {
        plugins = TestPlugins();
        history.clear(); comp = std::make_shared<composition::Composition>(); comp->setDurationSeconds(50);
        auto& l = comp->addLayer("shot_1.mp4"); composition::Clip clip; clip.durationSeconds = 50;
        composition::Effect effect; effect.pluginId = plugins.effect.id; effect.name = "Blur";
        for (const auto& p : plugins.effect.parameters) effect.parameterValues.append(p.defaultValue);
        clip.effects.append(effect); l.clips.append(clip);
        panel = std::make_unique<ui::TimelineWidget>(); panel->resize(1315, 442);
        panel->setUndoStack(&history); panel->setPluginManager(&plugins); panel->setComposition(comp);
        panel->show(); QTest::qWait(40);
    }
    void cleanup() { panel.reset(); history.clear(); }
    void nativeLayerPickerKeepsIdentityAcrossDuplicateNames() {
        plugin::EffectParameterSpec spec;
        spec.type = QStringLiteral("layer");
        spec.choices = {QStringLiteral("None"), QStringLiteral("Target"),
                        QStringLiteral("Target")};
        spec.choiceValues = {QStringLiteral("00000000-0000-0000-0000-000000000000"),
                             QStringLiteral("layer-one"), QStringLiteral("layer-two")};
        QVariant stored = QStringLiteral("layer-one");
        QVector<std::function<void()>> readers;
        QWidget holder;
        auto* editor = ui::timelineParameterEditor(
            &holder, spec, QStringLiteral("layerPicker"),
            [&stored] { return stored; },
            [&stored](QVariant value) { stored = value; }, readers);
        auto* combo = editor->findChild<QComboBox*>();
        QVERIFY(combo);
        QCOMPARE(combo->currentIndex(), 1);
        combo->setCurrentIndex(2);
        QCOMPARE(stored.toString(), QStringLiteral("layer-two"));
        combo->setItemText(2, QStringLiteral("Renamed"));
        for (const auto& refresh : readers) refresh();
        QCOMPARE(combo->currentIndex(), 2);
        QCOMPARE(stored.toString(), QStringLiteral("layer-two"));
    }
    void relativePathsAndWorkspaceSurviveProjectMove() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QDir root(temp.path()); QVERIFY(root.mkpath("original/media")); QVERIFY(root.mkpath("moved/media"));
        const QString original = root.filePath("original");
        const QString imagePath = original + "/media/image.png";
        QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::red); QVERIFY(image.save(imagePath));
        media::MediaManager media; QVERIFY(media.importFile(imagePath).isSuccess());
        composition::Composition scene; scene.addClip("V1", media.assetByFilePath(imagePath).id(), 0, 1);
        scene.addClip("V1", media.assetByFilePath(imagePath).id(), 1, 1);
        auto nested = std::make_shared<composition::Composition>();
        nested->addClip("V1", media.assetByFilePath(imagePath).id(), 0, 1);
        composition::Clip nestedClip; nestedClip.durationSeconds = 1;
        nestedClip.nestedComposition = nested; nestedClip.nestedCompositionId = nested->id();
        nestedClip.mediaId = core::Identifier("composition:" + nested->id().value());
        scene.addLayer("nested").clips.append(nestedClip);
        project::ProjectSaveOptions options;
        options.useRelativePaths = true; options.screenLayout = QByteArray("layout\0bytes", 12);
        const QString path = original + "/relative.vegfx";
        QVERIFY(project::VegfxSerializer::saveToFile(path, scene, media, options).isSuccess());
        QFile xml(path); QVERIFY(xml.open(QIODevice::ReadOnly)); const QByteArray data = xml.readAll(); xml.close();
        QDomDocument document; QVERIFY(document.setContent(data));
        QCOMPARE(document.elementsByTagName("Filename").at(0).toElement().text(), QString("media/image.png"));
        QVERIFY(!data.contains(imagePath.toUtf8()));
        const QString moved = root.filePath("moved");
        QVERIFY(QFile::copy(imagePath, moved + "/media/image.png"));
        QVERIFY(QFile::copy(path, moved + "/relative.vegfx"));
        media::MediaManager loadedMedia; composition::Composition loaded; QByteArray layout;
        QVERIFY(project::VegfxSerializer::loadFromFile(moved + "/relative.vegfx", &loaded, &loadedMedia, &layout).isSuccess());
        QCOMPARE(layout, options.screenLayout);
        QCOMPARE(loadedMedia.assets().size(), 1);
        QCOMPARE(loadedMedia.assets()[0].filePath(), moved + "/media/image.png");
        QCOMPARE(loaded.layers()[0].clips.size(), 2);
        QCOMPARE(loaded.layers()[0].clips[1].mediaId, loadedMedia.assets()[0].id());
        QVERIFY(loaded.layers()[1].clips[0].nestedComposition);
        QCOMPARE(loaded.layers()[1].clips[0].nestedComposition->layers()[0].clips[0].mediaId,
                 loadedMedia.assets()[0].id());
        QVERIFY(QFile::remove(moved + "/media/image.png"));
        media::MediaManager offline; composition::Composition offlineScene;
        QVERIFY(project::VegfxSerializer::loadFromFile(moved + "/relative.vegfx", &offlineScene, &offline).isSuccess());
        QCOMPARE(offline.assets()[0].filePath(), moved + "/media/image.png");
        options.useRelativePaths = false; options.screenLayout.clear();
        QVERIFY(project::VegfxSerializer::saveToFile(path, scene, media, options).isSuccess());
        QVERIFY(xml.open(QIODevice::ReadOnly)); const QByteArray absolute = xml.readAll();
        QVERIFY(absolute.contains(imagePath.toUtf8())); QVERIFY(!absolute.contains("OpenVegasScreenLayout"));
    }
    void layout() {
        QVERIFY(parameter(0));
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); QVERIFY(canvas);
        QCOMPARE(tree()->horizontalScrollBar()->maximum(), 0);
        QVERIFY(qAbs(tree()->viewport()->mapTo(panel.get(), tree()->visualItemRect(parameter(0)).topLeft()).y()
            - canvas->mapTo(panel.get(), QPoint(0, ui::kTimelineRulerHeight + tree()->visualItemRect(parameter(0)).top())).y()) <= 1);
        QVERIFY(panel->grab().save(testArtifactPath(QStringLiteral("timeline-panel.png"))));
        const int keyBarLeft = panel->findChild<QToolButton*>("timelinePreviousKeyframe")->mapTo(panel.get(), QPoint()).x();
        const int canvasLeft = canvas->mapTo(panel.get(), QPoint()).x();
        QVERIFY2(qAbs(keyBarLeft - canvasLeft) <= 8,
                 qPrintable(QStringLiteral("key bar %1, canvas %2").arg(keyBarLeft).arg(canvasLeft)));
    }
    void referenceScreenshots() {
        plugins.effect = plugin::timelineBuiltinSpecs()[0];
        auto& effect = comp->layerRef(0).clips[0].effects[0]; effect.pluginId = plugins.effect.id; effect.name = plugins.effect.displayName;
        effect.parameterValues = {"9.48", "2", "Horizontal & Vertical", "true"};
        panel->refreshKeyFrames(); panel->setPlayheadPosition(23.6); QTest::qWait(30);
        QVERIFY(panel->grab().save(testArtifactPath(QStringLiteral("timeline-blur.png"))));
        plugins.effect = plugin::timelineBuiltinSpecs()[1]; effect.pluginId = plugins.effect.id; effect.name = plugins.effect.displayName;
        effect.parameterValues.clear(); for (const auto& p : plugins.effect.parameters) effect.parameterValues.append(p.defaultValue);
        effect.animation[0].set(0, 1.); effect.animation[0].set(180, .4); effect.animation[0].set(496, .8); effect.animation[0].set(738, .6); effect.animation[0].set(1002, 1.);
        panel->refreshKeyFrames(); panel->setPlayheadPosition(496./30.); tree()->setCurrentItem(parameter(0)); QTest::qWait(30);
        parameter(3)->parent()->setExpanded(true); QTest::qWait(20);
        QVERIFY(panel->grab().save(testArtifactPath(QStringLiteral("timeline-color-wheels.png"))));
    }
    void controlsMirrorSelection() {
        auto controls = std::make_unique<ui::EffectInspector>();
        controls->resize(590, 700); controls->setUndoStack(&history);
        controls->setPluginManager(&plugins); controls->setComposition(comp);
        controls->setSelection(0, -1); controls->show(); QTest::qWait(30);
        auto* controlsTree = controls->findChild<QTreeWidget*>("controlsTree"); QVERIFY(controlsTree);
        QCOMPARE(controlsTree->topLevelItemCount(), 6);
        QCOMPARE(controlsTree->topLevelItem(0)->text(0), QString("Layer Properties"));
        QCOMPARE(controlsTree->topLevelItem(3)->text(0), QString("Effects"));
        QCOMPARE(controlsTree->topLevelItem(4)->text(0), QString("Transform"));
        QCOMPARE(controlsTree->topLevelItem(5)->text(0), QString("Behaviors"));
        QVERIFY(controls->findChild<QCheckBox*>("controlsLayerVisible"));
        auto* radiusRoot = controls->findChild<QWidget*>("controlsParam_0_0_0_0"); QVERIFY(radiusRoot);
        auto* radius = radiusRoot->findChild<QDoubleSpinBox*>(); QVERIFY(radius);
        QCOMPARE(radius->minimum(), 0.); QCOMPARE(radius->maximum(), 400.);
        radius->setValue(9.48); QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[0], QString("9.48"));
        QCOMPARE(history.count(), 1); history.undo(); QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[0], QString("0"));
        history.redo(); QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[0], QString("9.48"));
        auto* search = controls->findChild<QLineEdit*>("controlsSearch"); search->setText("Radius");
        QVERIFY(!controlsTree->topLevelItem(3)->isHidden()); QVERIFY(controlsTree->topLevelItem(0)->isHidden());
        search->clear();
        auto* dimension = controls->findChild<QComboBox*>("controlsLayerDimension"); QVERIFY(dimension);
        // No camera in the shot yet: the 3D switch asks for one and adds it.
        answerPrompt("Adding3DCameras", QMessageBox::Yes, nullptr);
        dimension->setCurrentIndex(1); QTest::qWait(10);
        QCOMPARE(comp->layers()[0].dimension, composition::LayerDimension::ThreeD);
        QCOMPARE(comp->layers().size(), 2);
        QCOMPARE(comp->layers()[1].kind, composition::LayerKind::Camera);
        QCOMPARE(controlsTree->topLevelItem(4)->text(0), QString("World Transform"));
        history.clear();
        auto* xRoot = controls->findChild<QWidget*>(QStringLiteral("controlsTransform_0_%1_0").arg(int(composition::TransformProperty::Scale)));
        QVERIFY(xRoot); xRoot->findChild<QDoubleSpinBox*>()->setValue(200);
        QCOMPARE(comp->layers()[0].transform.scalePercent, QPointF(200, 200));
        history.undo(); QCOMPARE(comp->layers()[0].transform.scalePercent, QPointF(100, 100));
        auto* key = controls->findChild<QToolButton*>("controlsParamKey_0_0"); QVERIFY(key);
        controls->setCurrentTime(1); key->click();
        QVERIFY(comp->layers()[0].clips[0].effects[0].animation[0].at(30));
        history.clear();
        auto* addMask = controls->findChild<QToolButton*>("controlsAddMask");
        QVERIFY(addMask); addMask->click();
        QTRY_COMPARE(comp->layers()[0].masks.size(), 1);
        QTRY_VERIFY(controls->findChild<QDoubleSpinBox*>("controlsMaskOpacity_0"));
        auto* maskOpacity = controls->findChild<QDoubleSpinBox*>("controlsMaskOpacity_0");
        maskOpacity->setValue(55.0);
        QCOMPARE(comp->layers()[0].masks[0].opacity, .55);
        history.undo(); QCOMPARE(comp->layers()[0].masks[0].opacity, 1.0);

        composition::Effect behavior; behavior.pluginId = plugins.behavior.id;
        behavior.name = plugins.behavior.displayName; behavior.parameterValues = {"1"};
        comp->layerRef(0).clips[0].effects.append(behavior); controls->refresh();
        QVERIFY(controls->findChild<QWidget*>("controlsParam_0_0_1_0"));
        // A native module switching a control off (SetPropertyState) hides
        // its row until it switches it on again.
        const auto behaviorRow = [&]() -> QTreeWidgetItem* {
            for (QTreeWidgetItemIterator it(controlsTree); *it; ++it) {
                if ((*it)->data(0, Qt::UserRole + 2).toInt() == 1 && (*it)->data(0, Qt::UserRole + 3).toInt() == 0
                    && (*it)->data(0, Qt::UserRole + 3).isValid()) return *it;
            }
            return nullptr;
        };
        QVERIFY(behaviorRow() && !behaviorRow()->isHidden());
        const QString instance = comp->layers()[0].id.value() + "/0/1";
        const QString control = plugins.behavior.parameters.value(0).name;
        plugin::setNativeControlStates(instance, {{control, false}});
        controls->setCurrentTime(2);
        QVERIFY(behaviorRow() && behaviorRow()->isHidden());
        plugin::setNativeControlStates(instance, {{control, true}});
        controls->setCurrentTime(3);
        QVERIFY(behaviorRow() && !behaviorRow()->isHidden());
        plugin::clearNativeControlStates();
        QVERIFY(controls->grab().save(testArtifactPath(QStringLiteral("controls-panel.png"))));
    }
    void layerPanelSelectionAndEditing() {
        history.clear();
        auto layerPanel = std::make_unique<ui::LayerPanel>();
        layerPanel->resize(360, 520); layerPanel->setUndoStack(&history);
        layerPanel->bindModel(comp); layerPanel->show(); QTest::qWait(20);
        QCOMPARE(layerPanel->objectName(), QString("LayerPanel"));
        QCOMPARE(layerPanel->windowTitle(), QString("Layer"));
        QCOMPARE(layerPanel->findChild<QLabel*>("dummyWidget")->text(), QString("No Selection"));

        layerPanel->setSelection(0);
        auto* name = layerPanel->findChild<QLineEdit*>("layerName"); QVERIFY(name); QVERIFY(name->isVisible());
        name->setText("Renamed layer"); QMetaObject::invokeMethod(name, "editingFinished");
        QCOMPARE(comp->layers()[0].name, QString("Renamed layer")); QCOMPARE(history.count(), 1);
        history.undo(); QCOMPARE(comp->layers()[0].name, QString("shot_1.mp4"));
        history.redo(); QCOMPARE(comp->layers()[0].name, QString("Renamed layer"));

        auto* visible = layerPanel->findChild<QCheckBox*>("layerVisible"); QVERIFY(visible);
        visible->click(); QVERIFY(!comp->layers()[0].visible); history.undo(); QVERIFY(comp->layers()[0].visible);
        auto* opacity = layerPanel->findChild<QDoubleSpinBox*>("layerOpacity"); QVERIFY(opacity);
        opacity->setValue(42.5); QMetaObject::invokeMethod(opacity, "editingFinished");
        QCOMPARE(comp->layers()[0].opacity, .425);

        auto& parent = comp->addLayer("Parent"); parent.kind = composition::LayerKind::Point;
        layerPanel->refresh();
        auto* parentBox = layerPanel->findChild<QComboBox*>("layerParent"); QVERIFY(parentBox);
        parentBox->setCurrentIndex(parentBox->findData(1));
        QCOMPARE(comp->layers()[0].parentLayerId, comp->layers()[1].id);
        QVERIFY(layerPanel->findChild<QPushButton*>("layerLabelColor"));
        QVERIFY(layerPanel->findChild<QLabel*>("layerID")->text() == comp->layers()[0].id.value());
        QVERIFY(layerPanel->grab().save(testArtifactPath(QStringLiteral("layer-panel.png"))));

        layerPanel->setSelection(QVector<int>{0, 1});
        QCOMPARE(layerPanel->findChild<QLabel*>("dummyWidget")->text(), QString("Multiple Selection"));
        ui::TrackPanel track; track.bindModel(comp);
        QCOMPARE(track.windowTitle(), QString("Track"));
        QVERIFY(track.findChild<QToolButton*>("toolButtonNewLayer"));
    }
    void layerPanelLockCyclesAndStableUndo() {
        auto model = std::make_shared<composition::Composition>();
        model->addLayer("Selected"); model->addLayer("A"); model->addLayer("B");
        const auto selectedId = model->layers()[0].id;
        QUndoStack stack;
        auto inspector = std::make_unique<ui::LayerPanel>();
        inspector->bindModel(model); inspector->setUndoStack(&stack); inspector->setSelection(0);
        auto* name = inspector->findChild<QLineEdit*>("layerName");
        QMetaObject::invokeMethod(name,"editingFinished"); QCOMPARE(stack.count(),0);
        auto* lock = inspector->findChild<QCheckBox*>("layerLocked"); lock->setChecked(true);
        QVERIFY(model->layers()[0].locked); QVERIFY(!name->isEnabled());
        name->setText("Forbidden"); QMetaObject::invokeMethod(name,"editingFinished");
        QCOMPARE(model->layers()[0].name,QString("Selected")); QCOMPARE(stack.count(),1);
        lock->setChecked(false); name->setText("Renamed"); QMetaObject::invokeMethod(name,"editingFinished");
        QVERIFY(model->swapLayers(0,2)); stack.undo();
        QCOMPARE(model->layers()[2].id,selectedId); QCOMPARE(model->layers()[2].name,QString("Selected"));
        // An unrelated malformed parent cycle must neither hang nor become an option.
        model->layerRef(0).parentLayerId=model->layers()[1].id;
        model->layerRef(1).parentLayerId=model->layers()[0].id;
        inspector->setSelection(2);
        auto* parents=inspector->findChild<QComboBox*>("layerParent"); QCOMPARE(parents->count(),1);
        inspector.reset(); stack.redo(); // Command remains safe after its panel is destroyed.
        QCOMPARE(model->layers()[2].name,QString("Renamed"));
    }
    void layerPreviewIsolatedAndStaleSelection() {
        auto model=std::make_shared<composition::Composition>(); model->setSize(320,180);
        for (const auto& color : {QColor(Qt::red),QColor(Qt::blue)}) {
            auto& l=model->addLayer(color.name()); l.kind=composition::LayerKind::Plane; l.planeColor=color;
            composition::Clip clip; clip.durationSeconds=10; l.clips.append(clip);
        }
        model->layerRef(0).visible=false;
        ui::LayerPanel inspector; inspector.resize(600,650); inspector.bindModel(model);
        inspector.setMediaManager(std::make_shared<media::MediaManager>()); inspector.show(); inspector.setSelection(0);
        const auto isolated=inspector.previewComposition(); QVERIFY(isolated); QCOMPARE(isolated->layers().size(),1);
        QVERIFY(isolated->layers()[0].visible); QVERIFY(!model->layers()[0].visible);
        QTRY_VERIFY_WITH_TIMEOUT(!inspector.previewFrame().isNull(),5000);
        QVERIFY(inspector.previewFrame().pixelColor(160,90).red()>240);
        inspector.setSelection(1); inspector.setSelection(QVector<int>{0,1});
        QTest::qWait(100); QVERIFY(inspector.previewFrame().isNull());
    }
    void cameraFovProjectRoundTrip() {
        comp->layerRef(0).kind=composition::LayerKind::Camera; comp->layerRef(0).cameraFieldOfView=72.5;
        QTemporaryDir temp; media::MediaManager assets; composition::Composition loaded;
        const QString path=temp.filePath("camera.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path,*comp,assets).isSuccess());
        QVERIFY(project::VegfxSerializer::loadFromFile(path,&loaded,&assets).isSuccess());
        QCOMPARE(loaded.layers()[0].cameraFieldOfView,72.5);
    }
    void layerIdentityRoundTrip() {
        auto& parent = comp->addLayer("Parent"); parent.kind = composition::LayerKind::Point;
        comp->layerRef(0).parentLayerId = parent.id;
        const QString childId = comp->layers()[0].id.value();
        const QString parentId = parent.id.value();
        QTemporaryDir temp; QVERIFY(temp.isValid()); media::MediaManager media;
        const QString path = temp.filePath("layer-ids.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *comp, media).isSuccess());
        composition::Composition loaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &media).isSuccess());
        QCOMPARE(loaded.layers().size(), 2);
        QCOMPARE(loaded.layers()[0].id.value(), childId);
        QCOMPARE(loaded.layers()[1].id.value(), parentId);
        QCOMPARE(loaded.layers()[0].parentLayerId.value(), parentId);
    }
    void viewer360ProjectionAndControls() {
        QImage equirect(400, 200, QImage::Format_ARGB32);
        for (int y = 0; y < equirect.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(equirect.scanLine(y));
            for (int x = 0; x < equirect.width(); ++x)
                line[x] = qRgb(x * 255 / (equirect.width() - 1),
                               y * 255 / (equirect.height() - 1), 80);
        }
        ui::Preview360VideoPanel viewer;
        viewer.resize(420, 320); viewer.setFrame(equirect); viewer.show(); QTest::qWait(20);
        QCOMPARE(viewer.objectName(), QString("Preview360VideoPanel"));
        QCOMPARE(viewer.windowTitle(), QString("360 Viewer"));
        auto* combo = viewer.findChild<QComboBox*>("comboBoxCurrentView"); QVERIFY(combo);
        QCOMPARE(combo->count(), 7);
        QCOMPARE(combo->itemText(0), QString("Front")); QCOMPARE(combo->itemText(6), QString("Custom"));
        QVERIFY(viewer.findChild<QToolButton*>("toolButtonProperties"));
        auto* canvas = viewer.videoWidget(); QVERIFY(canvas);
        QCOMPARE(canvas->objectName(), QString("glWidget"));
        QCOMPARE(canvas->property("stop-playback").toBool(), false);

        const auto centre = [canvas](ui::Preview360VideoWidget::View view) {
            canvas->setCurrentView(view); return canvas->projectedFrame(QSize(1, 1)).pixelColor(0, 0);
        };
        const QColor front = centre(ui::Preview360VideoWidget::View::Front);
        const QColor right = centre(ui::Preview360VideoWidget::View::Right);
        const QColor left = centre(ui::Preview360VideoWidget::View::Left);
        const QColor back = centre(ui::Preview360VideoWidget::View::Back);
        QVERIFY(left.red() < front.red()); QVERIFY(front.red() < right.red()); QVERIFY(back.red() > 240);
        QVERIFY(centre(ui::Preview360VideoWidget::View::Top).green() < 10);
        QVERIFY(centre(ui::Preview360VideoWidget::View::Bottom).green() > 245);
        canvas->setYawPitch(27.0, -12.0); QCOMPARE(canvas->currentView(), ui::Preview360VideoWidget::View::Custom);
        QCOMPARE(combo->currentData().toInt(), int(ui::Preview360VideoWidget::View::Custom));
        canvas->setFieldOfView(75.0); QCOMPARE(canvas->fieldOfView(), 75.0);
        canvas->setUseCameraFOV(true); canvas->setCameraFieldOfView(55.0);
        canvas->setEnvWrapX(true); canvas->setEnvWrapY(true);
        QVERIFY(!canvas->projectedFrame(QSize(320, 220)).isNull());
        QVERIFY(viewer.grab().save(testArtifactPath(QStringLiteral("viewer360-panel.png"))));
    }
    void viewer360RotationAndPropertiesCancel() {
        QImage gradient(400,200,QImage::Format_ARGB32);
        for(int y=0;y<200;++y) for(int x=0;x<400;++x) gradient.setPixel(x,y,qRgb(x*255/399,y*255/199,0));
        ui::Preview360VideoPanel panel; auto* video=panel.videoWidget(); video->setFrame(gradient);
        video->setYawPitch(0,30); const int first=video->projectedFrame(QSize(101,101)).pixelColor(50,50).green();
        video->setYawPitch(90,30); const int second=video->projectedFrame(QSize(101,101)).pixelColor(50,50).green();
        QVERIFY(qAbs(first-second)<=1); // Pitch does not depend on longitude.
        video->setUseCameraFOV(true); video->setCameraFieldOfView(40);
        const QImage narrow=video->projectedFrame(QSize(101,101)); video->setCameraFieldOfView(100);
        QVERIFY(narrow!=video->projectedFrame(QSize(101,101)));
        video->setCurrentView(ui::Preview360VideoWidget::View::Top);
        QTest::keyClick(video,Qt::Key_Home); QCOMPARE(video->currentView(),ui::Preview360VideoWidget::View::Front);
        video->setRoll(33); QCOMPARE(video->roll(),33.0);
        QTest::keyClick(video,Qt::Key_Right,Qt::ShiftModifier); QCOMPARE(video->yaw(),-10.0);
        auto* properties=panel.findChild<QToolButton*>("toolButtonProperties");
        QTimer::singleShot(0,[] { if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dialog->findChild<QDoubleSpinBox*>("viewer360Yaw")->setValue(145); dialog->reject();
        }});
        properties->click(); QCOMPARE(video->yaw(),-10.0); QCOMPARE(video->roll(),33.0);
        video->setCurrentView(ui::Preview360VideoWidget::View::Top);
        QTimer::singleShot(0,[] { if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->accept(); });
        properties->click(); QCOMPARE(video->currentView(),ui::Preview360VideoWidget::View::Top);
        QImage translucent(400,200,QImage::Format_ARGB32); translucent.fill(qRgba(120,40,20,64));
        video->setFrame(translucent);
        QCOMPARE(video->projectedFrame(QSize(101,101)).pixelColor(50,50).alpha(),64);
    }
    void viewer360NativeWrapAndDirectionRecovery() {
        QTemporaryDir settingsRoot; QVERIFY(settingsRoot.isValid());
        const auto previousFormat=QSettings::defaultFormat();
        const QString previousOrganization=QCoreApplication::organizationName();
        const QString previousApplication=QCoreApplication::applicationName();
        const auto restoreSettings=qScopeGuard([=] {
            QSettings::setDefaultFormat(previousFormat);
            QCoreApplication::setOrganizationName(previousOrganization);
            QCoreApplication::setApplicationName(previousApplication);
        });
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsRoot.path());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenVegas360Regression"));
        QCoreApplication::setApplicationName(QStringLiteral("WrapRecovery"));
        { QSettings legacy; legacy.setValue("Viewer360/View",0);
          legacy.setValue("Viewer360/Yaw",30.0); legacy.setValue("Viewer360/EnvWrapY",true); }
        ui::Preview360VideoPanel panel; auto* video=panel.videoWidget();
        QCOMPARE(video->yaw(),-30.0); QCOMPARE(int(video->wrapYMode()),1);
        QImage image(4,2,QImage::Format_ARGB32);
        for (int y=0;y<2;++y) for(int x=0;x<4;++x) image.setPixelColor(x,y,QColor(x*80,0,0));
        video->setFrame(image); video->setCurrentView(ui::Preview360VideoWidget::View::Back);
        video->setWrapXMode(ui::Preview360VideoWidget::WrapMode::No);
        QCOMPARE(video->projectedFrame(QSize(1,1)).pixelColor(0,0).red(),240);
        video->setWrapXMode(ui::Preview360VideoWidget::WrapMode::Tile);
        QCOMPARE(video->projectedFrame(QSize(1,1)).pixelColor(0,0).red(),120);
        video->setWrapXMode(ui::Preview360VideoWidget::WrapMode::Reflect);
        QCOMPARE(video->projectedFrame(QSize(1,1)).pixelColor(0,0).red(),240);
        video->setCurrentView(ui::Preview360VideoWidget::View::Left); QCOMPARE(video->yaw(),90.0);
        video->setCurrentView(ui::Preview360VideoWidget::View::Right); QCOMPARE(video->yaw(),-90.0);
        video->setYawPitch(30,20); video->setRoll(10);
        auto* properties=panel.findChild<QToolButton*>("toolButtonProperties");
        QTimer::singleShot(0,[] { if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dialog->findChild<QDoubleSpinBox*>("viewer360Yaw")->setValue(0);
            dialog->findChild<QDoubleSpinBox*>("viewer360Pitch")->setValue(90);
            dialog->findChild<QDoubleSpinBox*>("viewer360Roll")->setValue(0);
            dialog->findChild<QDoubleSpinBox*>("viewer360Fov")->setValue(175);
            dialog->findChild<QComboBox*>("envWrapY")->setCurrentIndex(2); dialog->accept();
        }});
        properties->click(); QCOMPARE(video->currentView(),ui::Preview360VideoWidget::View::Top);
        QCOMPARE(video->fieldOfView(),175.0); QCOMPARE(int(video->wrapYMode()),2);
        ui::Preview360VideoPanel restored; QCOMPARE(int(restored.videoWidget()->wrapYMode()),2);
    }
    void viewerToolStripAlignmentAndTools() {
        ui::ViewerWidget viewer;
        viewer.resize(640, 360);
        viewer.show();
        QTest::qWait(20);

        auto* strip = viewer.findChild<QWidget*>(QStringLiteral("viewer-tool-strip"));
        QVERIFY(strip);
        QCOMPARE(strip->geometry(), QRect(0, 0, 24, 360));

        const QStringList topButtons = {
            QStringLiteral("toolButtonPointer"),
            QStringLiteral("toolButtonHand"),
            QStringLiteral("toolButtonText"),
            QStringLiteral("toolButtonMaskShape"),
            QStringLiteral("toolButtonMaskPen"),
            QStringLiteral("toolButtonCameraOrbit")
        };
        for (int i = 0; i < topButtons.size(); ++i) {
            auto* button = strip->findChild<QToolButton*>(topButtons[i]);
            QVERIFY2(button, qPrintable(topButtons[i]));
            QCOMPARE(button->size(), QSize(22, 22));
            QCOMPARE(button->pos(), QPoint(1, 1 + i * 22));
            QCOMPARE(button->iconSize(), QSize(16, 16));
        }

        auto* pointer = strip->findChild<QToolButton*>(QStringLiteral("toolButtonPointer"));
        auto* hand = strip->findChild<QToolButton*>(QStringLiteral("toolButtonHand"));
        auto* path = strip->findChild<QToolButton*>(QStringLiteral("toolButtonVectorPath"));
        QVERIFY(pointer && hand && path);
        QVERIFY(pointer->isChecked());
        QCOMPARE(path->size(), QSize(22, 22));
        QCOMPARE(path->pos(), QPoint(1, strip->height() - 23));

        hand->click();
        QCOMPARE(viewer.tool(), ui::ViewerWidget::ViewerTool::Hand);
        QVERIFY(hand->isChecked());
        QVERIFY(!pointer->isChecked());
        path->click();
        QCOMPARE(viewer.tool(), ui::ViewerWidget::ViewerTool::VectorPath);
        QVERIFY(path->isChecked());
        viewer.setProjectSize(QSize(640, 360));
        viewer.setFrame(QImage(640, 360, QImage::Format_ARGB32_Premultiplied));
        QSignalSpy pathCreated(&viewer, &ui::ViewerWidget::freehandMaskCreationRequested);
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
        QTest::mouseMove(&viewer, QPoint(120, 115));
        QTest::mouseMove(&viewer, QPoint(140, 100));
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, QPoint(160, 120));
        QCOMPARE(pathCreated.count(), 1);
        pointer->click();
        QCOMPARE(viewer.tool(), ui::ViewerWidget::ViewerTool::Select);
        QVERIFY(pointer->isChecked());

        viewer.resize(500, 280);
        QCoreApplication::processEvents();
        QCOMPARE(strip->geometry(), QRect(0, 0, 24, 280));
        QCOMPARE(path->pos(), QPoint(1, 257));
        QVERIFY(viewer.grab().save(testArtifactPath(QStringLiteral("viewer-tool-strip.png"))));
    }
    void viewerTextTransformFrame() {
        // A 640x360 frame shown at 100% beside the 24 px tool strip, so a
        // widget pixel is a canvas pixel shifted by the strip.
        ui::ViewerWidget viewer;
        viewer.resize(664, 360); viewer.show(); QTest::qWait(20);
        viewer.setProjectSize(QSize(640, 360));
        viewer.setFrame(QImage(640, 360, QImage::Format_ARGB32_Premultiplied));
        QCOMPARE(viewer.mapping().toWidget(QPointF(10, 20)), QPointF(34, 20));

        composition::Layer layer; layer.kind = composition::LayerKind::Text;
        composition::TextStyle style; style.text = "Hello"; style.fontSize = 60;
        style.alignH = composition::TextStyle::AlignH::Center;
        style.alignV = composition::TextStyle::AlignV::Middle;
        int frame = 0, changes = 0;
        QStringList commits;
        composition::LayerTransform committedBefore;
        ui::TextTransformOverlay overlay(
            [&] { return ui::TextTransformOverlay::Target{&layer, style, frame, QSizeF(640, 360)}; },
            [&] { ++changes; },
            [&](const composition::LayerTransform& before, const composition::LayerTransform&,
                const QString& title) { committedBefore = before; commits << title; });
        viewer.addOverlay(&overlay);
        QVERIFY(overlay.isActive());
        const auto widgetQuad = [&] {
            QPolygonF quad;
            for (const QPointF& corner : overlay.canvasQuad()) quad << viewer.mapping().toWidget(corner);
            return quad;
        };
        QPolygonF quad = widgetQuad();
        QCOMPARE(quad.size(), 4);
        const QPoint centre = quad.boundingRect().center().toPoint();
        QCOMPARE(overlay.handleAt(centre, viewer.mapping()), ui::TextTransformOverlay::Handle::Move);
        // Hovering (no button) asks the overlay for its cursor.
        QMouseEvent hover(QEvent::MouseMove, QPointF(centre), viewer.mapToGlobal(QPointF(centre)),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&viewer, &hover);
        QCOMPARE(viewer.cursor().shape(), Qt::SizeAllCursor);

        // Move: the text follows the pointer, Y up in the model.
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, centre);
        QTest::mouseMove(&viewer, centre + QPoint(50, 20));
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, centre + QPoint(50, 20));
        QCOMPARE(layer.transform.position, QPointF(50, -20));
        QCOMPARE(commits, QStringList{"Move Text"});
        QCOMPARE(committedBefore.position, QPointF(0, 0));
        QVERIFY(changes > 0);

        // Right edge: wider only, the left edge stays put.
        quad = widgetQuad();
        const QPointF right = (quad[1] + quad[2]) / 2.0;
        QCOMPARE(overlay.handleAt(right, viewer.mapping()), ui::TextTransformOverlay::Handle::Right);
        const double width = quad[1].x() - quad[0].x();
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, right.toPoint());
        QTest::mouseMove(&viewer, (right + QPointF(width, 0)).toPoint());
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, (right + QPointF(width, 0)).toPoint());
        QVERIFY2(std::abs(layer.transform.scalePercent.x() - 200.0) < 2.0,
                 qPrintable(QString::number(layer.transform.scalePercent.x())));
        QCOMPARE(layer.transform.scalePercent.y(), 100.0);
        QVERIFY(std::abs(widgetQuad()[0].x() - quad[0].x()) < 1.0);
        QCOMPARE(commits.last(), QString("Scale Text"));

        // A corner keeps the proportions.
        quad = widgetQuad();
        const QPointF corner = quad[2];
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, corner.toPoint());
        QTest::mouseMove(&viewer, (corner + (corner - quad[0]) * 0.5).toPoint());
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, (corner + (corner - quad[0]) * 0.5).toPoint());
        const QPointF scaled = layer.transform.scalePercent;
        QVERIFY(scaled.y() > 140.0);
        QVERIFY(std::abs(scaled.x() / scaled.y() - 2.0) < 0.05);

        // The knob turns the text about its origin; Shift snaps to 15 degrees.
        quad = widgetQuad();
        const QPointF top = (quad[0] + quad[1]) / 2.0;
        const QPointF knob = top + QPointF(0, -24);
        QCOMPARE(overlay.handleAt(knob, viewer.mapping()), ui::TextTransformOverlay::Handle::Rotate);
        // A quarter turn clockwise on screen about the layer origin.
        const QPointF origin = viewer.mapping().toWidget(
            QPointF(320 + layer.transform.position.x(), 180 - layer.transform.position.y()));
        const QPointF arm = knob - origin;
        const QPointF turned = origin + QPointF(-arm.y(), arm.x());
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, knob.toPoint());
        QTest::mouseMove(&viewer, turned.toPoint());
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, turned.toPoint());
        QCOMPARE(commits.last(), QString("Rotate Text"));
        QVERIFY2(std::abs(layer.transform.rotationDegrees - 90.0) < 3.0,
                 qPrintable(QString::number(layer.transform.rotationDegrees)));

        // Animated position: the drag keys the current frame.
        layer.transform = composition::LayerTransform();
        layer.transform.positionXCurve.set(0, 0.0); layer.transform.positionXCurve.set(30, 100.0);
        layer.transform.positionYCurve.set(0, 0.0); layer.transform.positionYCurve.set(30, 0.0);
        frame = 15;
        const QPoint middle = widgetQuad().boundingRect().center().toPoint();
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, middle);
        QTest::mouseMove(&viewer, middle + QPoint(0, 30));
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, middle + QPoint(0, 30));
        QVERIFY(layer.transform.positionYCurve.contains(15));
        QCOMPARE(layer.transform.positionAt(15), QPointF(50, -30));
        QCOMPARE(layer.transform.positionAt(30), QPointF(100, 0));

        // A locked layer is framed but not moved; Hand pans past the frame.
        layer.locked = true;
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, widgetQuad().boundingRect().center().toPoint());
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, widgetQuad().boundingRect().center().toPoint());
        QCOMPARE(commits.size(), 5);
        QVERIFY(viewer.grab().save(testArtifactPath(QStringLiteral("viewer-text-frame.png"))));
        viewer.removeOverlay(&overlay);
        QVERIFY(!overlay.viewer());

        // Picking: a press on text that is not selected selects it and drags
        // it in the same gesture.
        composition::Layer other; other.kind = composition::LayerKind::Text;
        other.transform.position = QPointF(-200, 100);
        composition::Layer* selected = nullptr;
        const QSizeF canvas(640, 360);
        ui::TextTransformOverlay picking(
            [&] { return ui::TextTransformOverlay::Target{selected, style, 0, canvas}; }, {},
            [&](const composition::LayerTransform&, const composition::LayerTransform&,
                const QString& title) { commits << title; },
            [&](const QPointF& at) {
                const ui::TextTransformOverlay::Target candidate{&other, style, 0, canvas};
                if (selected == &other
                    || !ui::TextTransformOverlay::quadFor(candidate).containsPoint(at, Qt::OddEvenFill)) {
                    return false;
                }
                selected = &other;
                return true;
            });
        viewer.addOverlay(&picking);
        QVERIFY(picking.isActive());
        const QPoint otherCentre = viewer.mapping().toWidget(
            ui::TextTransformOverlay::quadFor({&other, style, 0, canvas}).boundingRect().center()).toPoint();
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, otherCentre);
        QTest::mouseMove(&viewer, otherCentre + QPoint(10, 10));
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, otherCentre + QPoint(10, 10));
        QCOMPARE(selected, &other);
        QCOMPARE(other.transform.position, QPointF(-190, 90));
        QCOMPARE(commits.last(), QString("Move Text"));
    }
    // 2D effects that move by themselves get the time (RenderContext
    // +0x6c/+0x70/+0x98/+0x9c/+0xa0): Linear Wipe at frame 0 and at frame 30
    // of a 120-frame layer differ, the same frame is the same picture, and
    // past the layer's end it holds its last frame. Needs the OpenGL 4.1
    // context native modules run in.
    void nativeEffectsMoveWithTime() {
        const QString dir = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const QString module = dir + "/Plugins/2D/LinearWipe.hfpl";
        if (!QFileInfo::exists(module)) QSKIP("The reference LinearWipe.hfpl is not in SAMPLES");
        const auto metadata = plugin::loadNativePluginMetadata(module, dir);
        const core::Identifier id("test.native.linearwipe");
        plugin::registerNativeEffectModule(id, module, dir, true, metadata.parameters);
        auto cleanup = qScopeGuard([] {
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        QStringList values;
        for (const auto& parameter : metadata.parameters) values << parameter.defaultValue;
        QImage pattern(64, 64, QImage::Format_RGBA8888);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) pattern.setPixelColor(x, y, QColor(x * 4, y * 4, 128));
        const auto at = [&](int frame, bool* ok) {
            QImage image = pattern;
            plugin::NativeFrameTime time;
            time.frame = frame; time.layerFrame = frame; time.layerFrames = 120;
            *ok = plugin::applyEffectToImage(image, id, values, time);
            return image;
        };
        bool ok = false;
        const QImage start = at(0, &ok);
        if (!ok) QSKIP("No OpenGL 4.1 context for native modules on this platform");
        const QImage later = at(30, &ok);
        QVERIFY(ok);
        QVERIFY(start != later);
        QCOMPARE(at(30, &ok), later);
        QCOMPARE(at(200, &ok), at(119, &ok));
    }
    // Behaviors get their time in milliseconds, their opacity is read back
    // from the context, and they are told where their layer stands and how
    // big it is (PluginBehaviorEffect::TransformationAtTime): FadeBehavior
    // fades in over its first 25 %, DownInsert brings a layer down from the
    // top of the frame and leaves it in place.
    void nativeBehaviorsKnowTimeAndLayer() {
        const QString dir = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const QString fade = dir + "/Plugins/Behavior/FadeBehavior.hfpl";
        const QString insert = dir + "/Plugins/Behavior/DownInsert.hfpl";
        if (!QFileInfo::exists(fade) || !QFileInfo::exists(insert))
            QSKIP("The reference Behavior modules are not in SAMPLES");
        const core::Identifier fadeId("test.native.fade"), insertId("test.native.downinsert");
        const auto fadeMetadata = plugin::loadNativePluginMetadata(fade, dir);
        const auto insertMetadata = plugin::loadNativePluginMetadata(insert, dir);
        plugin::registerNativeBehaviorModule(fadeId, fade, dir, true, fadeMetadata.parameters);
        plugin::registerNativeBehaviorModule(insertId, insert, dir, true, insertMetadata.parameters);
        auto cleanup = qScopeGuard([] {
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        QStringList fadeValues;
        for (const auto& parameter : fadeMetadata.parameters) fadeValues << parameter.defaultValue;
        // 120 frames at 30 fps, revealed over 25 % - the first second.
        const auto opacityAt = [&](int frame) {
            plugin::NativeBehaviorResult result;
            if (!plugin::evaluateNativeBehaviorFrame(result, frame, frame, 120, 1920, 1080, 30.0,
                                                     fadeId, fadeValues)) {
                return -1.0f;
            }
            return result.opacity;
        };
        const float start = opacityAt(0);
        if (start < 0.0f) QSKIP("Native Behavior modules do not run on this platform");
        QVERIFY2(start < 0.01f, qPrintable(QString::number(start)));
        const float middle = opacityAt(15);
        QVERIFY2(middle > 0.5f && middle < 0.9f, qPrintable(QString::number(middle)));
        QVERIFY(opacityAt(30) > 0.99f && opacityAt(90) > 0.99f);

        // A 40x20 red plane in the middle of a 200x100 shot. A plane is a
        // frame-sized asset, 0..200 by 0..100 in its own units: at the start
        // DownInsert lifts its origin to the top edge (half of it shows);
        // from a quarter of the layer on it stands where it is.
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 100); shot->setDurationSeconds(4);
        {
            auto& plane = shot->addLayer("Plane");
            plane.kind = composition::LayerKind::Plane; plane.planeColor = Qt::red;
            plane.transform.scalePercent = QPointF(20, 20);
            composition::Effect behavior; behavior.pluginId = insertId; behavior.name = "Down Insert";
            for (const auto& parameter : insertMetadata.parameters) behavior.parameterValues << parameter.defaultValue;
            composition::Clip clip; clip.durationSeconds = 4; clip.effects.append(behavior);
            plane.clips.append(clip);
        }
        render::RenderManager manager; manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        const auto frameAt = [&](double seconds) {
            frames.clear();
            manager.requestFrame(0, seconds, QSize(200, 100));
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000)) return QImage();
            const QByteArray bytes = frames.last()[1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 200, 100, 200 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const auto red = [](const QImage& image, int x, int y) {
            const QColor c = image.pixelColor(x, y);
            return c.red() > 200 && c.green() < 60;
        };
        const QImage first = frameAt(0.0);
        QVERIFY(!first.isNull());
        QVERIFY(red(first, 100, 5) && !red(first, 100, 50) && !red(first, 100, 30));
        const QImage later = frameAt(2.0);
        QVERIFY(!later.isNull());
        QVERIFY(red(later, 100, 50) && red(later, 85, 45) && !red(later, 100, 5));
    }
    // MotionTrack moves its layer by matrices its own instance gives on the
    // GUI thread: the host places the tracked footage for the viewer, turns
    // the module's motion into the renderer's terms, the renderer applies
    // them, and the instance's data survives save and load as <InstanceBytes>.
    void motionTrackInstanceMovesItsLayer() {
        const QString module = QStringLiteral(OPENVEGAS_SOURCE_DIR)
            + "/SAMPLES/VEGAS_Effects/Plugins/Behavior/MotionTrack.hfpl";
        if (!QFileInfo::exists(module)) QSKIP("The reference MotionTrack.hfpl is not in SAMPLES");
        const QString dependencies = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const auto metadata = plugin::loadNativePluginMetadata(module, dependencies);
        const core::Identifier id("test.native.motiontrack.instance");
        plugin::registerNativeBehaviorModule(id, module, dependencies, false, metadata.parameters);
        auto cleanup = qScopeGuard([] {
            plugin::clearNativeInstanceTransforms();
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        QVERIFY(plugin::nativeBehaviorUsesInstance(id));
        QVERIFY(!plugin::nativeBehaviorUsesInstance(core::Identifier("test.not.registered")));

        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 100); shot->setDurationSeconds(1);
        // The footage: a frame-sized plane at half size, 20 px right and
        // 10 px up - on the canvas x 70..170, y 15..65. Hidden: only its
        // placement matters.
        QString footageId;
        {
            auto& footage = shot->addLayer("Footage");
            footage.kind = composition::LayerKind::Plane; footage.visible = false;
            footage.transform.position = QPointF(20, 10);
            footage.transform.scalePercent = QPointF(50, 50);
            composition::Clip still; still.durationSeconds = 1; footage.clips.append(still);
            footageId = footage.id.value();
        }
        // The tracked layer: a 20x20 red plane 30 px right of the centre.
        composition::Effect motion; motion.pluginId = id; motion.name = "Motion Track";
        for (const auto& parameter : metadata.parameters) {
            motion.parameterValues << (parameter.name == QLatin1String("motionFromLayer")
                                           ? footageId : parameter.defaultValue);
        }
        motion.instanceData = QByteArray("RTOM\x01\x00\x02\xff", 8);
        core::Identifier trackedId;
        {
            auto& tracked = shot->addLayer("Tracked");
            tracked.kind = composition::LayerKind::Plane; tracked.planeColor = Qt::red;
            tracked.transform.position = QPointF(30, 0);
            tracked.transform.scalePercent = QPointF(10, 20);
            composition::Clip clip; clip.durationSeconds = 1; clip.effects.append(motion);
            tracked.clips.append(clip);
            trackedId = tracked.id;
        }

        // The view the viewer and the module share: the footage's pixels,
        // placed on the canvas as the footage layer is.
        ui::NativeInstanceHost host([shot] { return shot; },
                                    [] { return std::shared_ptr<media::MediaManager>(); });
        const ui::NativeInstanceHost::Ref ref {trackedId, 0, 0};
        const QString key = ui::NativeInstanceHost::instanceKey(ref);
        QCOMPARE(key, trackedId.value() + "/0/0");
        core::Identifier pluginId; QStringList values; plugin::NativeCustomUiView view;
        QVERIFY(host.describe(ref, 0.0, &pluginId, &values, &view));
        QCOMPARE(pluginId, id);
        QCOMPARE(view.instanceKey, key);
        QCOMPARE(view.area, QSize(200, 100));
        QCOMPARE(view.target, QSize(200, 100));
        const QTransform placed(view.matrix[0], view.matrix[1], view.matrix[4], view.matrix[5],
                                view.matrix[12], view.matrix[13]);
        QCOMPARE(placed.map(QPointF(0, 0)), QPointF(70, 15));
        QCOMPARE(placed.map(QPointF(200, 100)), QPointF(170, 65));

        // The module's motion (its result already Y up): 10 px right and 4 px
        // down in the footage is half that on the canvas.
        const std::array<float, 16> shift {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, -4, 0, 1};
        const auto shifted = ui::NativeInstanceHost::canvasMotion(shift, placed, QSize(200, 100));
        QCOMPARE(shifted[0], 1.0f); QCOMPARE(shifted[5], 1.0f);
        QCOMPARE(shifted[12], 5.0f); QCOMPARE(shifted[13], -2.0f);

        // A quarter turn clockwise on screen about the footage's centre, for
        // footage filling the frame: the plane right of the centre ends up
        // below it - x 90..110, y 70..90.
        QTransform turn; turn.translate(100, 50); turn.rotate(90); turn.translate(-100, -50);
        const QTransform flip = QTransform::fromScale(1, -1);
        const QTransform up = flip * turn * flip;
        const std::array<float, 16> quarter {float(up.m11()), float(up.m12()), 0, 0,
                                             float(up.m21()), float(up.m22()), 0, 0, 0, 0, 1, 0,
                                             float(up.dx()), float(up.dy()), 0, 1};
        plugin::setNativeInstanceTransforms(
            key, 0, QVector<std::array<float, 16>>(30, ui::NativeInstanceHost::canvasMotion(
                                                           quarter, QTransform(), QSize(200, 100))));
        render::RenderManager manager; manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 0, QSize(200, 100));
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 5000);
        const QByteArray bytes = frames[0][1].toByteArray();
        const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 200, 100, 200 * 4,
                           QImage::Format_RGBA8888);
        const auto red = [&](int x, int y) {
            const QColor c = image.pixelColor(x, y);
            return c.red() > 200 && c.green() < 60;
        };
        QVERIFY(red(100, 80) && red(93, 73) && red(107, 87));
        QVERIFY(!red(130, 50) && !red(100, 20));

        // The instance's data goes into the project and comes back.
        QTemporaryDir temp; QVERIFY(temp.isValid()); media::MediaManager media;
        const QString path = temp.filePath("motiontrack.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, media).isSuccess());
        composition::Composition loaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &media).isSuccess());
        QCOMPARE(loaded.layers().size(), 2);
        QCOMPARE(loaded.layers()[1].clips.size(), 1);
        QCOMPARE(loaded.layers()[1].clips[0].effects.size(), 1);
        QCOMPARE(loaded.layers()[1].clips[0].effects[0].instanceData, motion.instanceData);
        QCOMPARE(loaded.layers()[0].clips[0].effects.size(), 0);
    }
    // The whole of MotionTrack as the application drives it: "Motion From"
    // set in the inspector, the footage analysed in the background from the
    // media, the lasso drawn in the viewer, then matrices for every frame -
    // and the same matrices again from the effect's saved data alone. Needs
    // the OpenGL 4.1 context native modules run in.
    void motionTrackHostTracksFootage() {
        const QString module = QStringLiteral(OPENVEGAS_SOURCE_DIR)
            + "/SAMPLES/VEGAS_Effects/Plugins/Behavior/MotionTrack.hfpl";
        if (!QFileInfo::exists(module)) QSKIP("The reference MotionTrack.hfpl is not in SAMPLES");
        const QString dependencies = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const auto metadata = plugin::loadNativePluginMetadata(module, dependencies);
        const core::Identifier id("test.native.motiontrack.host");
        plugin::registerNativeBehaviorModule(id, module, dependencies, false, metadata.parameters);
        auto cleanup = qScopeGuard([] {
            plugin::clearNativeInstanceTransforms();
            plugin::clearNativeControlStates();
            plugin::clearNativeTrackedFeatures();
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        int sourceIndex = -1, statusIndex = -1;
        QStringList defaults;
        for (int i = 0; i < metadata.parameters.size(); ++i) {
            defaults << metadata.parameters.at(i).defaultValue;
            if (metadata.parameters.at(i).name == QLatin1String("motionFromLayer")) sourceIndex = i;
            if (metadata.parameters.at(i).name == QLatin1String("analysisStatus")) statusIndex = i;
        }
        QVERIFY(sourceIndex >= 0 && statusIndex >= 0);
        {
            plugin::NativeCustomUiView probe;
            probe.area = QSize(64, 64);
            probe.instanceKey = QStringLiteral("gl-check");
            if (!plugin::nativeCustomUiSetup(id, defaults, probe).handled)
                QSKIP("No OpenGL 4.1 context for native modules on this platform");
            plugin::nativeCustomUiShutdown(id, defaults, probe);
        }

        // Footage: 30 frames of blurred noise drifting 2 px right and 1 px
        // down a frame, as an image sequence.
        constexpr int kFrames = 30;
        const QSize size(320, 180);
        QImage texture(size.width() + 2 * kFrames + 8, size.height() + kFrames + 8,
                       QImage::Format_RGBA8888);
        quint32 seed = 12345u;
        for (int y = 0; y < texture.height(); ++y) {
            for (int x = 0; x < texture.width(); ++x) {
                seed = seed * 1664525u + 1013904223u;
                const int v = int((seed >> 24) & 0xff);
                texture.setPixelColor(x, y, QColor(v, v, v));
            }
        }
        texture = texture.scaled(texture.width() / 4, texture.height() / 4, Qt::IgnoreAspectRatio,
                                 Qt::FastTransformation)
                      .scaled(texture.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QStringList files;
        for (int f = 0; f < kFrames; ++f) {
            files << temp.filePath(QStringLiteral("frame%1.png").arg(f, 3, 10, QLatin1Char('0')));
            QVERIFY(texture.copy(QRect(QPoint(2 * kFrames - 2 * f, kFrames - f), size)).save(files.last()));
        }
        auto media = std::make_shared<media::MediaManager>();
        QString assetPath;
        QVERIFY(media->importImageSequence(files, 30.0, &assetPath).isSuccess());
        const media::MediaAsset asset = media->assetByFilePath(assetPath);
        QVERIFY(asset.isValid());

        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(size.width(), size.height()); shot->setDurationSeconds(1);
        QString footageId;
        {
            auto& footage = shot->addLayer("Footage");
            composition::Clip clip; clip.mediaId = asset.id(); clip.durationSeconds = 1;
            footage.clips.append(clip);
            footageId = footage.id.value();
        }
        core::Identifier trackedId;
        {
            auto& tracked = shot->addLayer("Tracked");
            tracked.kind = composition::LayerKind::Plane; tracked.planeColor = Qt::red;
            tracked.transform.scalePercent = QPointF(10, 10);
            composition::Effect motion; motion.pluginId = id; motion.name = "Motion Track";
            motion.parameterValues = defaults;
            composition::Clip clip; clip.durationSeconds = 1; clip.effects.append(motion);
            tracked.clips.append(clip);
            trackedId = tracked.id;
        }
        const auto effect = [&]() -> const composition::Effect& {
            return shot->layers()[1].clips[0].effects[0];
        };
        ui::NativeInstanceHost host([shot] { return shot; }, [media] { return media; });
        const ui::NativeInstanceHost::Ref ref {trackedId, 0, 0};
        const QString key = ui::NativeInstanceHost::instanceKey(ref);

        // "Motion From" chosen in the inspector: the footage is analysed.
        shot->layerRef(1).clips[0].effects[0].parameterValues[sourceIndex] = footageId;
        host.propertyChanged(ref, sourceIndex, 0.0);
        QTRY_VERIFY_WITH_TIMEOUT(effect().parameterValues.value(statusIndex).contains(QLatin1String("Draw")),
                                 120000);
        QVERIFY(!host.isBusy());

        // The lasso around the middle, in canvas pixels (the footage fills
        // the frame unmoved), closed by crossing its start.
        const QVector<QPoint> path {{100, 50}, {160, 50}, {220, 50}, {220, 90}, {220, 130},
                                    {160, 130}, {100, 130}, {100, 90}, {100, 60}, {130, 40}};
        const auto send = [&](plugin::NativeCustomUiMouse type, const QPoint& at, bool pressed) {
            core::Identifier pluginId; QStringList values; plugin::NativeCustomUiView view;
            QVERIFY(host.describe(ref, 0.0, &pluginId, &values, &view));
            QCOMPARE(view.area, size);
            plugin::NativeCustomUiPointer pointer;
            pointer.position = at; pointer.pressed = pressed; pointer.button = 1;
            pointer.buttons = pressed ? 1 : 0; pointer.clicks = 1;
            host.handleResult(ref, plugin::nativeCustomUiMouse(pluginId, values, view, type, pointer));
        };
        send(plugin::NativeCustomUiMouse::Press, path.first(), true);
        for (const QPoint& point : path) send(plugin::NativeCustomUiMouse::Move, point, true);
        send(plugin::NativeCustomUiMouse::Release, path.last(), false);
        QVERIFY(host.isBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!host.isBusy(), 120000);
        QVERIFY(!effect().instanceData.isEmpty());
        // With the transform calculated the module switches its status off
        // (SetPropertyState) and its options on.
        QVERIFY(!plugin::nativeControlShown(key, QStringLiteral("analysisStatus")));
        QVERIFY(plugin::nativeControlShown(key, QStringLiteral("positionX")));

        // The plane follows the footage: 58 px right and 29 px down (Y up)
        // by the last frame.
        std::array<float, 16> matrix {};
        QVERIFY(plugin::nativeInstanceTransformAt(key, kFrames - 1, &matrix));
        QVERIFY2(qAbs(matrix[12] - 58.0f) < 1.5f && qAbs(matrix[13] + 29.0f) < 1.5f,
                 qPrintable(QStringLiteral("%1, %2").arg(matrix[12]).arg(matrix[13])));
        QVERIFY(plugin::nativeInstanceTransformAt(key, 0, &matrix));
        QVERIFY(qAbs(matrix[12]) < 1.5f && qAbs(matrix[13]) < 1.5f);

        // A new instance gets the same motion from the data alone.
        plugin::clearNativeInstanceTransforms();
        plugin::releaseNativeEffectThreadRenderer();
        host.restoreAll();
        QVERIFY(plugin::nativeInstanceTransformAt(key, kFrames - 1, &matrix));
        QVERIFY2(qAbs(matrix[12] - 58.0f) < 1.5f && qAbs(matrix[13] + 29.0f) < 1.5f,
                 qPrintable(QStringLiteral("%1, %2").arg(matrix[12]).arg(matrix[13])));
    }
    void viewerCustomUiReachesNativeModule() {
        // Key and cursor tables the bridge hands to modules.
        QCOMPARE(plugin::nativeKeysym(Qt::Key_Control, QString()), quint32(0xffe3));
        QCOMPARE(plugin::nativeKeysym(Qt::Key_Alt, QString()), quint32(0xffe9));
        QCOMPARE(plugin::nativeKeysym(Qt::Key_A, QStringLiteral("A")), quint32('a'));
        QCOMPARE(plugin::nativeKeysym(Qt::Key_F1, QString()), quint32(0xffbe));
        QCOMPARE(plugin::nativeCursorShape(4), int(Qt::PointingHandCursor));
        QCOMPARE(plugin::nativeCursorShape(20), int(Qt::OpenHandCursor));
        QCOMPARE(plugin::nativeCursorShape(0), -1);

        const QString module = QStringLiteral(OPENVEGAS_SOURCE_DIR)
            + "/SAMPLES/VEGAS_Effects/Plugins/Behavior/MotionTrack.hfpl";
        if (!QFileInfo::exists(module)) QSKIP("The reference MotionTrack.hfpl is not in SAMPLES");
        const QString dependencies = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const auto metadata = plugin::loadNativePluginMetadata(module, dependencies);
        const core::Identifier id("test.native.motiontrack");
        plugin::registerNativeBehaviorModule(id, module, dependencies, false, metadata.parameters);
        auto cleanup = qScopeGuard([] {
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        QVERIFY(plugin::nativeEffectHasCustomUi(id));
        QVERIFY(!plugin::nativeEffectHasCustomUi(core::Identifier("test.not.registered")));
        QStringList values;
        for (const auto& parameter : metadata.parameters) values << parameter.defaultValue;
        plugin::NativeCustomUiView view;
        view.area = QSize(640, 360);
        const auto setup = plugin::nativeCustomUiSetup(id, values, view);
        if (!setup.handled) QSKIP("No OpenGL 4.1 context for native modules on this platform");
        // MotionTrack keeps Ctrl/Alt state from the keysym at event+0x10 and
        // asks for a redraw each time.
        const auto ctrl = plugin::nativeCustomUiKey(id, values, view, plugin::NativeCustomUiKey::Press,
                                                    0xffe3, QString());
        QVERIFY(ctrl.redraw);
        QVERIFY(plugin::nativeCustomUiKey(id, values, view, plugin::NativeCustomUiKey::Release,
                                          0xffe3, QString()).redraw);
        plugin::NativeCustomUiPointer pointer; pointer.position = QPoint(320, 180);
        pointer.pressed = true; pointer.button = 1; pointer.buttons = 1;
        QVERIFY(plugin::nativeCustomUiMouse(id, values, view, plugin::NativeCustomUiMouse::Press,
                                            pointer).handled);
        plugin::NativeCustomUiResult drawn;
        const QImage overlay = plugin::nativeCustomUiRender(id, values, view, &drawn);
        QVERIFY(drawn.handled);
        QCOMPARE(overlay.size(), QSize(640, 360));
        QVERIFY(plugin::nativeCustomUiShutdown(id, values, view).handled);

        // In the Viewer the module's UI takes the Select tool's drag.
        ui::ViewerWidget viewer;
        viewer.resize(664, 360); viewer.show(); QTest::qWait(20);
        viewer.setProjectSize(QSize(640, 360));
        viewer.setFrame(QImage(640, 360, QImage::Format_ARGB32_Premultiplied));
        ui::NativeCustomUiOverlay custom([&] {
            ui::NativeCustomUiOverlay::Target target;
            target.effectId = id; target.instanceKey = "layer/0/0"; target.values = values;
            target.view = view;
            return target;
        });
        viewer.addOverlay(&custom);
        QVERIFY(custom.isActive());
        QSignalSpy moved(&viewer, &ui::ViewerWidget::layerMoved);
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, QPoint(300, 150));
        QTest::mouseMove(&viewer, QPoint(330, 170));
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, QPoint(330, 170));
        QCOMPARE(moved.count(), 0);
        QCOMPARE(custom.activeEffect(), id);
        viewer.grab();   // paints through CustomUIRender
        viewer.removeOverlay(&custom);
    }
    void viewer360ModeIsTheSameViewer() {
        QImage equirect(400, 200, QImage::Format_ARGB32);
        for (int y = 0; y < equirect.height(); ++y)
            for (int x = 0; x < equirect.width(); ++x)
                equirect.setPixel(x, y, qRgb(x * 255 / 399, y * 255 / 199, 80));
        ui::ViewerWidget viewer;
        viewer.resize(424, 300); viewer.show(); QTest::qWait(20);
        viewer.setProjectSize(QSize(400, 200));
        viewer.setFrame(equirect);
        ui::Viewer360View sphere;
        QSignalSpy modes(&viewer, &ui::ViewerWidget::sphericalModeChanged);
        viewer.setSphericalView(&sphere);
        QVERIFY(viewer.isSpherical()); QCOMPARE(modes.count(), 1);

        // The view's middle looks at the frame's middle for Front, and the
        // mapping goes both ways through the lens.
        const ui::ViewerMapping map = viewer.mapping();
        QVERIFY(map.isSpherical());
        const QPointF middle = map.viewRect.center();
        const QPointF looked = map.toCanvas(middle);
        QVERIFY(std::abs(looked.x() - 200.0) < 1.0 && std::abs(looked.y() - 100.0) < 1.0);
        bool visible = false;
        const QPointF back = map.toWidget(QPointF(250, 70), &visible);
        QVERIFY(visible);
        const QPointF again = map.toCanvas(back);
        QVERIFY(std::abs(again.x() - 250.0) < 0.5 && std::abs(again.y() - 70.0) < 0.5);
        map.toWidget(QPointF(0, 100), &visible);
        QVERIFY(!visible);   // straight behind

        const QImage shown = viewer.grab().toImage();
        const QColor centre = shown.pixelColor(middle.toPoint());
        QVERIFY(std::abs(centre.red() - 128) < 12);

        // Select drags look around; the wheel is the lens; arrows turn; Home
        // faces front; the Text tool places text where the lens points.
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, middle.toPoint());
        QTest::mouseMove(&viewer, middle.toPoint() + QPoint(40, 0));
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, middle.toPoint() + QPoint(40, 0));
        QVERIFY(sphere.yaw() > 5.0);
        QCOMPARE(sphere.currentView(), ui::Viewer360View::View::Custom);
        const double fov = sphere.fieldOfView();
        QWheelEvent wheel(middle, viewer.mapToGlobal(middle), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&viewer, &wheel);
        QCOMPARE(sphere.fieldOfView(), fov - 5.0);
        QTest::keyClick(&viewer, Qt::Key_Home);
        QCOMPARE(sphere.currentView(), ui::Viewer360View::View::Front);
        QTest::keyClick(&viewer, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(sphere.yaw(), 10.0);
        QTest::keyClick(&viewer, Qt::Key_Home);
        viewer.setTool(ui::ViewerWidget::ViewerTool::Text);
        QSignalSpy placed(&viewer, &ui::ViewerWidget::textCreationRequested);
        QTest::mouseClick(&viewer, Qt::LeftButton, Qt::NoModifier, middle.toPoint());
        QCOMPARE(placed.count(), 1);
        const QPointF at = placed[0][0].toPointF();
        QVERIFY(std::abs(at.x() - 200.0) <= 1.0 && std::abs(at.y() - 100.0) <= 1.0);
        QVERIFY(viewer.grab().save(testArtifactPath(QStringLiteral("viewer-360-mode.png"))));

        // A text frame does not draw over the lens.
        composition::Layer layer;
        composition::TextStyle style; style.text = "360";
        ui::TextTransformOverlay overlay(
            [&] { return ui::TextTransformOverlay::Target{&layer, style, 0, QSizeF(400, 200)}; },
            {}, {});
        viewer.addOverlay(&overlay);
        QVERIFY(!overlay.isActive());
        viewer.setSphericalView(nullptr);
        QVERIFY(!viewer.isSpherical()); QVERIFY(overlay.isActive());

        // The 360 Viewer panel takes the Viewer page in place of its own canvas.
        ui::Preview360VideoPanel panel;
        panel.resize(420, 320); panel.show(); QTest::qWait(10);
        QWidget page;
        QVERIFY(!panel.hostsViewer());
        panel.hostViewer(&page);
        QVERIFY(panel.hostsViewer());
        QVERIFY(page.isVisible()); QVERIFY(!panel.videoWidget()->isVisible());
        QCOMPARE(panel.videoWidget()->view(), panel.view());
        QCOMPARE(panel.releaseViewer(), &page);
        QVERIFY(!panel.hostsViewer()); QVERIFY(panel.videoWidget()->isVisible());
        page.setParent(nullptr);
    }
    void controlsColorWheels() {
        plugins.effect = plugin::timelineBuiltinSpecs()[1];
        auto& effect = comp->layerRef(0).clips[0].effects[0];
        effect.pluginId = plugins.effect.id; effect.name = plugins.effect.displayName;
        effect.parameterValues.clear();
        for (const auto& parameter : plugins.effect.parameters) effect.parameterValues.append(parameter.defaultValue);
        auto controls = std::make_unique<ui::EffectInspector>();
        controls->resize(620, 780); controls->setUndoStack(&history);
        controls->setPluginManager(&plugins); controls->setComposition(comp); controls->setSelection(0, 0);
        controls->show(); QTest::qWait(30);
        auto* wheels = controls->findChild<QWidget*>("controlsColorWheels"); QVERIFY(wheels); QVERIFY(wheels->isVisible());
        auto* highlights = controls->findChild<QWidget*>("controlsColorWheel_1"); QVERIFY(highlights);
        QTest::mouseClick(highlights, Qt::LeftButton, Qt::NoModifier, QPoint(highlights->width() - 8, highlights->height() / 2));
        QVERIFY(comp->layers()[0].clips[0].effects[0].parameterValues[5].toDouble() > .8);
        QCOMPARE(history.count(), 1); history.undo(); QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[5], QString("0"));
        history.redo();
        QVERIFY(controls->grab().save(testArtifactPath(QStringLiteral("controls-color-wheels.png"))));
    }
    void controlsFollowsPlayheadClip() {
        auto second = comp->layers()[0].clips[0];
        comp->layerRef(0).clips[0].durationSeconds = 25;
        second.startSeconds = 25; second.durationSeconds = 25;
        second.effects[0].parameterValues[0] = "25";
        comp->layerRef(0).clips.append(second);
        auto controls = std::make_unique<ui::EffectInspector>();
        controls->setPluginManager(&plugins); controls->setComposition(comp);
        controls->setCurrentTime(30); controls->setSelection(0, -1);
        QVERIFY(controls->findChild<QWidget*>("controlsParam_0_1_0_0"));
        controls->setCurrentTime(5); QTest::qWait(5);
        auto* first = controls->findChild<QWidget*>("controlsParam_0_0_0_0"); QVERIFY(first);
        QCOMPARE(first->findChild<QDoubleSpinBox*>()->value(), 0.);
    }
    void editAndUndo() {
        auto* radius = editor<QDoubleSpinBox>(0); QVERIFY(radius);
        radius->setValue(9.48); QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[0], QString("9.48"));
        QCOMPARE(history.count(), 1); history.undo(); QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[0], QString("0"));
        history.redo(); QCOMPARE(editor<QDoubleSpinBox>(0)->value(), 9.48);
        editor<QComboBox>(2)->setCurrentIndex(2); QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[2], QString("Vertical"));
        editor<QCheckBox>(3)->click(); QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[3], QString("false"));
    }
    void searchAndExpansion() {
        auto* layer = tree()->topLevelItem(0); layer->child(3)->setExpanded(true);
        tree()->setCurrentItem(parameter(0));
        panel->refreshKeyFrames(); QVERIFY(tree()->topLevelItem(0)->child(3)->isExpanded()); QVERIFY(parameter(0)->isSelected());
        auto* search = panel->findChild<QLineEdit*>("timelineSearch"); search->setText("Radius");
        QVERIFY(!parameter(0)->isHidden()); QVERIFY(parameter(1)->isHidden());
        search->clear(); QVERIFY(!parameter(1)->isHidden());
        search->setFocus(); QTest::keyClicks(search, "shade"); QCOMPARE(search->text(), QString("shade")); QCOMPARE(panel->tool(), ui::EditorTool::Select);
    }
    void numericAnimation() {
        tree()->setCurrentItem(parameter(0)); panel->toggleSelectedKeyFrame();
        QVERIFY(comp->layers()[0].clips[0].effects[0].isAnimated(0));
        panel->setPlayheadPosition(1); editor<QDoubleSpinBox>(0)->setValue(10);
        auto& curve = comp->layerRef(0).clips[0].effects[0].animation[0];
        QCOMPARE(curve.valueAt(15).toDouble(), 5.);
        panel->setPlayheadPosition(0); panel->goToNextKeyFrame(); QCOMPARE(panel->playhead(), 1.);
        auto* hold = panel->findChild<QToolButton*>("timelineKeyFrameTypekey-frame-hold"); QVERIFY(hold); QVERIFY(hold->isEnabled()); hold->click();
        QCOMPARE(comp->layers()[0].clips[0].effects[0].animation[0].at(30)->temporal, composition::TemporalType::Hold);
        panel->goToPreviousKeyFrame(); QCOMPARE(panel->playhead(), 0.);
        panel->setPlayheadPosition(0.5); QCOMPARE(editor<QDoubleSpinBox>(0)->value(), 5.);
    }
    void layerHeaderMatchesReferenceRow() {
        comp->addLayer("Title").kind = composition::LayerKind::Text;
        comp->addLayer("Child");
        panel->setComposition(comp); QTest::qWait(20);
        QVERIFY(tree()->topLevelItem(0)->data(0, Qt::UserRole + 5).toBool());
        // "N. name [Kind]" until double-clicked, then the bare name.
        auto* name = panel->findChild<QLineEdit*>("timelineLayerName_0");
        QVERIFY(name); QVERIFY(name->isReadOnly());
        QCOMPARE(name->text(), QString("1. shot_1.mp4 [Media]"));
        QCOMPARE(panel->findChild<QLineEdit*>("timelineLayerName_1")->text(), QString("2. Title [Text]"));
        QTest::mouseDClick(name, Qt::LeftButton);
        QVERIFY(!name->isReadOnly()); QCOMPARE(name->text(), QString("shot_1.mp4"));
        name->setText("Ignored"); QTest::keyClick(name, Qt::Key_Escape);
        QVERIFY(name->isReadOnly()); QCOMPARE(comp->layers()[0].name, QString("shot_1.mp4"));
        QTest::mouseDClick(name, Qt::LeftButton);
        name->setText("Renamed"); QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(comp->layers()[0].name, QString("Renamed"));
        QTest::qWait(20);
        QCOMPARE(panel->findChild<QLineEdit*>("timelineLayerName_0")->text(), QString("1. Renamed [Media]"));
        history.undo(); QCOMPARE(comp->layers()[0].name, QString("shot_1.mp4"));
        QTest::qWait(20);
        // Motion blur and dimension toggles swap their icon, not a background.
        auto* blur = panel->findChild<QToolButton*>("timelineMotionBlur_0"); QVERIFY(blur);
        blur->click(); QVERIFY(comp->layers()[0].motionBlur);
        auto* dimension = panel->findChild<QToolButton*>("timelineDimension_0"); QVERIFY(dimension);
        answerPrompt("Adding3DCameras", QMessageBox::Yes, nullptr);   // the shot has no camera yet
        dimension->click(); QCOMPARE(comp->layers()[0].dimension, composition::LayerDimension::ThreeD);
        QCOMPARE(comp->layers().last().kind, composition::LayerKind::Camera);
        QTest::qWait(20);
        // Parent picker: "None" plus every layer that would not form a loop.
        auto* childParent = panel->findChild<QComboBox*>("timelineParent_2"); QVERIFY(childParent);
        QCOMPARE(childParent->itemText(0), QString("None"));
        QCOMPARE(childParent->count(), 4);   // None, the two other layers, the camera
        const int top = childParent->findData(0); QVERIFY(top > 0);
        childParent->setCurrentIndex(top); emit childParent->activated(top);
        QCOMPARE(comp->layers()[2].parentLayerId, comp->layers()[0].id);
        QTest::qWait(20);
        auto* topParent = panel->findChild<QComboBox*>("timelineParent_0");
        QCOMPARE(topParent->findData(2), -1);   // the child cannot become its parent's parent
        QVERIFY(topParent->findData(1) > 0);
        QCOMPARE(panel->findChild<QComboBox*>("timelineParent_2")->currentIndex(),
                 panel->findChild<QComboBox*>("timelineParent_2")->findData(0));
        // Masks come from the Viewer's mask tools; the group has no "+".
        QVERIFY(!panel->findChild<QToolButton*>("timelineAddMask_0"));
        QVERIFY(panel->findChild<QToolButton*>("timelineAddEffect_0"));
        // An effect is listed collapsed under the open Effects group.
        auto* effects = tree()->topLevelItem(0)->child(2);
        QVERIFY(effects->isExpanded()); QVERIFY(effects->childCount() > 0);
        QVERIFY(!effects->child(0)->isExpanded());
    }
    void threeDLayersNeedACamera() {
        // CompositionAsset::Is3D: a shot is 3D exactly when it holds a camera.
        QVERIFY(!ui::compositionIs3D(*comp));
        const bool asked = ui::promptEnabled("Adding3DCameras");
        int shown = 0;
        if (asked) {
            // Cancel leaves everything as it was.
            answerPrompt("Adding3DCameras", QMessageBox::Cancel, &shown);
            panel->setLayerDimension(0, composition::LayerDimension::ThreeD);
            QCOMPARE(shown, 1);
            QCOMPARE(comp->layers().size(), 1);
            QCOMPARE(comp->layers()[0].dimension, composition::LayerDimension::TwoD);
            QCOMPARE(history.count(), 0);
        }
        // Yes adds the camera and the switch as one step of History.
        answerPrompt("Adding3DCameras", QMessageBox::Yes, &shown);
        panel->setLayerDimension(0, composition::LayerDimension::ThreeD);
        QCOMPARE(shown, asked ? 2 : 0);
        QCOMPARE(comp->layers().size(), 2);
        QCOMPARE(comp->layers()[0].dimension, composition::LayerDimension::ThreeD);
        const composition::Layer camera = comp->layers()[1];
        QCOMPARE(camera.kind, composition::LayerKind::Camera);
        QVERIFY(ui::compositionIs3D(*comp));
        // It stands where the default view looks from, with the same lens.
        const model3d::Camera standIn = model3d::defaultCameraForCanvas(QSize(comp->width(), comp->height()));
        QCOMPARE(camera.transform.positionZ, double(standIn.position.z()));
        QCOMPARE(camera.cameraFieldOfView, standIn.fieldOfViewDegrees);
        QCOMPARE(history.count(), 1);
        QCOMPARE(history.undoText(), QString("Set Layer Dimension(s)"));
        history.undo();
        QCOMPARE(comp->layers().size(), 1);
        QCOMPARE(comp->layers()[0].dimension, composition::LayerDimension::TwoD);
        history.redo();
        QCOMPARE(comp->layers().size(), 2);
        QCOMPARE(comp->layers()[0].dimension, composition::LayerDimension::ThreeD);
        // A shot that is already 3D asks nothing more.
        comp->addLayer("Plain").clips.append(comp->layers()[0].clips[0]);
        panel->setComposition(comp); QTest::qWait(20);
        answerPrompt("Adding3DCameras", QMessageBox::Cancel, &shown);
        const int before = shown;
        panel->setLayerDimension(2, composition::LayerDimension::ThreeD);
        QCoreApplication::processEvents();
        QCOMPARE(shown, before);
        QCOMPARE(comp->layers().size(), 3);
        QCOMPARE(comp->layers()[2].dimension, composition::LayerDimension::ThreeD);
        // Lights and models need the camera too; the camera does not.
        composition::Layer light; light.kind = composition::LayerKind::Light;
        composition::Layer model; model.kind = composition::LayerKind::Model3D;
        QVERIFY(ui::layerNeedsCamera(light)); QVERIFY(ui::layerNeedsCamera(model));
        QVERIFY(!ui::layerNeedsCamera(camera)); QVERIFY(!ui::layerNeedsCamera(composition::Layer()));
        // Remove Layer(s): only taking every camera while something stays asks.
        QVERIFY(ui::removesLastCamera(*comp, {1}));
        QVERIFY(!ui::removesLastCamera(*comp, {0}));
        QVERIFY(!ui::removesLastCamera(*comp, {0, 1, 2}));
        comp->insertLayer(comp->layers().size(), camera);
        QVERIFY(!ui::removesLastCamera(*comp, {1}));       // the other camera stays
        // Once the cameras are gone: lights removed, 3D layers back to 2D,
        // a model (which cannot be 2D here) kept.
        composition::Composition shot;
        shot.addLayer("Flat");
        shot.addLayer("Deep").dimension = composition::LayerDimension::ThreeD;
        shot.insertLayer(shot.layers().size(), light);
        model.dimension = composition::LayerDimension::ThreeD;
        shot.insertLayer(shot.layers().size(), model);
        QVERIFY(ui::convertTo2D(shot));
        QCOMPARE(shot.layers().size(), 3);
        QCOMPARE(shot.layers()[1].dimension, composition::LayerDimension::TwoD);
        QCOMPARE(shot.layers()[2].kind, composition::LayerKind::Model3D);
        QVERIFY(!ui::convertTo2D(shot));
        // The Layer panel's Dimension asks the same question in its own record.
        auto loose = std::make_shared<composition::Composition>();
        loose->addLayer("Solo");
        QUndoStack stack;
        ui::LayerPanel layerPanel; layerPanel.setUndoStack(&stack);
        layerPanel.bindModel(loose); layerPanel.setSelection(0);
        auto* combo = layerPanel.findChild<QComboBox*>("layerDimension"); QVERIFY(combo);
        answerPrompt("Adding3DCameras", QMessageBox::Yes, &shown);
        combo->setCurrentIndex(combo->findData(int(composition::LayerDimension::ThreeD)));
        QCOMPARE(loose->layers().size(), 2);
        QCOMPARE(loose->layers()[0].dimension, composition::LayerDimension::ThreeD);
        QCOMPARE(stack.count(), 1);
        stack.undo(); QCOMPARE(loose->layers().size(), 1);
        QCOMPARE(loose->layers()[0].dimension, composition::LayerDimension::TwoD);
    }
    void layerBaseFlagsUseNativeElements() {
        comp->addLayer("Child").clips.append(comp->layers()[0].clips[0]);
        comp->layerRef(0).motionBlur = true; comp->layerRef(0).muted = true; comp->layerRef(0).locked = true;
        comp->layerRef(1).parentLayerId = comp->layers()[0].id;
        QTemporaryDir temp; media::MediaManager media;
        const QString path = temp.filePath("flags.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *comp, media).isSuccess());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QString xml = QString::fromUtf8(file.readAll()); file.close();
        QVERIFY(xml.contains("<MotionBlurOn>1</MotionBlurOn>"));
        QVERIFY2(xml.contains("<ParentLayerID>" + comp->layers()[0].id.value() + "</ParentLayerID>"),
                 qPrintable(comp->layers()[0].id.value() + " | " + xml.mid(xml.indexOf("<ParentLayerID>"), 400)));
        QVERIFY(xml.contains("<ParentLayerID>00000000-0000-0000-0000-000000000000</ParentLayerID>"));
        // A file written by the reference has no OpenVegas* extensions.
        xml.remove(QRegularExpression(QStringLiteral(" OpenVegas(Locked|Muted|ParentLayerID)=\"[^\"]*\"")));
        QVERIFY(!xml.contains("OpenVegasParentLayerID"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write(xml.toUtf8()); file.close();
        composition::Composition restored;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &restored, &media).isSuccess());
        QCOMPARE(restored.layers().size(), 2);
        QVERIFY(restored.layers()[0].motionBlur); QVERIFY(restored.layers()[0].muted); QVERIFY(restored.layers()[0].locked);
        QVERIFY(!restored.layers()[1].motionBlur); QVERIFY(!restored.layers()[1].locked);
        QCOMPARE(restored.layers()[1].parentLayerId, restored.layers()[0].id);
        QVERIFY(!restored.layers()[0].parentLayerId.isValid());
    }
    // Names what differs between two copies of a layer, for round-trip checks.
    static QString layerDifferences(const composition::Layer& a, const composition::Layer& b) {
        QStringList out;
        const auto check = [&out](const QString& what, const QVariant& x, const QVariant& y) {
            if (x != y) out << QStringLiteral("%1: %2 -> %3").arg(what, x.toString(), y.toString());
        };
        check("id", a.id.value(), b.id.value()); check("parent", a.parentLayerId.value(), b.parentLayerId.value());
        check("name", a.name, b.name); check("kind", int(a.kind), int(b.kind));
        check("dimension", int(a.dimension), int(b.dimension)); check("zIndex", a.zIndex, b.zIndex);
        check("visible", a.visible, b.visible); check("locked", a.locked, b.locked); check("muted", a.muted, b.muted);
        check("motionBlur", a.motionBlur, b.motionBlur); check("label", a.labelColor.name(QColor::HexArgb), b.labelColor.name(QColor::HexArgb));
        check("blend", a.blendMode, b.blendMode); check("opacity", a.opacity, b.opacity);
        check("fov", a.cameraFieldOfView, b.cameraFieldOfView);
        for (auto p : composition::transformPropertiesFor(composition::LayerDimension::ThreeD)) {
            for (int axis = 0; axis < composition::axisCount(p, composition::LayerDimension::ThreeD); ++axis) {
                const QString what = QStringLiteral("%1.%2").arg(composition::transformPropertyName(p)).arg(axis);
                check(what + "@0", a.transform.valueAt(p, axis, 0), b.transform.valueAt(p, axis, 0));
                const auto* ca = a.transform.curve(p, axis); const auto* cb = b.transform.curve(p, axis);
                if (ca && cb) {
                    check(what + " keys", ca->locations().size(), cb->locations().size());
                    // Key ids are minted per load; everything else must match.
                    for (int frame : ca->locations()) {
                        const auto* ka = ca->at(frame); const auto* kb = cb->at(frame);
                        const QString key = QStringLiteral("%1 key@%2 ").arg(what).arg(frame);
                        if (!kb) { out << key + "missing"; continue; }
                        if (qAbs(ka->value.toDouble() - kb->value.toDouble()) > 1e-3)
                            check(key + "value", ka->value, kb->value);
                        check(key + "type", int(ka->temporal), int(kb->temporal));
                        const auto near = [](QPointF p, QPointF q) { return (p - q).manhattanLength() < 1e-3; };
                        if (!near(ka->incomingHandle, kb->incomingHandle) || !near(ka->outgoingHandle, kb->outgoingHandle))
                            out << key + QStringLiteral("handles (%1,%2|%3,%4) -> (%5,%6|%7,%8)")
                                .arg(ka->incomingHandle.x()).arg(ka->incomingHandle.y())
                                .arg(ka->outgoingHandle.x()).arg(ka->outgoingHandle.y())
                                .arg(kb->incomingHandle.x()).arg(kb->incomingHandle.y())
                                .arg(kb->outgoingHandle.x()).arg(kb->outgoingHandle.y());
                        if (qAbs(ka->incomingInfluence - kb->incomingInfluence) > 1e-3
                            || qAbs(ka->outgoingInfluence - kb->outgoingInfluence) > 1e-3)
                            out << key + "influence";
                        check(key + "locked", ka->handlesLocked, kb->handlesLocked);
                    }
                }
            }
        }
        check("audioLevel", a.transform.audioLevel, b.transform.audioLevel);
        check("masks", a.masks.size(), b.masks.size()); check("tracks", a.motionTracks.size(), b.motionTracks.size());
        check("clips", a.clips.size(), b.clips.size());
        for (int c = 0; c < qMin(a.clips.size(), b.clips.size()); ++c) {
            const auto& x = a.clips[c]; const auto& y = b.clips[c];
            const QString at = QStringLiteral("clip%1.").arg(c);
            check(at + "media", x.mediaId.value(), y.mediaId.value());
            check(at + "start", x.startSeconds, y.startSeconds); check(at + "duration", x.durationSeconds, y.durationSeconds);
            check(at + "source", x.sourceStartSeconds, y.sourceStartSeconds); check(at + "speed", x.speed, y.speed);
            check(at + "level", x.audioLevel, y.audioLevel); check(at + "effects", x.effects.size(), y.effects.size());
            for (int e = 0; e < qMin(x.effects.size(), y.effects.size()); ++e) {
                const auto& ex = x.effects[e]; const auto& ey = y.effects[e];
                const QString fx = at + QStringLiteral("fx%1.").arg(e);
                check(fx + "plugin", ex.pluginId.value(), ey.pluginId.value()); check(fx + "name", ex.name, ey.name);
                check(fx + "enabled", ex.enabled, ey.enabled);
                if (ex.parameterValues != ey.parameterValues) {
                    for (int p = 0; p < qMax(ex.parameterValues.size(), ey.parameterValues.size()); ++p)
                        check(fx + QStringLiteral("p%1").arg(p), ex.parameterValues.value(p), ey.parameterValues.value(p));
                }
                check(fx + "animated", ex.animation.size(), ey.animation.size());
            }
        }
        return out.join(QStringLiteral("; "));
    }
    void referenceProjectSurvivesResave() {
        const QString project = QStringLiteral(OPENVEGAS_SOURCE_DIR)
            + "/SAMPLES/vegfx_projects/project_1/Project.vegfx";
        if (!QFileInfo::exists(project)) QSKIP("SAMPLES/vegfx_projects/project_1 is not present");
        composition::Composition first; media::MediaManager firstMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(project, &first, &firstMedia).isSuccess());
        // Saved where the media sits, so its relative layout stays the same.
        const QString saved = QFileInfo(project).dir().filePath("resave-check.vegfx");
        const auto cleanup = qScopeGuard([saved] { QFile::remove(saved); });
        QVERIFY(project::VegfxSerializer::saveToFile(saved, first, firstMedia).isSuccess());
        QFile::remove(testArtifactPath("resave-project_1.vegfx"));
        QVERIFY(QFile::copy(saved, testArtifactPath("resave-project_1.vegfx")));
        composition::Composition second; media::MediaManager secondMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(saved, &second, &secondMedia).isSuccess());
        // Everything the model holds comes back as it was read.
        QCOMPARE(second.name(), first.name());
        QCOMPARE(second.id().value(), first.id().value());
        QCOMPARE(QSize(second.width(), second.height()), QSize(first.width(), first.height()));
        QCOMPARE(second.fpsNumerator(), first.fpsNumerator());
        QCOMPARE(second.durationSeconds(), first.durationSeconds());
        QCOMPARE(second.layers().size(), first.layers().size());
        for (int i = 0; i < first.layers().size(); ++i) {
            const QString differences = layerDifferences(first.layers()[i], second.layers()[i]);
            QVERIFY2(differences.isEmpty(), qPrintable(first.layers()[i].name + ": " + differences));
        }
        QCOMPARE(secondMedia.assets().size(), firstMedia.assets().size());
        QVERIFY(second.renderSettings() == first.renderSettings());
        // The shot keeps its own 35 s (2100 frames) although a layer runs on,
        // and its playhead and work area.
        QCOMPARE(qRound(first.durationSeconds() * 60), 2100);
        QCOMPARE(second.currentFrame(), 1370LL);
        QCOMPARE(second.workOut(), 1800LL);
        QCOMPARE(second.projectId().value(), QString("1bd8b759-d488-4b04-8e3d-d1d3bf27b8ea"));
        QCOMPARE(second.projectSettings().antialiasingMode, 1);
        // Text formats come from <Formats>, not from the box height.
        const composition::TextStyle text = composition::textStyleFromParameters(
            first.layers()[0].clips[0].effects[0].parameterValues);
        QCOMPARE(text.fontFamily, QString("Ubuntu"));
        QCOMPARE(text.fontStyle, QString("Medium"));
        QCOMPARE(text.fontSize, 61);
        QCOMPARE(text.fontColor, QColor(Qt::white));
        QCOMPARE(text.alignH, composition::TextStyle::AlignH::Center);
        // What the model does not hold is still in the file.
        QDomDocument written; QFile savedFile(saved);
        QVERIFY(savedFile.open(QIODevice::ReadOnly)); QVERIFY(written.setContent(savedFile.readAll()));
        const QDomElement root = written.documentElement();
        QVERIFY(!root.firstChildElement("OpenCompositeShots").isNull());
        QVERIFY(!root.firstChildElement("Screens").isNull());
        const QDomElement projectNode = root.firstChildElement("Project");
        QVERIFY(!projectNode.firstChildElement("BinFolder").isNull());
        QVERIFY(!projectNode.firstChildElement("Metadata").isNull());
        QCOMPARE(projectNode.firstChildElement("ProjectSettings").firstChildElement("UseLinearColor").text(), QString("0"));
        const QDomElement shot = projectNode.firstChildElement("AssetList").firstChildElement("Assets")
                                     .firstChildElement("CompositionAsset");
        QVERIFY(!shot.firstChildElement("CompositionTemplate").isNull());
        QVERIFY(!shot.firstChildElement("ViewerState").isNull());
        QCOMPARE(shot.firstChildElement("AudioVideoSettings").firstChildElement("FrameCount").text(), QString("2100"));
        QCOMPARE(shot.firstChildElement("AudioVideoSettings").firstChildElement("AudioSampleRate").text(), QString("48000"));
        int shininess = 0, postscript = 0, ids = 0;
        for (int i = 0; i < written.elementsByTagName("Name").size(); ++i)
            if (written.elementsByTagName("Name").at(i).toElement().text() == "matShininess") ++shininess;
        postscript = written.elementsByTagName("PostscriptName").size();
        for (const QString id : {"58429a15-2bcd-4650-b383-0b577a9d3e1c", "d5693c0f-de74-48aa-bdda-ab6a69d4ff01",
                                 "b60b46bb-5d13-4a9e-a4da-f12e8f794125"}) {
            for (int i = 0; i < written.elementsByTagName("AssetID").size(); ++i)
                if (written.elementsByTagName("AssetID").at(i).toElement().text() == id) { ++ids; break; }
        }
        QCOMPARE(shininess, first.layers().size());
        QCOMPARE(postscript, 2);
        QCOMPARE(ids, 3);
        QCOMPARE(written.elementsByTagName("SOuPt").size(), 4);
    }
    // project_1's two point-text layers: "Very D.I.S.C.O" says Top alignment,
    // "Daft Punk x Ottawan" Middle, and both stand on their own baseline at
    // the layer position (Flux ignores the vertical alignment of point text).
    // The port used to align them in a frame-sized box, which put the Top one
    // at the top of the frame.
    void referencePointTextStandsOnItsBaseline() {
        const QString project = QStringLiteral(OPENVEGAS_SOURCE_DIR)
            + "/SAMPLES/vegfx_projects/project_1/Project.vegfx";
        if (!QFileInfo::exists(project)) QSKIP("SAMPLES/vegfx_projects/project_1 is not present");
        // The offscreen platform only knows the fonts it is given; the
        // project's own (Ubuntu) when the machine has it.
        for (const QString& dir : {qEnvironmentVariable("LOCALAPPDATA") + "/Microsoft/Windows/Fonts",
                                   QStringLiteral("C:/Windows/Fonts")}) {
            for (const char* face : {"Ubuntu-Medium.ttf", "Ubuntu-Light.ttf"})
                if (QFileInfo::exists(dir + "/" + face)) QFontDatabase::addApplicationFont(dir + "/" + face);
        }
        auto shot = std::make_shared<composition::Composition>();
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(project::VegfxSerializer::loadFromFile(project, shot.get(), media.get()).isSuccess());
        QCOMPARE(QSize(shot->width(), shot->height()), QSize(1080, 1920));
        {
            // The whole frame, for a look.
            render::RenderManager whole; whole.setMediaManager(media); whole.setComposition(shot);
            QSignalSpy ready(&whole, &render::RenderManager::frameReady);
            whole.requestFrame(900, 15.0, QSize(540, 960));
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 20000);
            const QByteArray rgba = ready[0][1].toByteArray();
            const QImage frame(reinterpret_cast<const uchar*>(rgba.constData()), 540, 960, 540 * 4,
                               QImage::Format_RGBA8888);
            QVERIFY(frame.save(testArtifactPath(QStringLiteral("project_1-frame.png"))));
        }
        struct Line { QString text; double positionY; composition::TextStyle style; };
        QVector<Line> lines;
        for (int i = 0; i < shot->layers().size(); ++i) {
            composition::Layer& layer = shot->layerRef(i);
            const bool text = layer.kind == composition::LayerKind::Text && !layer.clips.isEmpty()
                              && !layer.clips[0].effects.isEmpty();
            layer.visible = text;
            if (!text) continue;
            const composition::TextStyle style = composition::textStyleFromParameters(
                layer.clips[0].effects[0].parameterValues);
            lines.append({style.text.trimmed(), layer.transform.positionAt(900).y(), style});
        }
        QCOMPARE(lines.size(), 2);
        std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.positionY > b.positionY; });
        QCOMPARE(lines[0].text, QString("Daft Punk x Ottawan"));
        QCOMPARE(lines[1].text, QString("Very D.I.S.C.O"));
        QCOMPARE(lines[0].style.alignV, composition::TextStyle::AlignV::Middle);
        QCOMPARE(lines[1].style.alignV, composition::TextStyle::AlignV::Top);
        QCOMPARE(lines[1].style.textMode, composition::TextStyle::TextMode::Point);
        QVERIFY(qAbs(lines[1].positionY + 188.807) < 0.01);
        // Baseline on the origin, centred on it - whatever the alignment.
        const QRectF frame(-540, -960, 1080, 1920);
        for (const Line& line : lines) {
            QFont font = QFontDatabase::font(line.style.fontFamily, line.style.fontStyle, 12);
            font.setPixelSize(line.style.fontSize);
            const QRectF ink = QFontMetricsF(font).tightBoundingRect(line.style.text);
            const QRectF bounds = render::styledTextBounds(frame, line.style);
            QVERIFY2(qAbs(bounds.top() - ink.top()) < 2.0 && qAbs(bounds.bottom() - ink.bottom()) < 2.0,
                     qPrintable(QStringLiteral("%1: %2..%3, ink %4..%5").arg(line.text)
                                    .arg(bounds.top()).arg(bounds.bottom()).arg(ink.top()).arg(ink.bottom())));
            // Centred without the trailing space "Daft Punk x Ottawan " has.
            QVERIFY2(qAbs(bounds.center().x()) < 4.0, qPrintable(QStringLiteral("%1: %2..%3")
                .arg(line.text).arg(bounds.left()).arg(bounds.right())));
        }
        // Rendered at 15 s, where both rest at their static place.
        render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(900, 15.0, QSize(1080, 1920));
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 20000);
        const QByteArray bytes = frames[0][1].toByteArray();
        const QImage image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), 1080, 1920,
                                    1080 * 4, QImage::Format_RGBA8888).copy();
        QVERIFY(image.save(testArtifactPath(QStringLiteral("project_1-text.png"))));
        // Rows with text ink, split between the two lines.
        const auto inkRows = [&image](int from, int to) {
            int first = -1, last = -1;
            for (int y = from; y < to; ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    if (qGray(row[x]) > 96 && qAlpha(row[x]) > 96) {
                        if (first < 0) first = y;
                        last = y;
                        break;
                    }
                }
            }
            return std::make_pair(first, last);
        };
        // Below the upper line's baseline, above the lower line's capitals.
        const double split = 960 - lines[0].positionY + (lines[0].positionY - lines[1].positionY) / 4.0;
        QCOMPARE(inkRows(0, int(960 - lines[0].positionY) - 80).first, -1);
        for (int i = 0; i < 2; ++i) {
            QFont font = QFontDatabase::font(lines[i].style.fontFamily, lines[i].style.fontStyle, 12);
            font.setPixelSize(lines[i].style.fontSize);
            const QRectF ink = QFontMetricsF(font).tightBoundingRect(lines[i].style.text);
            const double baseline = 960 - lines[i].positionY;
            const auto [top, bottom] = i == 0 ? inkRows(0, int(split)) : inkRows(int(split), 1920);
            QVERIFY2(qAbs(top - (baseline + ink.top())) < 4 && qAbs(bottom - (baseline + ink.bottom())) < 4,
                     qPrintable(QStringLiteral("%1: rows %2..%3, expected %4..%5").arg(lines[i].text)
                                    .arg(top).arg(bottom).arg(baseline + ink.top()).arg(baseline + ink.bottom())));
        }
        // Written back, the point-text box is the reference's own extent.
        QTemporaryDir temp;
        const QString saved = temp.filePath("text.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(saved, *shot, *media).isSuccess());
        QFile file(saved); QVERIFY(file.open(QIODevice::ReadOnly));
        QDomDocument written; QVERIFY(written.setContent(file.readAll()));
        bool found = false;
        const QDomNodeList boxes = written.elementsByTagName("TextBox");
        for (int i = 0; i < boxes.size(); ++i) {
            const QDomElement box = boxes.at(i).toElement();
            if (qAbs(box.firstChildElement("MaxY").text().toDouble() - 96.928) > 0.5) continue;
            found = true;
            QVERIFY(qAbs(box.firstChildElement("MinY").text().toDouble() + 19.656) < 0.5);
            QVERIFY(qAbs(box.firstChildElement("MinX").text().toDouble()
                         + box.firstChildElement("MaxX").text().toDouble()) < 0.01);
        }
        QVERIFY(found);
    }
    // A preview at another size than the shot (Half, Quarter, Antialiased,
    // the Layer panel's 960x540) is the whole frame resampled, not
    // full-scale content cropped into a smaller canvas.
    void previewSizesScaleTheWholeFrame() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 100); shot->setDurationSeconds(1);
        auto& plane = shot->addLayer("Plane");
        plane.kind = composition::LayerKind::Plane; plane.planeColor = Qt::red;
        plane.transform.position = QPointF(50, 0);
        plane.transform.scalePercent = QPointF(50, 50);
        composition::Clip fill; fill.durationSeconds = 1; plane.clips.append(fill);
        // Full size: a 100x50 plane centred at (150, 50), x 100..200, y 25..75.
        for (const double scale : {1.0, 0.5, 0.25, 2.0}) {
            const QSize size(qRound(200 * scale), qRound(100 * scale));
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0, size);
            QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 5000);
            QCOMPARE(frames[0][2].toSize(), size);
            const QByteArray bytes = frames[0][1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), size.width(),
                               size.height(), size.width() * 4, QImage::Format_RGBA8888);
            const auto red = [&](double x, double y) {
                const QColor c = image.pixelColor(int(x * scale), int(y * scale));
                return c.red() > 200 && c.green() < 60;
            };
            QVERIFY2(red(150, 50) && red(110, 30) && red(190, 70), qPrintable(QString::number(scale)));
            QVERIFY2(!red(90, 50) && !red(150, 15) && !red(150, 85), qPrintable(QString::number(scale)));
        }
    }
    // Non-square pixels the way Flux has them: layers live in square units,
    // width x PAR by height (a New Grade is round(Width * PAR) wide), the
    // frame is that squeezed into width x height pixels, the viewer shows it
    // at the square shape and an export tells the stream its sample aspect.
    void pixelAspectSqueezesTheFrameAndTheViewerUnsqueezes() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 100); shot->setDurationSeconds(1);
        shot->setPixelAspect(composition::Composition::Anamorphic2To1);
        QCOMPARE(shot->displaySize(), QSize(400, 100));
        shot->setPixelAspect(composition::Composition::DvNtsc);
        QCOMPARE(shot->displaySize(), QSize(182, 100));
        shot->setPixelAspect(composition::Composition::Anamorphic2To1);
        auto& plane = shot->addLayer("Plane");
        plane.kind = composition::LayerKind::Plane; plane.planeColor = Qt::red;
        plane.transform.position = QPointF(100, 0);
        plane.transform.scalePercent = QPointF(50, 50);
        composition::Clip fill; fill.durationSeconds = 1; plane.clips.append(fill);
        // Square space: a 200x50 plane centred at (300, 50) - x 200..400 - so
        // pixels 100..200 of the 200-pixel-wide frame.
        render::RenderManager manager; manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 0, QSize(200, 100));
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 5000);
        const QByteArray bytes = frames[0][1].toByteArray();
        const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 200, 100, 200 * 4,
                           QImage::Format_RGBA8888);
        const auto red = [&](int x, int y) {
            const QColor c = image.pixelColor(x, y);
            return c.red() > 200 && c.green() < 60;
        };
        QVERIFY(red(105, 30) && red(195, 70) && red(150, 50));
        QVERIFY(!red(95, 50) && !red(150, 20) && !red(150, 80));
        // The viewer shows the 200x100 frame 4:1, and a point on it maps to
        // square units.
        ui::ViewerWidget viewer;
        viewer.resize(840, 300);
        viewer.setProjectSize(shot->displaySize());
        viewer.setFrame(image.copy());
        viewer.show(); QTest::qWait(20);
        const QRectF shown = viewer.mapping().imageRect;
        QVERIFY2(qAbs(shown.width() / shown.height() - 4.0) < 0.05,
                 qPrintable(QStringLiteral("%1x%2").arg(shown.width()).arg(shown.height())));
        QCOMPARE(viewer.mapping().canvasSize, QSizeF(400, 100));
        QCOMPARE(render::sampleAspectRatio(10.0 / 11.0), QString("10/11"));
        QCOMPARE(render::sampleAspectRatio(40.0 / 33.0), QString("40/33"));
        QCOMPARE(render::sampleAspectRatio(4.0 / 3.0), QString("4/3"));
        QCOMPARE(render::sampleAspectRatio(2.0), QString("2/1"));
        QCOMPARE(render::sampleAspectRatio(1.0), QString("1/1"));
    }
    // Footage keeps its own pixel shape: the stream's SAR comes from the file
    // (MediaAssetRef::PixelAspectRatio), the user can override it in Media
    // Properties (MediaOverrideOptions::PixelAspectRatio, <OverridePAR>/<PAR>;
    // a still's own <PAR>), and the renderer draws the media that much wider.
    void mediaAudioStreamsMetadataSelectionAndRoundTrip() {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
        if (ffmpeg.isEmpty() || ffprobe.isEmpty() || !media::vlc().available)
            QSKIP("FFmpeg and libVLC are needed for the multi-stream integration test");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString source = dir.filePath("streams.mkv");
        QProcess make;
        make.start(ffmpeg, {"-v", "error", "-y", "-f", "lavfi", "-i", "color=s=32x32:r=25:d=1",
                            "-f", "lavfi", "-i", "aevalsrc=0.02|0.02:s=32000:d=1",
                            "-f", "lavfi", "-i", "aevalsrc=0.08|0.08:s=44100:d=1",
                            "-map", "0:v:0", "-map", "1:a:0", "-map", "2:a:0",
                            "-c:v", "ffv1", "-c:a", "pcm_s16le",
                            "-metadata:s:a:0", "language=eng", "-metadata:s:a:1", "language=jpn", source});
        QVERIFY(make.waitForFinished(30000));
        QVERIFY2(make.exitCode() == 0, make.readAllStandardError().constData());
        media::MediaManager manager;
        QVERIFY(manager.importFile(source).isSuccess());
        const media::MediaAsset original = manager.assetByFilePath(source);
        const auto& streams = original.fileStreams();
        QVERIFY(streams.hasVideo && !streams.videoFormat.isEmpty() && !streams.videoCodec.isEmpty());
        QCOMPARE(streams.audio.size(), 2);
        QCOMPARE(streams.audio[0].sampleRate, 32000u);
        QCOMPARE(streams.audio[1].sampleRate, 44100u);
        QCOMPARE(streams.audio[1].channels, 2u);
        QVERIFY(!streams.audio[1].codec.isEmpty());
        QVERIFY(!streams.audio[0].language.isEmpty());

        ui::MediaSettingsDialog dialog(original);
        auto* combo = dialog.findChild<QComboBox*>("mediaSettingsAudioStream");
        auto* rate = dialog.findChild<QLabel*>("mediaSettingsSampleRate");
        QVERIFY(combo && rate);
        QCOMPARE(rate->text(), QString("-")); // the automatic preference is unknown on two streams
        combo->setCurrentIndex(combo->findData(0));
        QVERIFY(rate->text().contains("32000"));
        combo->setCurrentIndex(combo->findData(1));
        QVERIFY(rate->text().contains("44100"));
        QCOMPARE(dialog.result().audioStreamIndex(), 1);
        dialog.adjustSize();
        QVERIFY(dialog.grab().save(testArtifactPath(QStringLiteral("media-settings-audio-streams.png"))));

        ui::MediaPanel panel; panel.setMediaManager(&manager);
        QUndoStack undo; panel.setUndoStack(&undo);
        panel.setMediaOverrides(original.id(), dialog.result());
        QCOMPARE(manager.assetById(original.id()).audioStreamIndex(), 1);
        QCOMPARE(undo.count(), 1);
        undo.undo(); QCOMPARE(manager.assetById(original.id()).audioStreamIndex(), -1);
        undo.redo(); QCOMPARE(manager.assetById(original.id()).audioStreamIndex(), 1);

        composition::Composition scene; scene.setSize(32, 32); scene.setDurationSeconds(1);
        auto& layer = scene.addLayer("Audio streams");
        composition::Clip clip; clip.mediaId = original.id(); clip.durationSeconds = 1;
        layer.clips.append(clip);
        const QString projectPath = dir.filePath("streams.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(projectPath, scene, manager).isSuccess());
        composition::Composition restored; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(projectPath, &restored, &reloaded).isSuccess());
        QCOMPARE(reloaded.assetByFilePath(source).audioStreamIndex(), 1);
        // Clear/change the field after a native-document merge as well.
        reloaded.assetByFilePathForEdit(source)->setAudioStreamIndex(0);
        QVERIFY(project::VegfxSerializer::saveToFile(projectPath, restored, reloaded).isSuccess());
        composition::Composition again; media::MediaManager againMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(projectPath, &again, &againMedia).isSuccess());
        QCOMPARE(againMedia.assetByFilePath(source).audioStreamIndex(), 0);

        render::ExportAudioRequest request;
        request.composition = &scene; request.media = &manager;
        request.ffmpeg = ffmpeg; request.ffprobe = ffprobe; request.temporaryDirectory = dir.path();
        request.exportStart = 0; request.exportEnd = 1;
        QVector<render::ExportAudioInput> inputs; QString error;
        QVERIFY2(render::buildExportAudioInputs(request, inputs, &error), qPrintable(error));
        QCOMPARE(inputs.size(), 1);
        QVERIFY(!inputs[0].rawPcm);
        QCOMPARE(inputs[0].audioStreamIndex, 1);
        // The direct filter path must select the same track in a finished movie.
        scene.setFrameRate(25, 1);
        render::ExportJob::Request movie;
        movie.composition = std::shared_ptr<composition::Composition>(&scene, [](auto*) {});
        movie.media = std::shared_ptr<media::MediaManager>(&manager, [](auto*) {});
        movie.outputPath = dir.filePath("selected.mov"); movie.lastFrame = 2;
        movie.hardwareEncoding = false;
        {
            render::ExportJob job(movie);
            QSignalSpy finished(&job, &render::ExportJob::finished);
            job.start();
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
            QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        }
        QProcess decodeMovie;
        decodeMovie.start(ffmpeg, {"-v", "error", "-i", movie.outputPath, "-map", "0:a:0",
                                   "-ar", "48000", "-ac", "2", "-f", "s16le", "pipe:1"});
        QVERIFY(decodeMovie.waitForFinished(10000));
        QCOMPARE(decodeMovie.exitCode(), 0);
        const QByteArray moviePcm = decodeMovie.readAllStandardOutput();
        QVERIFY(moviePcm.size() > 2400 * 4 + 2);
        QVERIFY(qAbs(int(qFromLittleEndian<qint16>(moviePcm.constData() + 2400 * 4)) - 2621) < 50);
        // An animated gain forces the PCM/native processing branch.
        layer.transform.audioLevelCurve.set(0, 0);
        layer.transform.audioLevelCurve.set(30, 0);
        inputs.clear();
        QVERIFY2(render::buildExportAudioInputs(request, inputs, &error), qPrintable(error));
        QCOMPARE(inputs.size(), 1); QVERIFY(inputs[0].rawPcm);
        QFile pcm(inputs[0].path); QVERIFY(pcm.open(QIODevice::ReadOnly));
        QVERIFY(pcm.seek(12000 * 4));
        const QByteArray bytes = pcm.read(2); QCOMPARE(bytes.size(), 2);
        const int value = qFromLittleEndian<qint16>(bytes.constData());
        QVERIFY2(qAbs(value - 2621) < 50, qPrintable(QString::number(value)));

        const QString cacheDirectory = dir.filePath("waveforms");
        {
            media::WaveformCache cache; cache.setDecoder(ffmpeg); cache.setCacheDirectory(cacheDirectory);
            QSignalSpy ready(&cache, &media::WaveformCache::peaksReady);
            QVERIFY(!cache.peaks(source, 0)); QVERIFY(!cache.peaks(source, 1));
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
            const auto* quiet = cache.peaks(source, 0);
            const auto* loud = cache.peaks(source, 1);
            QVERIFY(quiet && loud);
            QVERIFY(loud->rms.value(25) > quiet->rms.value(25) * 3.5f);
        }
        media::WaveformCache cached; cached.setDecoder(QString()); cached.setCacheDirectory(cacheDirectory);
        QVERIFY(cached.peaks(source, 0) && cached.peaks(source, 1));
        QVERIFY(cached.peaks(source, 1)->rms.value(25) > cached.peaks(source, 0)->rms.value(25) * 3.5f);

        // Offline media keeps the explicit choice, ready for Relink.
        QVERIFY(QFile::rename(source, dir.filePath("missing-source.bin")));
        composition::Composition offline; media::MediaManager offlineMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(projectPath, &offline, &offlineMedia).isSuccess());
        QCOMPARE(offlineMedia.assetByFilePath(source).audioStreamIndex(), 0);
    }

    void mediaPixelAspectFromFileAndOverride() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        // A 200x100 red still in a 600x200 shot.
        const QString still = temp.filePath("still.png");
        QImage red(200, 100, QImage::Format_RGB32); red.fill(Qt::red);
        QVERIFY(red.save(still));
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importFile(still).isSuccess());
        const media::MediaAsset imported = media->assetByFilePath(QFileInfo(still).absoluteFilePath());
        QVERIFY(imported.isValid());
        QCOMPARE(imported.pixelAspectValue(), 1.0);

        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(600, 200); shot->setDurationSeconds(1);
        {
            auto& layer = shot->addLayer("Still");
            composition::Clip clip; clip.mediaId = imported.id(); clip.durationSeconds = 1;
            layer.clips.append(clip);
        }
        const auto widthOfRed = [&] {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0, QSize(600, 200));
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000)) return -1;
            const QByteArray bytes = frames.last()[1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 600, 200, 600 * 4,
                               QImage::Format_RGBA8888);
            int width = 0;
            for (int x = 0; x < 600; ++x) {
                const QColor c = image.pixelColor(x, 100);
                if (c.red() > 200 && c.green() < 60) ++width;
            }
            return width;
        };
        QVERIFY(qAbs(widthOfRed() - 200) <= 2);
        // Anamorphic 2:1 makes it twice as wide.
        media->assetByIdForEdit(imported.id())->setPixelAspectOverride(
            true, composition::Composition::Anamorphic2To1);
        QCOMPARE(media->assetById(imported.id()).pixelAspectValue(), 2.0);
        QVERIFY(qAbs(widthOfRed() - 400) <= 2);

        // Through the project and back.
        const QString path = temp.filePath("par.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, *media).isSuccess());
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &reloaded).isSuccess());
        const media::MediaAsset back = reloaded.assetByFilePath(QFileInfo(still).absoluteFilePath());
        QVERIFY(back.isValid());
        QVERIFY(back.overridesPixelAspect());
        QCOMPARE(back.pixelAspectOverride(), int(composition::Composition::Anamorphic2To1));

        // Media Properties: "From File" off with the override chosen; ticking
        // it gives the file's shape back.
        ui::MediaSettingsDialog dialog(media->assetById(imported.id()));
        auto* fromFile = dialog.findChild<QCheckBox*>("mediaSettingsAspectFromFile");
        auto* aspect = dialog.findChild<QComboBox*>("mediaSettingsAspect");
        QVERIFY(fromFile && aspect);
        QVERIFY(!fromFile->isChecked() && aspect->isEnabled());
        QCOMPARE(aspect->currentIndex(), int(composition::Composition::Anamorphic2To1));
        dialog.adjustSize();
        QVERIFY(dialog.grab().save(testArtifactPath(QStringLiteral("media-settings.png"))));
        QVERIFY(dialog.overridesPixelAspect());
        fromFile->setChecked(true);
        QVERIFY(!dialog.overridesPixelAspect() && !aspect->isEnabled());
        QCOMPARE(aspect->currentIndex(), int(composition::Composition::SquarePixels));

        // The stream's own SAR, read by libVLC: 720x480 at 10:11 is DV NTSC.
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty() || !media::vlc().available)
            QSKIP("ffmpeg or libVLC is not available to make and read a DV-shaped clip");
        const QString clip = temp.filePath("dv.mp4");
        QProcess make;
        make.start(ffmpeg, {"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc=size=720x480:rate=25:duration=1",
                            "-vf", "setsar=10/11", "-c:v", "mpeg4", "-q:v", "5", clip});
        QVERIFY(make.waitForFinished(60000));
        if (make.exitCode() != 0 || !QFileInfo::exists(clip)) QSKIP("ffmpeg could not write the test clip");
        const media::VideoInfo info = media::probeVideo(clip);
        QVERIFY(info.valid);
        QCOMPARE(info.frameSize, QSize(720, 480));
        QVERIFY2(qAbs(info.pixelAspect - 10.0 / 11.0) < 0.01, qPrintable(QString::number(info.pixelAspect)));
        QVERIFY2(qAbs(info.frameRate - 25.0) < 0.01, qPrintable(QString::number(info.frameRate)));
        QVERIFY(media->importFile(clip).isSuccess());
        const media::MediaAsset dv = media->assetByFilePath(QFileInfo(clip).absoluteFilePath());
        QCOMPARE(dv.filePixelAspectKind(), int(composition::Composition::DvNtsc));
        QVERIFY(qAbs(dv.pixelAspectValue() - 10.0 / 11.0) < 0.01);
        QCOMPARE(dv.fileFrameRate(), 25.0);

        // The video's own overrides through the project and back.
        media::MediaAsset* edited = media->assetByIdForEdit(dv.id());
        edited->setFrameRateOverride(true, 50.0);
        edited->setColorLevels(media::MediaAsset::FullLevels);
        edited->setColorSpace(media::MediaAsset::Rec709);
        edited->setHardwareDecoding(false);
        QCOMPARE(edited->durationSeconds(), dv.durationSeconds() / 2.0);
        {
            auto& layer = shot->addLayer("DV");
            composition::Clip dvClip; dvClip.mediaId = dv.id(); dvClip.durationSeconds = 1;
            layer.clips.append(dvClip);
        }
        const QString videoProject = temp.filePath("dv.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(videoProject, *shot, *media).isSuccess());
        composition::Composition videoLoaded; media::MediaManager videoReloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(videoProject, &videoLoaded, &videoReloaded).isSuccess());
        const media::MediaAsset dvBack = videoReloaded.assetByFilePath(QFileInfo(clip).absoluteFilePath());
        QVERIFY(dvBack.overridesFrameRate());
        QCOMPARE(dvBack.frameRate(), 50.0);
        QCOMPARE(dvBack.colorLevels(), int(media::MediaAsset::FullLevels));
        QCOMPARE(dvBack.colorSpace(), int(media::MediaAsset::Rec709));
        QVERIFY(!dvBack.hardwareDecoding());
        // Media Properties shows them all for decoded video.
        ui::MediaSettingsDialog videoDialog(dvBack);
        QVERIFY(!videoDialog.findChild<QCheckBox*>("mediaSettingsFrameRateFromFile")->isChecked());
        QCOMPARE(videoDialog.findChild<QComboBox*>("mediaSettingsColorLevels")->currentIndex(),
                 int(media::MediaAsset::FullLevels));
        QVERIFY(!videoDialog.findChild<QCheckBox*>("mediaSettingsHardwareDecoding")->isChecked());
        videoDialog.adjustSize();
        QVERIFY(videoDialog.grab().save(testArtifactPath(QStringLiteral("media-settings-video.png"))));
        const media::MediaAsset unchanged = videoDialog.result();
        QCOMPARE(unchanged.frameRate(), 50.0);
        QVERIFY(!unchanged.hardwareDecoding());
    }
    // The rest of Media Properties: a frame rate override plays the file's
    // frames at another rate, premultiplied alpha is divided out, full-range
    // footage the decoder took as studio range is squeezed back, a forced
    // matrix replaces the decoder's default - and a still keeps its alpha
    // override in the project.
    void mediaOverridesReinterpretFrames() {
        media::MediaAsset clip;
        clip.setFilePath(QStringLiteral("C:/media/clip.mp4"));
        QCOMPARE(clip.kind(), media::MediaKind::Video);
        clip.setFrameSize(QSize(1920, 1080));
        clip.setDurationSeconds(2.0);
        clip.setFileFrameRate(29.97);
        QCOMPARE(clip.fileFrameRate(), 30000.0 / 1001.0);
        clip.setFileFrameRate(30.0);
        // At 15 fps the file's frame 15 comes a second in, and it lasts 4 s.
        clip.setFrameRateOverride(true, 15.0);
        QCOMPARE(clip.frameRate(), 15.0);
        QCOMPARE(clip.sourceSecondsAt(1.0), 0.5);
        QCOMPARE(clip.durationSeconds(), 4.0);
        clip.setFrameRateOverride(false, 15.0);
        QCOMPARE(clip.sourceSecondsAt(1.0), 1.0);
        QCOMPARE(clip.durationSeconds(), 2.0);

        QImage frame(2, 1, QImage::Format_RGBA8888);
        frame.setPixelColor(0, 0, QColor(255, 0, 0));
        frame.setPixelColor(1, 0, QColor(0, 0, 0));
        QVERIFY(!clip.reinterpretsPixels());
        // Full range: white back to 235, black to 16.
        clip.setColorLevels(media::MediaAsset::FullLevels);
        QImage squeezed = clip.interpretFrame(frame);
        QCOMPARE(squeezed.pixelColor(0, 0), QColor(235, 16, 16));
        QCOMPARE(squeezed.pixelColor(1, 0), QColor(16, 16, 16));
        clip.setColorLevels(media::MediaAsset::AutomaticLevels);
        // HD is decoded as Rec. 709; read as Rec. 601, pure red loses some
        // red and all its green.
        clip.setColorSpace(media::MediaAsset::Rec601);
        QVERIFY(clip.reinterpretsPixels());
        const QColor rematrixed = clip.interpretFrame(frame).pixelColor(0, 0);
        QVERIFY2(rematrixed.red() > 225 && rematrixed.red() < 240 && rematrixed.green() == 0,
                 qPrintable(rematrixed.name()));
        // SD is Rec. 601 already.
        clip.setFrameSize(QSize(720, 480));
        QVERIFY(!clip.reinterpretsPixels());

        // A still with alpha: premultiplied bytes divided out.
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString still = temp.filePath("alpha.png");
        QImage translucent(2, 2, QImage::Format_ARGB32);
        translucent.fill(QColor(64, 0, 0, 128));
        QVERIFY(translucent.save(still));
        media::MediaManager media;
        QVERIFY(media.importFile(still).isSuccess());
        const core::Identifier id = media.assetByFilePath(QFileInfo(still).absoluteFilePath()).id();
        media::MediaAsset* asset = media.assetByIdForEdit(id);
        QVERIFY(asset && asset->fileHasAlpha());
        QVERIFY(!asset->reinterpretsPixels());
        asset->setAlphaOverride(true, media::MediaAsset::PremultipliedAlpha);
        const QColor straight = asset->interpretFrame(QImage(still)).pixelColor(0, 0);
        QVERIFY2(qAbs(straight.red() - 127) <= 1 && straight.alpha() == 128, qPrintable(straight.name(QColor::HexArgb)));

        // Media Properties for it: alpha offered, no levels or space.
        ui::MediaSettingsDialog dialog(*asset);
        auto* alphaFromFile = dialog.findChild<QCheckBox*>("mediaSettingsAlphaFromFile");
        auto* alpha = dialog.findChild<QComboBox*>("mediaSettingsAlpha");
        QVERIFY(alphaFromFile && alpha && alphaFromFile->isEnabled());
        QVERIFY(!alphaFromFile->isChecked() && alpha->isEnabled());
        QCOMPARE(alpha->currentIndex(), int(media::MediaAsset::PremultipliedAlpha));
        QVERIFY(!dialog.findChild<QComboBox*>("mediaSettingsColorLevels"));
        QVERIFY(!dialog.findChild<QComboBox*>("mediaSettingsFrameRate"));
        alphaFromFile->setChecked(true);
        QVERIFY(!dialog.result().overridesAlpha());

        // Through the project and back.
        composition::Composition shot; shot.setSize(100, 100);
        {
            auto& layer = shot.addLayer("Still");
            composition::Clip clipOnLayer; clipOnLayer.mediaId = id; clipOnLayer.durationSeconds = 1;
            layer.clips.append(clipOnLayer);
        }
        const QString path = temp.filePath("alpha.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, shot, media).isSuccess());
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &reloaded).isSuccess());
        const media::MediaAsset back = reloaded.assetByFilePath(QFileInfo(still).absoluteFilePath());
        QVERIFY(back.overridesAlpha());
        QCOMPARE(back.alphaMode(), int(media::MediaAsset::PremultipliedAlpha));
    }
    // A paragraph box keeps its place in layer space through load and save.
    void paragraphTextBoxKeepsItsOffset() {
        composition::TextStyle style;
        style.text = "Box"; style.textMode = composition::TextStyle::TextMode::Paragraph;
        style.paragraphSize = QSizeF(200, 100); style.paragraphOffset = QPointF(150, -40);
        style.alignV = composition::TextStyle::AlignV::Top; style.alignH = composition::TextStyle::AlignH::Left;
        const composition::TextStyle again =
            composition::textStyleFromParameters(composition::textStyleToParameters(style));
        QCOMPARE(again.paragraphOffset, QPointF(150, -40));
        // Box left at x = 50, top at y = 10 (Y up) - so 10 px above the origin
        // in the Y-down frame the text starts there.
        const QRectF bounds = render::styledTextBounds(QRectF(-320, -180, 640, 360), style);
        QVERIFY2(bounds.left() >= 50 && bounds.left() < 56, qPrintable(QString::number(bounds.left())));
        QVERIFY2(bounds.top() >= -10 && bounds.top() < 10, qPrintable(QString::number(bounds.top())));

        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(640, 360);
        auto& layer = shot->addLayer("Box");
        layer.kind = composition::LayerKind::Text;
        composition::Clip clip; clip.durationSeconds = 1;
        composition::Effect text; text.pluginId = core::Identifier("text");
        text.name = "text"; text.parameterValues = composition::textStyleToParameters(style);
        clip.effects.append(text); layer.clips.append(clip);
        QTemporaryDir temp; media::MediaManager media;
        const QString path = temp.filePath("box.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, media).isSuccess());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(file.readAll()); file.close();
        QVERIFY(xml.contains("<MinX>50</MinX>")); QVERIFY(xml.contains("<MaxX>250</MaxX>"));
        QVERIFY(xml.contains("<MinY>-90</MinY>")); QVERIFY(xml.contains("<MaxY>10</MaxY>"));
        composition::Composition loaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &media).isSuccess());
        const composition::TextStyle read = composition::textStyleFromParameters(
            loaded.layers()[0].clips[0].effects[0].parameterValues);
        QCOMPARE(read.paragraphOffset, QPointF(150, -40));
        QCOMPARE(read.paragraphSize, QSizeF(200, 100));
    }
    // Three shots the way the reference writes them: each a CompositionAsset,
    // "Main" (primary) nesting "Inner" through an AssetLayer whose <AssetID>
    // is Inner's <ID>, and "Spare" on its own; Main and Spare open as tabs.
    static QByteArray severalShotsProject() {
        return R"(<?xml version="1.0" encoding="UTF-8"?>
<VegasEffectsProject Version="0" CurrentScreen="2" AppVersion="1.0.0.0" AppEdition="5000">
 <Project Version="7">
  <ID>11111111-1111-1111-1111-111111111111</ID>
  <Name>Shots.vegfx</Name>
  <AssetList Version="7"><Assets>
   <CompositionAsset Version="19">
    <ID>aaaaaaaa-0000-0000-0000-000000000001</ID><Name>Inner</Name><CTI>12</CTI>
    <InPoint>0</InPoint><OutPoint>60</OutPoint><IsPrimary>0</IsPrimary>
    <AudioVideoSettings Version="1"><FrameCount>60</FrameCount><Width>640</Width><Height>360</Height><FrameRate>30</FrameRate></AudioVideoSettings>
    <Layers>
     <PointLayer Version="17"><LayerBase Version="2"><ID>10000000-0000-0000-0000-000000000001</ID><Name>Point</Name>
      <StartFrame>0</StartFrame><EndFrame>60</EndFrame></LayerBase></PointLayer>
     <GradeLayer Version="17"><LayerBase Version="2"><ID>10000000-0000-0000-0000-000000000002</ID><Name>Grade</Name>
      <StartFrame>5</StartFrame><EndFrame>35</EndFrame></LayerBase></GradeLayer>
    </Layers>
   </CompositionAsset>
   <CompositionAsset Version="19">
    <ID>bbbbbbbb-0000-0000-0000-000000000002</ID><Name>Main</Name><CTI>45</CTI>
    <InPoint>0</InPoint><OutPoint>300</OutPoint><IsPrimary>1</IsPrimary>
    <AudioVideoSettings Version="1"><FrameCount>300</FrameCount><Width>1920</Width><Height>1080</Height><FrameRate>30</FrameRate></AudioVideoSettings>
    <Layers>
     <AssetLayer Version="17"><AssetID>aaaaaaaa-0000-0000-0000-000000000001</AssetID><AssetInstanceStart>15</AssetInstanceStart>
      <Dimensions>0</Dimensions>
      <LayerBase Version="2"><ID>20000000-0000-0000-0000-000000000001</ID><Name>Inner</Name>
       <ParentLayerID>00000000-0000-0000-0000-000000000000</ParentLayerID>
       <StartFrame>30</StartFrame><EndFrame>90</EndFrame><BlendMode>0</BlendMode><Visible>1</Visible></LayerBase>
     </AssetLayer>
    </Layers>
   </CompositionAsset>
   <CompositionAsset Version="19">
    <ID>cccccccc-0000-0000-0000-000000000003</ID><Name>Spare</Name><CTI>0</CTI>
    <InPoint>0</InPoint><OutPoint>50</OutPoint><IsPrimary>0</IsPrimary>
    <AudioVideoSettings Version="1"><FrameCount>50</FrameCount><Width>1280</Width><Height>720</Height><FrameRate>25</FrameRate></AudioVideoSettings>
    <Layers/>
   </CompositionAsset>
  </Assets></AssetList>
 </Project>
 <OpenCompositeShots Version="0" TimelineType="1404" TimelineId="cccccccc-0000-0000-0000-000000000003">
  <CompositeShot CompositionId="bbbbbbbb-0000-0000-0000-000000000002" Name="Main"/>
  <CompositeShot CompositionId="cccccccc-0000-0000-0000-000000000003" Name="Spare"/>
 </OpenCompositeShots>
</VegasEffectsProject>
)";
    }
    void projectKeepsEveryCompositeShot() {
        QTemporaryDir folder; QVERIFY(folder.isValid());
        const QString path = folder.filePath("Shots.vegfx");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(severalShotsProject()); }
        composition::Composition main; media::MediaManager media;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &main, &media).isSuccess());
        // The primary shot is the root; the others are the project's further
        // shots, in asset-list order, and the nested layer points at Inner.
        QCOMPARE(main.name(), QString("Main"));
        QVERIFY(main.isPrimary());
        QCOMPARE(main.currentFrame(), 45LL);
        QCOMPARE(main.compositeShots().size(), 2);
        const auto inner = main.compositeShots().at(0);
        const auto spare = main.compositeShots().at(1);
        QCOMPARE(inner->name(), QString("Inner"));
        QCOMPARE(spare->name(), QString("Spare"));
        QVERIFY(!inner->isPrimary());
        QCOMPARE(QSize(spare->width(), spare->height()), QSize(1280, 720));
        QCOMPARE(spare->fpsNumerator(), 25);
        QCOMPARE(inner->currentFrame(), 12LL);
        QCOMPARE(main.layers().size(), 1);
        const composition::Clip& nested = main.layers().at(0).clips.at(0);
        QCOMPARE(nested.nestedComposition, inner);
        QCOMPARE(nested.startSeconds, 1.0);
        QCOMPARE(nested.durationSeconds, 2.0);
        QCOMPARE(nested.sourceStartSeconds, 0.5);
        // Point carries no clip; Grade keeps its range without media.
        QCOMPARE(inner->layers().size(), 2);
        QVERIFY(inner->layers().at(0).clips.isEmpty());
        QCOMPARE(inner->layers().at(1).clips.size(), 1);
        QVERIFY(inner->layers().at(1).clips.at(0).effects.isEmpty());
        QCOMPARE(inner->layers().at(1).clips.at(0).startSeconds, 5.0 / 30.0);
        QCOMPARE(main.openShotIds(), QStringList({"bbbbbbbb-0000-0000-0000-000000000002",
                                                  "cccccccc-0000-0000-0000-000000000003"}));
        QCOMPARE(main.activeShotId(), QString("cccccccc-0000-0000-0000-000000000003"));

        // Saved back: every shot is its own CompositionAsset again, the nested
        // layer names Inner's <ID>, and the tabs follow the model.
        main.compositeShots().at(1)->setName("Spare 2");
        main.setOpenShots({"cccccccc-0000-0000-0000-000000000003", "aaaaaaaa-0000-0000-0000-000000000001"},
                          "aaaaaaaa-0000-0000-0000-000000000001");
        const QString saved = folder.filePath("Saved.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(saved, main, media).isSuccess());
        QDomDocument written; QFile savedFile(saved);
        QVERIFY(savedFile.open(QIODevice::ReadOnly)); QVERIFY(written.setContent(savedFile.readAll()));
        savedFile.close();   // Windows cannot replace a file still open
        QCOMPARE(written.elementsByTagName("CompositionAsset").size(), 3);
        QCOMPARE(written.elementsByTagName("OpenVegasCompositions").size(), 0);
        QStringList primaries;
        for (int i = 0; i < written.elementsByTagName("CompositionAsset").size(); ++i) {
            const QDomElement shot = written.elementsByTagName("CompositionAsset").at(i).toElement();
            primaries << shot.firstChildElement("Name").text() + "=" + shot.firstChildElement("IsPrimary").text();
        }
        // The root shot first, then the others in the order the project lists them.
        QCOMPARE(primaries, QStringList({"Main=1", "Inner=0", "Spare 2=0"}));
        const QDomElement open = written.documentElement().firstChildElement("OpenCompositeShots");
        QCOMPARE(open.attribute("TimelineId"), QString("aaaaaaaa-0000-0000-0000-000000000001"));
        QCOMPARE(open.elementsByTagName("CompositeShot").size(), 2);
        QCOMPARE(open.firstChildElement("CompositeShot").attribute("Name"), QString("Spare 2"));
        composition::Composition again; media::MediaManager againMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(saved, &again, &againMedia).isSuccess());
        QCOMPARE(again.compositeShots().size(), 2);
        QCOMPARE(again.layers().at(0).clips.at(0).nestedComposition, again.compositeShots().at(0));
        QCOMPARE(again.layers().at(0).clips.at(0).sourceStartSeconds, 0.5);
        QCOMPARE(again.compositeShots().at(0)->layers().size(), 2);
        QVERIFY(again.compositeShots().at(0)->layers().at(0).clips.isEmpty());
        QCOMPARE(again.activeShotId(), QString("aaaaaaaa-0000-0000-0000-000000000001"));

        // A shot deleted in the port leaves the file too.
        again.removeCompositeShot(core::Identifier("cccccccc-0000-0000-0000-000000000003"));
        QVERIFY(project::VegfxSerializer::saveToFile(saved, again, againMedia).isSuccess());
        QDomDocument pruned; QFile prunedFile(saved);
        QVERIFY(prunedFile.open(QIODevice::ReadOnly)); QVERIFY(pruned.setContent(prunedFile.readAll()));
        QCOMPARE(pruned.elementsByTagName("CompositionAsset").size(), 2);
        // A closed tab's shot is not listed as open any more.
        QCOMPARE(pruned.documentElement().firstChildElement("OpenCompositeShots")
                     .elementsByTagName("CompositeShot").size(), 1);
    }
    void nestedShotsCannotLoop() {
        // Inner nests Main (the root) and Spare, and Spare nests Inner: the
        // links that would close a loop stay unresolved, so rendering cannot
        // recurse.
        const auto nest = [](const char* target, const char* id) {
            return QByteArray("<AssetLayer Version=\"17\"><AssetID>") + target
                   + "</AssetID><LayerBase Version=\"2\"><ID>" + id
                   + "</ID><Name>Nest</Name><StartFrame>0</StartFrame><EndFrame>10</EndFrame></LayerBase></AssetLayer>";
        };
        QByteArray xml = severalShotsProject();
        xml.replace("<PointLayer Version=\"17\">",
                    nest("bbbbbbbb-0000-0000-0000-000000000002", "30000000-0000-0000-0000-000000000001")
                        + nest("cccccccc-0000-0000-0000-000000000003", "30000000-0000-0000-0000-000000000002")
                        + "<PointLayer Version=\"17\">");
        xml.replace("<Layers/>", "<Layers>" + nest("aaaaaaaa-0000-0000-0000-000000000001",
                                                   "30000000-0000-0000-0000-000000000003") + "</Layers>");
        QTemporaryDir folder; QVERIFY(folder.isValid());
        const QString path = folder.filePath("Loop.vegfx");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(xml); }
        composition::Composition main; media::MediaManager media;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &main, &media).isSuccess());
        const auto inner = main.compositeShots().at(0);
        const auto spare = main.compositeShots().at(1);
        QCOMPARE(main.layers().at(0).clips.at(0).nestedComposition, inner);
        QVERIFY(!inner->layers().at(0).clips.at(0).nestedComposition);       // the root
        const bool innerNestsSpare = bool(inner->layers().at(1).clips.at(0).nestedComposition);
        const bool spareNestsInner = bool(spare->layers().at(0).clips.at(0).nestedComposition);
        QVERIFY(innerNestsSpare != spareNestsInner);
        // The unresolved link keeps its id, so a save still names the shot.
        QCOMPARE(spare->layers().at(0).clips.at(0).nestedCompositionId.value(),
                 QString("aaaaaaaa-0000-0000-0000-000000000001"));
    }
    void compositeShotFilesRoundTrip() {
        QTemporaryDir folder; QVERIFY(folder.isValid());
        QImage still(8, 8, QImage::Format_RGBA8888); still.fill(Qt::blue);
        const QString stillPath = QDir::cleanPath(folder.filePath("still.png"));
        const QString otherPath = QDir::cleanPath(folder.filePath("other.png"));
        QVERIFY(still.save(stillPath)); QVERIFY(still.save(otherPath));
        media::MediaManager media;
        QVERIFY(media.importFile(stillPath).isSuccess()); QVERIFY(media.importFile(otherPath).isSuccess());
        composition::Composition shot; shot.setName("Lonely"); shot.setSize(640, 360);
        composition::Clip* clip = shot.addClip("Still", core::Identifier("media:" + stillPath), 0, 2);
        QVERIFY(clip);

        // <BiffCompositeShot>: the media the shot uses, then its CompositionAsset.
        const QString file = folder.filePath("Lonely.vegfxcs");
        QVERIFY(project::VegfxSerializer::saveCompositeShot(file, shot, media).isSuccess());
        QDomDocument written; QFile saved(file);
        QVERIFY(saved.open(QIODevice::ReadOnly)); QVERIFY(written.setContent(saved.readAll())); saved.close();
        const QDomElement root = written.documentElement();
        QCOMPARE(root.tagName(), QString("BiffCompositeShot"));
        QCOMPARE(root.attribute("Version"), QString("1"));
        QCOMPARE(root.attribute("AppEdition"), QString("5000"));
        QCOMPARE(root.firstChildElement("Assets").childNodes().size(), 1);   // other.png is not used
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(
                     root.firstChildElement("Assets").firstChildElement().firstChildElement("Filename").text())),
                 stillPath);
        QCOMPARE(root.elementsByTagName("CompositionAsset").size(), 1);
        QVERIFY(root.firstChildElement("Project").isNull());

        // A shot nesting another is refused, as in the reference.
        composition::Composition outer = shot;
        composition::Clip nested; nested.nestedComposition = std::make_shared<composition::Composition>();
        nested.nestedCompositionId = nested.nestedComposition->id();
        outer.addLayer("Nest").clips << nested;
        QVERIFY(project::VegfxSerializer::saveCompositeShot(folder.filePath("Outer.vegfxcs"), outer, media).isFailure());
        QVERIFY(!QFileInfo::exists(folder.filePath("Outer.vegfxcs")));

        // Listed and imported: the media comes along, a taken ID is replaced.
        QVector<project::CompositeShotInfo> offered;
        QVERIFY(project::VegfxSerializer::listCompositeShots(file, &offered).isSuccess());
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered.first().name, QString("Lonely"));
        QCOMPARE(offered.first().size, QSize(640, 360));
        media::MediaManager target;
        QVector<std::shared_ptr<composition::Composition>> imported;
        QVERIFY(project::VegfxSerializer::importCompositeShots(file, {shot.id().value()}, {shot.id().value()},
                                                               &target, &imported).isSuccess());
        QCOMPARE(imported.size(), 1);
        QVERIFY(imported.first()->id() != shot.id());
        QVERIFY(!imported.first()->isPrimary());
        QCOMPARE(imported.first()->layers().size(), 1);
        QVERIFY(target.assetByFilePath(stillPath).isValid());
        QVERIFY(!target.assetByFilePath(otherPath).isValid());
        QCOMPARE(target.assetById(imported.first()->layers().first().clips.first().mediaId).filePath(),
                 media.assetByFilePath(stillPath).filePath());

        // From a project: the shot asked for brings the shot it nests, and a
        // renamed nested shot is still the one its layer points at.
        const QString projectPath = folder.filePath("Shots.vegfx");
        { QFile project(projectPath); QVERIFY(project.open(QIODevice::WriteOnly)); project.write(severalShotsProject()); }
        QVERIFY(project::VegfxSerializer::listCompositeShots(projectPath, &offered).isSuccess());
        QCOMPARE(offered.size(), 3);
        QVERIFY(project::VegfxSerializer::importCompositeShots(
                    projectPath, {"bbbbbbbb-0000-0000-0000-000000000002"},
                    {"aaaaaaaa-0000-0000-0000-000000000001"}, &target, &imported).isSuccess());
        QCOMPARE(imported.size(), 2);
        QCOMPARE(imported.at(0)->name(), QString("Main"));
        QCOMPARE(imported.at(1)->name(), QString("Inner"));
        QVERIFY(imported.at(1)->id().value() != "aaaaaaaa-0000-0000-0000-000000000001");
        const composition::Clip& link = imported.at(0)->layers().at(0).clips.at(0);
        QCOMPARE(link.nestedComposition, imported.at(1));
        QCOMPARE(link.nestedCompositionId, imported.at(1)->id());
        QVERIFY(project::VegfxSerializer::importCompositeShots(projectPath, {"missing"}, {}, &target, &imported)
                    .isFailure());
    }
    void importCompositionDialogPicksShots() {
        QVector<project::CompositeShotInfo> offered(3);
        offered[0].id = "a"; offered[0].name = "First";
        offered[1].id = "b"; offered[1].name = "Second";
        offered[2].id = "c"; offered[2].name = "Third";
        ui::ImportCompositionDialog dialog("Shots.vegfx", offered);
        QCOMPARE(dialog.objectName(), QString("ImportCompositionDialog"));
        QCOMPARE(dialog.findChild<QLineEdit*>("lineEditProjectName")->text(), QString("Shots.vegfx"));
        QVERIFY(dialog.findChild<QLineEdit*>("lineEditProjectName")->isReadOnly());
        auto* list = dialog.findChild<QListWidget*>("listViewCompositeShots"); QVERIFY(list);
        auto* import = dialog.findChild<QToolButton*>("toolButtonImport"); QVERIFY(import);
        QVERIFY(dialog.findChild<QToolButton*>("toolButtonCancel"));
        QCOMPARE(dialog.selectedIds(), QStringList({"a", "b", "c"}));
        list->item(1)->setCheckState(Qt::Unchecked);
        QCOMPARE(dialog.selectedIds(), QStringList({"a", "c"}));
        list->item(0)->setCheckState(Qt::Unchecked); list->item(2)->setCheckState(Qt::Unchecked);
        QVERIFY(!import->isEnabled());
        list->item(2)->setCheckState(Qt::Checked);
        QVERIFY(import->isEnabled());
        dialog.show(); QTest::qWait(10);
        QVERIFY(dialog.grab().save(testArtifactPath(QStringLiteral("import-composition-dialog.png"))));
    }
    void mediaPanelListsCompositeShots() {
        media::MediaManager media;
        ui::MediaPanel panel; panel.setMediaManager(&media);
        QSignalSpy opened(&panel, &ui::MediaPanel::compositeShotActivated);
        QSignalSpy removed(&panel, &ui::MediaPanel::compositeShotRemoveRequested);
        ui::MediaPanel::CompositeShotEntry main; main.id = "main"; main.name = "Main";
        main.size = QSize(1920, 1080); main.durationSeconds = 10; main.primary = true;
        ui::MediaPanel::CompositeShotEntry spare; spare.id = "spare"; spare.name = "Spare";
        spare.size = QSize(1280, 720); spare.durationSeconds = 2;
        panel.setCompositeShots({spare, main});
        auto* list = panel.findChild<QListWidget*>(); QVERIFY(list);
        QCOMPARE(list->count(), 2);
        QVERIFY(list->item(0)->text().startsWith("Main"));
        QVERIFY(list->item(0)->text().contains("[Primary]"));
        QVERIFY(list->item(0)->font().bold());
        QCOMPARE(panel.findChild<QLabel*>("media-panel-count")->text(), QString("2 item(s)"));
        // Activating a shot opens it; the trash button deletes it - neither
        // treats the row as the first media asset.
        emit list->itemActivated(list->item(1));
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.at(0).at(0).toString(), QString("spare"));
        list->setCurrentRow(1);
        QCOMPARE(panel.selectedCompositeShotId(), QString("spare"));
        QCOMPARE(panel.selectedFilePath(), QString());
        panel.findChild<QToolButton*>("MediaPanelTrashButton")->click();
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed.at(0).at(0).toString(), QString("spare"));
    }
    void autoSavesAreSeparateNumberedAndRecoverable() {
        QTemporaryDir folder; QVERIFY(folder.isValid());
        ui::autosave::setFolderOverride(folder.path());
        const auto restore = qScopeGuard([] { ui::autosave::setFolderOverride(QString()); });
        QTemporaryDir projects; QVERIFY(projects.isValid());
        const QString projectPath = projects.filePath("Shot.vegfx");
        // "<base>.vegfx.autosave<N>", numbered on, never beside the project.
        const QString first = ui::autosave::nextFile(projectPath);
        QCOMPARE(QFileInfo(first).fileName(), QString("Shot.vegfx.autosave1"));
        QCOMPARE(QFileInfo(first).absolutePath(), QDir(folder.path()).absolutePath());
        media::MediaManager media;
        project::ProjectSaveOptions options; options.isAutoSave = true; options.autoSaveOf = projectPath;
        QVERIFY(project::VegfxSerializer::saveToFile(first, *comp, media, options).isSuccess());
        QCOMPARE(QFileInfo(ui::autosave::nextFile(projectPath)).fileName(), QString("Shot.vegfx.autosave2"));
        QVERIFY(project::VegfxSerializer::saveToFile(ui::autosave::nextFile(projectPath), *comp, media,
                                                     options).isSuccess());
        // An untitled project gets its own name.
        project::ProjectSaveOptions untitled; untitled.isAutoSave = true;
        const QString loose = ui::autosave::nextFile(QString());
        QCOMPARE(QFileInfo(loose).fileName(), QString("Untitled Project.vegfx.autosave1"));
        QVERIFY(project::VegfxSerializer::saveToFile(loose, *comp, media, untitled).isSuccess());
        // The marker says where each one came from; the list is newest first.
        QCOMPARE(QDir::cleanPath(ui::autosave::projectOf(first)), QDir::cleanPath(projectPath));
        QCOMPARE(ui::autosave::projectOf(loose), QString());
        QCOMPARE(ui::autosave::entries().size(), 3);
        QCOMPARE(ui::autosave::filesFor(projectPath).size(), 2);
        // A recovered auto-save opens as the project it was taken from.
        composition::Composition recovered; media::MediaManager recoveredMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(first, &recovered, &recoveredMedia).isSuccess());
        QCOMPARE(recovered.layers().size(), comp->layers().size());
        // The Recovered Projects dialog lists them and deletes one.
        {
            ui::RecoveredProjectsDialog dialog;
            QCOMPARE(dialog.count(), 3);
            auto* list = dialog.findChild<QTreeWidget*>("treeRecoveredProjects"); QVERIFY(list);
            QVERIFY(list->topLevelItemCount() == 3);
            QStringList names;
            for (int i = 0; i < list->topLevelItemCount(); ++i) names << list->topLevelItem(i)->text(0);
            QVERIFY(names.contains("Shot.vegfx")); QVERIFY(names.contains("Recovered Untitled Project"));
            dialog.show(); QTest::qWait(10);
            QVERIFY(dialog.grab().save(testArtifactPath(QStringLiteral("recovered-projects.png"))));
            list->setCurrentItem(list->topLevelItem(0));
            dialog.findChild<QPushButton*>("pushButtonDeleteProject")->click();
            QCOMPARE(dialog.count(), 2);
        }
        // A manual save clears the project's auto-saves.
        ui::autosave::clearFor(projectPath);
        QCOMPARE(ui::autosave::filesFor(projectPath).size(), 0);
    }
    void learnSidebarIsNative() {
        // No web view any more: the same three entry points as plain widgets.
        ui::LearnSidebar learn;
        QCOMPARE(learn.objectName(), QString("LearnSidebar"));
        QVERIFY(learn.findChild<QWidget*>("learnWebViewSpace"));
        QSignalSpy tutorials(&learn, &ui::LearnSidebar::tutorialsRequested);
        QSignalSpy commands(&learn, &ui::LearnSidebar::commandRequested);
        learn.findChild<QPushButton*>("learnBrowseTutorials")->click();
        learn.findChild<QPushButton*>("learnCreateCompositeShot")->click();
        learn.findChild<QPushButton*>("learnImportMedia")->click();
        QCOMPARE(tutorials.count(), 1);
        QCOMPARE(commands.count(), 2);
        QCOMPARE(commands[0][0].toString(), QString("Create New Composite Shot"));
        QCOMPARE(commands[1][0].toString(), QString("Import Media"));
    }
    void projectSettingsDialogFollowsTheReference() {
        composition::Composition project;
        project.editorSequence().width = 1280; project.editorSequence().height = 720;
        project.editorSequence().fps = 25; project.editorSequence().frameCount = 250;
        project.editorSequence().audioSampleRate = 44100;
        project.projectSettings().bitDepth = 1001; project.projectSettings().useLinearColor = true;
        project.projectSettings().antialiasingMode = 3;
        ui::ProjectSettingsDialog dialog(ui::ProjectSettingsDialog::fromComposition(project), false);
        QCOMPARE(dialog.windowTitle(), QString("Project Settings"));
        QCOMPARE(dialog.findChild<QComboBox*>("comboBoxColorBitDepth")->currentText(),
                 QString("16-bit Float - Linear Color"));
        QCOMPARE(dialog.findChild<QComboBox*>("comboBoxAntialiasingMode")->currentText(), QString("16x MSAA"));
        QCOMPARE(dialog.findChild<QComboBox*>("comboBoxFrameRate")->currentText(), QString("25 (PAL)"));
        QCOMPARE(dialog.findChild<QSpinBox*>("spinBoxWidth")->value(), 1280);
        QCOMPARE(dialog.findChild<QDoubleSpinBox*>("spinBoxDuration")->value(), 10.0);
        dialog.show(); QTest::qWait(10);
        QVERIFY(dialog.grab().save(testArtifactPath(QStringLiteral("project-settings-editor.png"))));
        dialog.findChild<QTabWidget*>("tabWidgetProjectSettings")->setCurrentIndex(1); QTest::qWait(10);
        QVERIFY(dialog.grab().save(testArtifactPath(QStringLiteral("project-settings-rendering.png"))));
        // Preserve aspect ratio keeps 16:9.
        dialog.findChild<QCheckBox*>("checkBoxPreserveAspectRatio")->setChecked(true);
        dialog.findChild<QSpinBox*>("spinBoxWidth")->setValue(1920);
        QCOMPARE(dialog.findChild<QSpinBox*>("spinBoxHeight")->value(), 1080);
        dialog.findChild<QComboBox*>("comboBoxColorBitDepth")->setCurrentIndex(3);   // 32-bit Float
        dialog.findChild<QComboBox*>("comboBoxAntialiasingMode")->setCurrentIndex(0); // 4x MSAA
        ui::ProjectSettingsDialog::apply(dialog.values(), project);
        QCOMPARE(project.editorSequence().width, 1920);
        QCOMPARE(project.editorSequence().height, 1080);
        QCOMPARE(project.editorSequence().audioSampleRate, 44100);
        QCOMPARE(project.projectSettings().bitDepth, 1002);
        QVERIFY(!project.projectSettings().useLinearColor);
        QCOMPARE(project.projectSettings().antialiasingMode, 1);
        ui::ProjectSettingsDialog fresh(ui::ProjectSettingsDialog::fromComposition(project), true);
        QCOMPARE(fresh.windowTitle(), QString("New Project Settings"));
        // ProjectSettings survive a save in the reference's own element.
        auto shot = std::make_shared<composition::Composition>(project);
        QTemporaryDir temp; media::MediaManager media;
        const QString path = temp.filePath("settings.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, media).isSuccess());
        composition::Composition loaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &media).isSuccess());
        QVERIFY(loaded.projectSettings() == project.projectSettings());
        QCOMPARE(loaded.projectId().value(), project.projectId().value());
    }
    void referenceAudioLayerKeepsLevelFadeAndInPoint() {
        const QString project = QStringLiteral(OPENVEGAS_SOURCE_DIR)
            + "/SAMPLES/vegfx_projects/project_1/Project.vegfx";
        if (!QFileInfo::exists(project)) QSKIP("SAMPLES/vegfx_projects/project_1 is not present");
        auto loaded = std::make_shared<composition::Composition>();
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(project::VegfxSerializer::loadFromFile(project, loaded.get(), media.get()).isSuccess());
        int song = -1;
        for (int i = 0; i < loaded->layers().size(); ++i)
            if (loaded->layers()[i].name.endsWith(".mp3")) song = i;
        QVERIFY(song >= 0);
        const auto& layer = loaded->layers()[song];
        QVERIFY(!layer.clips.isEmpty());
        // AssetInstanceStart 1853 at 60 fps and LayerBase speed 1.0146.
        QVERIFY(qAbs(layer.clips[0].sourceStartSeconds - 1853.0 / 60.0) < 1e-6);
        QVERIFY(qAbs(layer.clips[0].speed - 1.0146) < 1e-6);
        // audioLevel keys -60/0 dB at -30.888/-30.395 s and 0/-60 at 29.70/30.05 s.
        const auto& curve = layer.transform.audioLevelCurve;
        QCOMPARE(curve.count(), 4);
        QCOMPARE(layer.audioLevelAtSeconds(0.0, 60.0), 0.0);
        QCOMPARE(layer.audioLevelAtSeconds(29.0, 60.0), 0.0);
        QVERIFY(layer.audioLevelAtSeconds(29.9, 60.0) < -20.0);
        QCOMPARE(layer.audioLevelAtSeconds(31.0, 60.0), -60.0);
        // The song's row shows just Effects and Audio > Level, like the reference.
        panel->setMediaManager(media); panel->setComposition(loaded); QTest::qWait(20);
        QTreeWidgetItem* row = nullptr;
        for (int i = 0; i < tree()->topLevelItemCount(); ++i)
            if (tree()->topLevelItem(i)->data(0, Qt::UserRole).toInt() == song) row = tree()->topLevelItem(i);
        QVERIFY(row);
        QCOMPARE(row->childCount(), 2);
        QCOMPARE(row->child(0)->text(0), QString("Effects"));
        QCOMPARE(row->child(1)->text(0), QString("Audio"));
        QCOMPARE(row->child(1)->child(0)->text(0), QString("Level"));
        QCOMPARE(row->child(1)->child(0)->data(0, Qt::UserRole + 4).toInt(),
                 int(composition::TransformProperty::AudioLevel));
        QVERIFY(row->child(1)->isExpanded());
        tree()->scrollToItem(row->child(1)->child(0)); QTest::qWait(20);
        QVERIFY(panel->grab().save(testArtifactPath(QStringLiteral("timeline-audio-layer.png"))));
        // Written back in the reference's own elements and read again.
        QTemporaryDir temp;
        const QString saved = temp.filePath("song.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(saved, *loaded, *media).isSuccess());
        QFile file(saved); QVERIFY(file.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(file.readAll()); file.close();
        QVERIFY(xml.contains("<AssetInstanceStart>1853</AssetInstanceStart>"));
        QVERIFY(xml.contains("<Name>audioLevel</Name>"));
        composition::Composition again; media::MediaManager againMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(saved, &again, &againMedia).isSuccess());
        int againSong = -1;
        for (int i = 0; i < again.layers().size(); ++i)
            if (again.layers()[i].name.endsWith(".mp3")) againSong = i;
        QVERIFY(againSong >= 0);
        QCOMPARE(again.layers()[againSong].transform.audioLevelCurve.count(), 4);
        QVERIFY(qAbs(again.layers()[againSong].audioLevelAtSeconds(29.9, 60.0)
                     - layer.audioLevelAtSeconds(29.9, 60.0)) < 0.5);
    }
    void visibilityLockAndTime() {
        auto* visible = panel->findChild<QToolButton*>("timelineVisible_0"); visible->click(); QVERIFY(!comp->layers()[0].visible);
        panel->findChild<QToolButton*>("timelineLock_0")->click(); QCoreApplication::processEvents();
        QVERIFY(comp->layers()[0].locked); QVERIFY(!editor<QDoubleSpinBox>(0)->isEnabled());
        auto* time = panel->findChild<QLineEdit*>("timelineTimecode"); time->setText("00:00:16:16");
        QMetaObject::invokeMethod(time, "editingFinished"); QVERIFY(qAbs(panel->playhead() - (16. + 16./30.)) < .0001);
        time->setText("bad"); QMetaObject::invokeMethod(time, "editingFinished"); QCOMPARE(time->text(), QString("00:00:16:16"));
        panel->findChild<QSlider*>("timelineZoomSlider")->setValue(600); QVERIFY(panel->zoom() > .5);
    }
    void transformKeysAndPersistence() {
        auto* transform = tree()->topLevelItem(0)->child(3); transform->setExpanded(true);
        auto* position = transform->child(2); tree()->setCurrentItem(position);
        panel->toggleSelectedKeyFrame(); QVERIFY(!comp->layers()[0].transform.positionXCurve.isEmpty());
        panel->setPlayheadPosition(1);
        auto* root = panel->findChild<QWidget*>("timelineTransform_0_2_0"); QVERIFY(root);
        root->findChild<QDoubleSpinBox*>()->setValue(100);
        QCOMPARE(comp->layers()[0].transform.positionAt(15).x(), 50.);
        panel->setPlayheadPosition(0); panel->goToNextKeyFrame(); QCOMPARE(panel->playhead(), 1.);
        auto& effectCurve = comp->layerRef(0).clips[0].effects[0].animation[0];
        effectCurve.setDefaultValue(0.0); effectCurve.set(0, 0.0);
        effectCurve.set(30, 12.5, composition::TemporalType::EasyEase);
        composition::LayerMask mask; mask.name = "Ellipse Mask";
        mask.shape = composition::MaskShape::Ellipse; mask.bounds = QRectF(10, 20, 120, 80);
        mask.points = {QPointF(10, 20), QPointF(130, 20), QPointF(130, 100)};
        mask.feather = 6.5; mask.opacity = .75; mask.inverted = true;
        comp->layerRef(0).masks.append(mask);
        composition::MotionTrack motion;
        motion.name = "Face"; motion.point = QPointF(40, 30);
        motion.sampleRadius = 6; motion.searchRadius = 18;
        motion.xCurve.set(0, 40.0); motion.xCurve.set(10, 45.0);
        motion.yCurve.set(0, 30.0); motion.yCurve.set(10, 33.0);
        comp->layerRef(0).motionTracks.append(motion);
        comp->layerRef(0).visible = false; comp->layerRef(0).locked = true; comp->layerRef(0).labelColor = Qt::red;
        QTemporaryDir temp; media::MediaManager media;
        const QString mediaPath = temp.filePath("shot.png");
        QFile mediaFile(mediaPath); QVERIFY(mediaFile.open(QIODevice::WriteOnly)); mediaFile.close();
        QVERIFY(media.importFile(mediaPath).isSuccess());
        comp->layerRef(0).clips[0].mediaId = core::Identifier(QStringLiteral("media:") + mediaPath);
        composition::Clip second = comp->layers()[0].clips[0];
        second.startSeconds = 4.25; second.durationSeconds = 2.5;
        second.sourceStartSeconds = .75; second.speed = 1.25; second.audioLevel = -6.0;
        second.effects[0].parameterValues[0] = "19.5";
        comp->layerRef(0).clips.append(second);
        comp->layerRef(0).muted = true;
        const auto path = temp.filePath("timeline.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *comp, media).isSuccess());
        composition::Composition restored;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &restored, &media).isSuccess());
        QCOMPARE(restored.durationSeconds(), 50.0);
        QVERIFY(!restored.layers()[0].visible); QVERIFY(restored.layers()[0].locked); QCOMPARE(restored.layers()[0].labelColor, QColor(Qt::red));
        QCOMPARE(restored.layers()[0].transform.positionAt(15).x(), 50.);
        QVERIFY(restored.layers()[0].muted);
        QCOMPARE(restored.layers()[0].clips.size(), 2);
        QCOMPARE(restored.layers()[0].clips[1].startSeconds, 4.25);
        QCOMPARE(restored.layers()[0].clips[1].durationSeconds, 2.5);
        QCOMPARE(restored.layers()[0].clips[1].sourceStartSeconds, .75);
        QCOMPARE(restored.layers()[0].clips[1].speed, 1.25);
        QCOMPARE(restored.layers()[0].clips[1].audioLevel, -6.0);
        QCOMPARE(restored.layers()[0].clips[1].effects[0].parameterValues[0], QString("19.5"));
        QCOMPARE(restored.layers()[0].clips[0].effects.size(), 1);
        const auto& restoredEffect = restored.layers()[0].clips[0].effects[0];
        QVERIFY(restoredEffect.isAnimated(0));
        QCOMPARE(restoredEffect.animation[0].valueAt(30).toDouble(), 12.5);
        QCOMPARE(restoredEffect.animation[0].at(30)->temporal, composition::TemporalType::EasyEase);
        QCOMPARE(restored.layers()[0].masks.size(), 1);
        QCOMPARE(restored.layers()[0].masks[0].shape, composition::MaskShape::Ellipse);
        QCOMPARE(restored.layers()[0].masks[0].bounds, QRectF(10, 20, 120, 80));
        QCOMPARE(restored.layers()[0].masks[0].points, mask.points);
        QCOMPARE(restored.layers()[0].masks[0].feather, 6.5);
        QVERIFY(restored.layers()[0].masks[0].inverted);
        QCOMPARE(restored.layers()[0].motionTracks.size(), 1);
        QCOMPARE(restored.layers()[0].motionTracks[0].name, QString("Face"));
        QCOMPARE(restored.layers()[0].motionTracks[0].pointAt(10), QPointF(45, 33));
        QCOMPARE(restored.layers()[0].motionTracks[0].sampleRadius, 6);
    }
    void automaticMotionTracking() {
        QVector<QImage> frames;
        for (int frame = 0; frame < 4; ++frame) {
            QImage image(80, 60, QImage::Format_Grayscale8); image.fill(20);
            const int ox = 25 + frame * 3, oy = 20 + frame * 2;
            for (int y = -5; y <= 5; ++y) for (int x = -5; x <= 5; ++x)
                image.setPixelColor(ox + x, oy + y,
                    QColor(qBound(0, 120 + x * 9 + y * 5, 255), 0, 0));
            frames.append(image);
        }
        const QVector<QPointF> points = composition::MotionTracker::track(
            frames, QPointF(25, 20), 5, 10);
        QCOMPARE(points.size(), 4);
        QCOMPARE(points[1], QPointF(28, 22));
        QCOMPARE(points[2], QPointF(31, 24));
        QCOMPARE(points[3], QPointF(34, 26));

        auto* addTrack = panel->findChild<QToolButton*>("timelineAddTrack_0");
        QVERIFY(addTrack); addTrack->click();
        QTRY_COMPARE(comp->layers()[0].motionTracks.size(), 1);
        QTRY_VERIFY(panel->findChild<QDoubleSpinBox*>("timelineTrackX_0_0"));
        QTRY_VERIFY(panel->findChild<QSpinBox*>("timelineTrackSearch_0_0"));
        ui::EffectInspector controls;
        controls.setComposition(comp); controls.setSelection(0, 0);
        QVERIFY(controls.findChild<QDoubleSpinBox*>("controlsTrackX_0"));
        auto* analyze = controls.findChild<QToolButton*>("controlsTrackAnalyze_0");
        QVERIFY(analyze); QSignalSpy analyzeSpy(&controls, &ui::EffectInspector::motionTrackingRequested);
        analyze->click(); QCOMPARE(analyzeSpy.count(), 1);
        QCOMPARE(history.count(), 1);
        history.undo(); QCOMPARE(comp->layers()[0].motionTracks.size(), 0);
    }
    void historyPanelToolbar() {
        ui::HistoryPanel historyPanel;
        QCOMPARE(historyPanel.objectName(), QStringLiteral("HistoryPanel"));
        QVERIFY(historyPanel.findChild<QWidget*>(QStringLiteral("HistoryWidget")));
        auto* undo = historyPanel.findChild<QToolButton*>(QStringLiteral("toolButtonUndo"));
        auto* redo = historyPanel.findChild<QToolButton*>(QStringLiteral("toolButtonRedo"));
        auto* clear = historyPanel.findChild<QToolButton*>(QStringLiteral("toolButtonClear"));
        QVERIFY(undo); QVERIFY(redo); QVERIFY(clear);
        QVERIFY(!undo->isEnabled()); QVERIFY(!redo->isEnabled()); QVERIFY(!clear->isEnabled());
        QSignalSpy undoSpy(&historyPanel, &ui::HistoryPanel::undoRequested);
        QSignalSpy redoSpy(&historyPanel, &ui::HistoryPanel::redoRequested);
        QSignalSpy clearSpy(&historyPanel, &ui::HistoryPanel::clearRequested);
        historyPanel.addEntry(QStringLiteral("New Project"));
        historyPanel.addEntry(QStringLiteral("Edit"));
        historyPanel.setCanUndo(true); historyPanel.setCanRedo(true);
        undo->click(); redo->click(); clear->click();
        QCOMPARE(undoSpy.count(), 1); QCOMPARE(redoSpy.count(), 1); QCOMPARE(clearSpy.count(), 1);
    }
    void startPanelCommands() {
        ui::StartPanel start;
        QVERIFY(start.findChild<QWidget*>(QStringLiteral("StartPanelWidget")));
        QVERIFY(start.findChild<QWidget*>(QStringLiteral("ProjectSideBarWidget")));
        auto* importFile = start.findChild<QToolButton*>(QStringLiteral("AddMedia"));
        auto* newProject = start.findChild<QToolButton*>(QStringLiteral("toolButtonNew"));
        auto* openProject = start.findChild<QToolButton*>(QStringLiteral("toolButtonOpen"));
        QVERIFY(importFile); QVERIFY(newProject); QVERIFY(openProject);
        QSignalSpy importSpy(&start, &ui::StartPanel::importFileRequested);
        QSignalSpy newSpy(&start, &ui::StartPanel::newProjectRequested);
        QSignalSpy openSpy(&start, &ui::StartPanel::openProjectRequested);
        importFile->click(); newProject->click(); openProject->click();
        QCOMPARE(importSpy.count(), 1); QCOMPARE(newSpy.count(), 1); QCOMPARE(openSpy.count(), 1);
    }
    void libraryLoadsConfiguredFolders() {
        QTemporaryDir temp;
        QDir root(temp.path()); QVERIFY(root.mkpath(QStringLiteral("Media")));
        QVERIFY(root.mkpath(QStringLiteral("Templates")));
        QFile mediaFile(root.filePath(QStringLiteral("Media/clip.mov")));
        QVERIFY(mediaFile.open(QIODevice::WriteOnly)); mediaFile.close();
        QFile templateFile(root.filePath(QStringLiteral("Templates/title.hfcs")));
        QVERIFY(templateFile.open(QIODevice::WriteOnly)); templateFile.close();
        ui::LibraryPanel library;
        QCOMPARE(library.objectName(), QStringLiteral("library"));
        library.setLibraryPaths(root.filePath(QStringLiteral("Media")),
                                root.filePath(QStringLiteral("Templates")));
        QTRY_COMPARE(library.findChild<QListWidget*>(QStringLiteral("libraryList"))->count(), 4);
        QVERIFY(library.findChild<QLabel*>(QStringLiteral("libraryStatus"))->text().contains('2'));
    }
    void mediaPanelCommandsAndDisplay() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("still.png"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        media::MediaManager manager; QVERIFY(manager.importFile(path).isSuccess());
        ui::MediaPanel mediaPanel; mediaPanel.setMediaManager(&manager);
        QCOMPARE(mediaPanel.findChild<QListWidget*>(QStringLiteral("listView"))->count(), 1);
        QVERIFY(mediaPanel.findChild<QToolButton*>(QStringLiteral("MediaPanelTrashButton")));
        QVERIFY(mediaPanel.findChild<QToolButton*>(QStringLiteral("MediaPanelNewFolderButton")));
        QVERIFY(mediaPanel.findChild<QToolButton*>(QStringLiteral("toolButtonMediaThumbnails")));
        auto* arrange = mediaPanel.findChild<QToolButton*>(QStringLiteral("mediaArrangeButton"));
        auto* group = mediaPanel.findChild<QToolButton*>(QStringLiteral("mediaGroupButton"));
        auto* search = mediaPanel.findChild<QLineEdit*>(QStringLiteral("media-panel-search"));
        QVERIFY(arrange); QVERIFY(arrange->menu());
        QVERIFY(group); QVERIFY(group->menu());
        QVERIFY(search);
        QCOMPARE(search->placeholderText(), QStringLiteral("Search in Project Media"));
        auto* importButton = mediaPanel.findChild<QToolButton*>(QStringLiteral("toolButtonImport"));
        QVERIFY(importButton);
        QSignalSpy importSpy(&mediaPanel, &ui::MediaPanel::importCommandRequested);
        importButton->click(); QCOMPARE(importSpy.count(), 1);
        auto* composite = mediaPanel.findChild<QToolButton*>(
            QStringLiteral("MediaPanelCompositeShotButton"));
        QVERIFY(composite);
        QSignalSpy compositeSpy(&mediaPanel, &ui::MediaPanel::newCompositeShotRequested);
        composite->click(); QCOMPARE(compositeSpy.count(), 1);
    }

    void mediaLabelsUndoAndOfflineRoundTrip() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString first = temp.filePath("a.png"), second = temp.filePath("b.png");
        QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::white);
        QVERIFY(image.save(first)); QVERIFY(image.save(second));
        media::MediaManager manager;
        QVERIFY(manager.importFile(first).isSuccess()); QVERIFY(manager.importFile(second).isSuccess());
        const auto firstId = manager.assetByFilePath(first).id(), secondId = manager.assetByFilePath(second).id();
        QUndoStack undo;
        auto mediaPanel = std::make_unique<ui::MediaPanel>();
        mediaPanel->setMediaManager(&manager); mediaPanel->setUndoStack(&undo);
        QSignalSpy modified(mediaPanel.get(), &ui::MediaPanel::mediaMetadataModified);
        auto* list = mediaPanel->findChild<QListWidget*>("listView");
        list->setCurrentRow(1);
        mediaPanel->setMediaLabel(secondId, QColor(20, 40, 60, 128));
        QCOMPARE(mediaPanel->selectedFilePath(), second);
        QCOMPARE(list->currentItem()->data(Qt::UserRole + 2).value<QColor>(), QColor(20, 40, 60, 128));
        QCOMPARE(undo.count(), 1);
        mediaPanel->resize(420, 280); mediaPanel->show();
        QTest::mouseClick(mediaPanel->findChild<QToolButton*>("toolButtonMediaOptions"), Qt::LeftButton);
        QTRY_VERIFY(list->visualItemRect(list->currentItem()).height() < 32);
        QVERIFY(mediaPanel->grab().save(testArtifactPath(QStringLiteral("media-labels.png"))));
        mediaPanel->setMediaLabel(secondId, QColor(20, 40, 60, 128)); QCOMPARE(undo.count(), 1);
        manager.removeAsset(firstId); // Asset index changes; Undo must still address b.png.
        undo.undo(); QVERIFY(!manager.assetById(secondId).labelColor().isValid());
        undo.redo(); QCOMPARE(manager.assetById(secondId).labelColor(), QColor(20, 40, 60, 128));
        mediaPanel->setMediaLabel(secondId, QColor());
        QVERIFY(!manager.assetById(secondId).labelColor().isValid());
        undo.undo(); QCOMPARE(manager.assetById(secondId).labelColor(), QColor(20, 40, 60, 128));
        QVERIFY(modified.count() >= 4);

        const QString missing = temp.filePath("missing.wav");
        manager.registerMissingFile(missing);
        manager.assetByFilePathForEdit(missing)->setLabelColor(Qt::green);
        composition::Composition scene;
        scene.addClip("Image", secondId, 0, 1);
        const QString project = temp.filePath("labels.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(project, scene, manager).isSuccess());
        QVERIFY(QFile::remove(second));
        media::MediaManager loaded;
        composition::Composition restored;
        QVERIFY(project::VegfxSerializer::loadFromFile(project, &restored, &loaded).isSuccess());
        QCOMPARE(loaded.assetByFilePath(second).labelColor(), QColor(20, 40, 60, 128));
        QCOMPARE(loaded.assetByFilePath(missing).labelColor(), QColor(Qt::green));
        mediaPanel->setMediaManager(&loaded);
        undo.undo(); // Commands belonging to the outgoing manager are ignored.
        QCOMPARE(loaded.assetByFilePath(second).labelColor(), QColor(20, 40, 60, 128));
        mediaPanel.reset(); undo.redo(); // Closing the panel is safe.
    }

    void projectDefaultsUseStableTemplateAndEditorDuration() {
        const QSettings::Format format = QSettings::IniFormat;
        QSettings settings(format, QSettings::UserScope, app::Settings::organizationName(), app::Settings::applicationName());
        const QStringList keys{"Options/DefaultTemplateId", "Options/DefaultTemplate", "Options/EditorDefaultDuration", "Options/CompositeShotDefaultDuration"};
        QMap<QString, QVariant> saved; for (const auto& key : keys) saved[key] = settings.value(key);
        const auto restore = qScopeGuard([&] { for (const auto& key : keys) { if (saved[key].isValid()) settings.setValue(key, saved[key]); else settings.remove(key); } });
        settings.setValue(keys[0], "fullhd60"); settings.setValue(keys[1], "4K UHD @ 30 fps");
        settings.setValue(keys[2], "00:02:03.500"); settings.setValue(keys[3], "00:00:17.250"); settings.sync();
        composition::Composition scene; app::applyNewProjectDefaults(scene);
        QCOMPARE(scene.width(), 1920); QCOMPARE(scene.height(), 1080); QCOMPARE(scene.fpsNumerator(), 60);
        QCOMPARE(scene.durationSeconds(), 17.25); QCOMPARE(scene.editorSequence().frameCount, 7410LL);
        QCOMPARE(scene.editorSequence().fps, 60.0);
        QTemporaryDir temp; media::MediaManager media;
        QVERIFY(project::VegfxSerializer::saveToFile(temp.filePath("defaults.vegfx"), scene, media).isSuccess());
        composition::Composition loaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(temp.filePath("defaults.vegfx"), &loaded, &media).isSuccess());
        QCOMPARE(loaded.editorSequence().frameCount, 7410LL); QCOMPARE(loaded.editorSequence().fps, 60.0);
        settings.remove(keys[0]); settings.setValue(keys[1], "4K UHD @ 30 fps"); settings.sync();
        app::applyNewProjectDefaults(scene); QCOMPARE(scene.width(), 3840); QCOMPARE(scene.height(), 2160);
        settings.setValue(keys[2], "invalid"); settings.setValue(keys[1], "unknown"); settings.sync();
        app::applyNewProjectDefaults(scene); QCOMPARE(scene.width(), 1920); QCOMPARE(scene.editorSequence().frameCount, 9000LL);
        // Existing projects retain their own settings when preferences change.
        QCOMPARE(loaded.fpsNumerator(), 60); QCOMPARE(loaded.editorSequence().frameCount, 7410LL);
    }
    void failedProjectLoadPreservesCurrentState() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString path = temp.filePath("image.png");
        QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::red); QVERIFY(image.save(path));
        media::MediaManager manager; QVERIFY(manager.importFile(path).isSuccess());
        manager.assetByFilePathForEdit(path)->setLabelColor(Qt::cyan);
        composition::Composition scene; scene.setName("Current"); scene.setDurationSeconds(42);
        scene.addClip("Layer", manager.assetByFilePath(path).id(), 0, 2);
        const auto layerId = scene.layers().first().id;
        QByteArray layout("current workspace");
        QFile broken(temp.filePath("broken.vegfx")); QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("<VegasEffectsProject><Project>"); broken.close();
        QVERIFY(project::VegfxSerializer::loadFromFile(broken.fileName(), &scene, &manager, &layout).isFailure());
        QCOMPARE(scene.name(), QStringLiteral("Current")); QCOMPARE(scene.durationSeconds(), 42.0);
        QCOMPARE(scene.layers().first().id, layerId);
        QCOMPARE(manager.assets().size(), 1); QCOMPARE(manager.assetByFilePath(path).labelColor(), QColor(Qt::cyan));
        QCOMPARE(layout, QByteArray("current workspace"));
        QVERIFY(project::VegfxSerializer::loadFromFile(temp.filePath("absent.vegfx"), &scene, &manager, &layout).isFailure());
        QCOMPARE(layout, QByteArray("current workspace")); QCOMPARE(scene.layers().first().id, layerId);
    }
    void metersAndViewerToggle() {
        ui::AudioMetersPanel meters;
        QCOMPARE(meters.objectName(), QStringLiteral("AudioMetersPanel"));
        QVERIFY(meters.findChild<QWidget*>(QStringLiteral("AudioMetersWidget")));
        QVERIFY(meters.findChild<QWidget*>(QStringLiteral("InputLevels")));
        QVERIFY(meters.findChild<QWidget*>(QStringLiteral("OutputLevels")));
        meters.setInputLevels(.25, .5); meters.setOutputLevels(.75, 1.0);
        meters.setHoldPeaks(false); QVERIFY(!meters.holdPeaks());
        meters.setHoldPeaks(true); QVERIFY(meters.holdPeaks()); meters.resetPeaks();
        ui::ViewerTransportBar transport;
        auto* button = transport.findChild<QToolButton*>(QStringLiteral("audioMeterIcon"));
        QVERIFY(button);
        QVERIFY(button->text().isEmpty());
        QCOMPARE(button->toolTip(), QStringLiteral("Audio Meters"));
        transport.setAudioLevels(.25, .8);
        transport.show();
        QTest::qWait(20);
        QVERIFY(transport.grab().save(testArtifactPath(QStringLiteral("viewer-transport-meter.png"))));
        QSignalSpy toggled(&transport, &ui::ViewerTransportBar::metersToggled);
        button->click(); QCOMPARE(toggled.count(), 1);
    }
    void dockTitleBarControlsPanelState() {
        QMainWindow window;
        QDockWidget dock(QStringLiteral("Controls"), &window);
        QDockWidget effects(QStringLiteral("Effects"), &window);
        dock.setObjectName(QStringLiteral("controlsPanel"));
        effects.setObjectName(QStringLiteral("effectsPanel"));
        window.addDockWidget(Qt::RightDockWidgetArea, &dock);
        window.addDockWidget(Qt::RightDockWidgetArea, &effects);
        window.tabifyDockWidget(&dock, &effects);
        ui::installDockTitleBar(&dock);
        ui::installDockTitleBar(&effects);
        QVERIFY(dock.titleBarWidget());
        QVERIFY(dock.titleBarWidget()->findChild<QToolButton*>(
            QStringLiteral("dockPanelMenu")));
        auto* label = dock.titleBarWidget()->findChild<QLabel*>(
            QStringLiteral("dockPanelTitle"));
        QVERIFY(label);
        QCOMPARE(label->text(), QStringLiteral("Controls"));
        window.resize(420, 240);
        window.show();
        QTest::qWait(20);
        ui::installDockTabMenus(&window);
        QVERIFY(window.findChild<QToolButton*>(QStringLiteral("dockPanelTabMenu")));
        QVERIFY(window.grab().save(testArtifactPath(QStringLiteral("dock-tabs.png"))));

        dock.setWindowTitle(QStringLiteral("Controls 2"));
        QCOMPARE(label->text(), QStringLiteral("Controls 2"));
        dock.setFloating(true);
        QVERIFY(dock.isFloating());
        window.addDockWidget(Qt::LeftDockWidgetArea, &dock);
        dock.setFloating(false);
        QCOMPARE(window.dockWidgetArea(&dock), Qt::LeftDockWidgetArea);
        QTest::qWait(20);
        QVERIFY(window.grab().save(testArtifactPath(QStringLiteral("dock-title-bar.png"))));
    }
    void viewerTimelineDuration() {
        ui::ViewerTransportBar transport;
        transport.setFrameRate(30, 1);
        transport.setDuration(10.0);
        transport.setTimecode(5.0);
        auto* current = transport.findChild<QLineEdit*>(QStringLiteral("spinBoxCurrentTime"));
        auto* duration = transport.findChild<QLineEdit*>(QStringLiteral("spinBoxFrameCount"));
        auto* scrubber = transport.findChild<QSlider*>(QStringLiteral("sliderCurrentFrame"));
        QVERIFY(current); QVERIFY(duration); QVERIFY(scrubber);
        QCOMPARE(current->text(), QStringLiteral("00:00:05:00"));
        QCOMPARE(duration->text(), QStringLiteral("00:00:10:00"));
        QCOMPARE(scrubber->value(), 500);

        QSignalSpy changed(&transport, &ui::ViewerTransportBar::durationChangeRequested);
        duration->setText(QStringLiteral("00:00:12:15"));
        QVERIFY(QMetaObject::invokeMethod(duration, "editingFinished"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.takeFirst().at(0).toDouble(), 12.5);

        // Applying a new duration preserves the current five-second playhead
        // instead of resetting the scrubber to the beginning.
        transport.setDuration(12.5);
        QCOMPARE(current->text(), QStringLiteral("00:00:05:00"));
        QCOMPARE(scrubber->value(), 400);

        duration->setText(QStringLiteral("00:61:00:00"));
        QVERIFY(QMetaObject::invokeMethod(duration, "editingFinished"));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(duration->text(), QStringLiteral("00:00:12:15"));

        // A project duration replaces the blank project's ten-second range;
        // its last frame is the right edge of the transport scrubber.
        transport.setDuration(70.0);
        transport.setTimecode(70.0);
        QCOMPARE(duration->text(), QStringLiteral("00:01:10:00"));
        QCOMPARE(current->text(), QStringLiteral("00:01:10:00"));
        QCOMPARE(scrubber->value(), scrubber->maximum());
    }
    void timelineDurationUndo() {
        QSignalSpy changes(panel.get(), &ui::TimelineWidget::compositionPropertiesChanged);
        panel->setPlayheadPosition(40.0);
        panel->setCompositionDuration(12.5);
        QCOMPARE(comp->durationSeconds(), 12.5);
        QCOMPARE(panel->playhead(), 12.5);
        QCOMPARE(history.count(), 1);
        QCOMPARE(changes.count(), 1);
        history.undo();
        QCOMPARE(comp->durationSeconds(), 50.0);
        QCOMPARE(changes.count(), 2);
        history.redo();
        QCOMPARE(comp->durationSeconds(), 12.5);
        QCOMPARE(changes.count(), 3);
    }
    void layoutNativeFormAndControls() {
        ui::LayoutPanel layout;
        layout.resize(340, 400);
        layout.show();
        QTest::qWait(50);
        auto* mirror = layout.findChild<QToolButton*>("toolButtonMirrorVertical");
        auto* rotate = layout.findChild<QToolButton*>("toolButtonClockWise");
        auto* x = layout.findChild<QDoubleSpinBox*>("spinBoxX");
        auto* y = layout.findChild<QDoubleSpinBox*>("spinBoxY");
        auto* width = layout.findChild<QDoubleSpinBox*>("spinBoxWidth");
        auto* height = layout.findChild<QDoubleSpinBox*>("spinBoxHeight");
        auto* link = layout.findChild<QToolButton*>("toolButtonScaleLinked");
        auto* alignTo = layout.findChild<QComboBox*>("comboBoxAlignTo");
        auto* right = layout.findChild<QToolButton*>("toolButtonAlignHorizontalRight");
        auto* scroll = layout.findChild<QScrollArea*>("layoutScrollArea");
        QVERIFY(mirror); QVERIFY(rotate); QVERIFY(x); QVERIFY(y); QVERIFY(width);
        QVERIFY(height); QVERIFY(link); QVERIFY(alignTo); QVERIFY(right); QVERIFY(scroll);
        QVERIFY(!mirror->isEnabled()); QVERIFY(!rotate->isEnabled());
        QVERIFY(!x->isEnabled()); QVERIFY(!alignTo->isEnabled());
        for (const auto* button : layout.findChildren<QToolButton*>()) {
            if (!button->property("layoutIcon").toBool()) continue;
            QVERIFY2(!button->icon().isNull(), qPrintable(button->objectName()));
            QVERIFY2(!button->icon().pixmap(16, 16, QIcon::Disabled).isNull(),
                     qPrintable(button->objectName()));
        }
        layout.setFrameRect(QRectF(0, 0, 640, 360));
        layout.setSelectionBounds(QRectF(10, 20, 160, 90));
        QVERIFY(mirror->isEnabled()); QVERIFY(rotate->isEnabled());
        QCOMPARE(x->value(), 90.0); QCOMPARE(y->value(), 65.0);
        QCOMPARE(width->value(), 160.0); QCOMPARE(height->value(), 90.0);
        QSignalSpy edited(&layout, &ui::LayoutPanel::boundsEdited);
        x->setFocus(); QCoreApplication::processEvents();
        layout.setDirection(ui::LayoutPanel::Direction::TopLeft);
        QCOMPARE(x->value(), 10.0); QCOMPARE(y->value(), 20.0);
        QCOMPARE(edited.count(), 0); // Changing the reference point must not move the layer.
        link->setChecked(true);
        width->setValue(320);
        QMetaObject::invokeMethod(width, "editingFinished");
        QCOMPARE(edited.count(), 1);
        QCOMPARE(edited.takeFirst().at(0).toRectF(), QRectF(10, 20, 320, 180));
        QCOMPARE(height->value(), 180.0);
        alignTo->setCurrentIndex(0);
        QVERIFY(!right->isEnabled()); // A single object cannot align to itself.
        alignTo->setCurrentIndex(1);
        QVERIFY(right->isEnabled());
        right->click();
        QCOMPARE(edited.count(), 1);
        QCOMPARE(edited.takeFirst().at(0).toRectF().right(), 640.0);
        QSignalSpy mirrored(&layout, &ui::LayoutPanel::mirrorRequested);
        QSignalSpy rotated(&layout, &ui::LayoutPanel::rotateRequested);
        mirror->click(); rotate->click();
        QCOMPARE(mirrored.count(), 1);
        QCOMPARE(mirrored.first().first().value<Qt::Orientation>(), Qt::Vertical);
        QCOMPARE(rotated.first().first().toInt(), 90);
        auto* ccw = layout.findChild<QToolButton*>("toolButtonCounterClockWise");
        QVERIFY(ccw); ccw->click();
        QCOMPARE(ccw->mapTo(&layout, QPoint()).x() - mirror->mapTo(&layout, QPoint()).x(), 58);
        QCOMPARE(rotated.last().first().toInt(), -90);
        auto* heading = layout.findChild<QLabel*>("layoutAlignmentHeading");
        QVERIFY(heading);
        QVERIFY(heading->mapTo(&layout, QPoint()).y() < alignTo->mapTo(&layout, QPoint()).y());
        QVERIFY(scroll->horizontalScrollBar()->maximum() == 0);
        QVERIFY(layout.grab().save(testArtifactPath("layout-panel.png")));
        layout.resize(520, 400); QCoreApplication::processEvents();
        // Orientation buttons stay at the left rather than spreading over the panel.
        QCOMPARE(rotate->mapTo(&layout, QPoint()).x() - mirror->mapTo(&layout, QPoint()).x(), 82);
        QVERIFY(layout.grab().save(testArtifactPath("layout-panel-wide.png")));
        layout.setSelectionBounds(QVector<QRectF>());
        QVERIFY(!mirror->isEnabled()); QVERIFY(!width->isEnabled());
        QVERIFY(layout.grab().save(testArtifactPath("layout-panel-empty.png")));
    }
    void layoutEditsPreserveAnimationAndUndo() {
        auto scene = std::make_shared<composition::Composition>();
        scene->addLayer("A"); scene->addLayer("B");
        auto& transform = scene->layerRef(0).transform;
        transform.rotationCurve.set(0, 0.0);
        transform.rotationCurve.set(10, 20.0, composition::TemporalType::ManualBezier);
        auto* key = transform.rotationCurve.keyAt(10);
        key->incomingHandle = QPointF(-3, 0.2);
        key->outgoingHandle = QPointF(4, 0.8);
        key->incomingInfluence = .3;
        key->handlesLocked = true;
        const auto originalKey = *key;
        transform.position = QPointF(5, 7);
        transform.positionXCurve.set(0, 5.0);
        QVector<ui::LayoutTransformChange> changes;
        for (const auto& layer : scene->layers()) {
            ui::LayoutTransformChange change{layer.id, layer.transform, layer.transform};
            ui::writeLayoutValue(change.after.rotationCurve, 10,
                                 change.after.rotationAt(10) + 90, change.after.rotationDegrees);
            ui::writeLayoutPoint(change.after.positionXCurve, change.after.positionYCurve,
                                 10, QPointF(30, 40), change.after.position);
            changes.append(change);
        }
        QUndoStack stack;
        int notifications = 0;
        stack.push(new ui::LayoutTransformCommand(scene, changes,
                    [&notifications] { ++notifications; }, "Layout"));
        QCOMPARE(stack.count(), 1); QCOMPARE(notifications, 1);
        QCOMPARE(scene->layers()[0].transform.rotationAt(10), 110.0);
        QCOMPARE(scene->layers()[1].transform.rotationAt(10), 90.0);
        const auto* edited = scene->layers()[0].transform.rotationCurve.at(10);
        QCOMPARE(edited->id, originalKey.id);
        QCOMPARE(edited->temporal, originalKey.temporal);
        QCOMPARE(edited->incomingHandle, originalKey.incomingHandle);
        QCOMPARE(edited->outgoingHandle, originalKey.outgoingHandle);
        QCOMPARE(edited->incomingInfluence, originalKey.incomingInfluence);
        QCOMPARE(edited->handlesLocked, originalKey.handlesLocked);
        QCOMPARE(scene->layers()[0].transform.rotationCurve.at(0)->value.toDouble(), 0.0);
        QCOMPARE(scene->layers()[0].transform.positionYCurve.defaultValue().toDouble(), 7.0);
        QCOMPARE(scene->layers()[0].transform.positionAt(10), QPointF(30, 40));
        // Reordering the stack must not redirect the command to the wrong layer.
        const auto a = scene->layers().first();
        QVERIFY(scene->removeLayer(0)); QVERIFY(scene->insertLayer(1, a));
        stack.undo(); QCOMPARE(notifications, 2);
        QCOMPARE(scene->layers()[0].transform.rotationAt(10), 0.0);
        QCOMPARE(scene->layers()[1].transform.rotationAt(10), 20.0);
        QCOMPARE(scene->layers()[1].transform.positionAt(10), QPointF(5, 7));
        stack.redo(); QCOMPARE(notifications, 3);
        QCOMPARE(scene->layers()[0].transform.rotationAt(10), 90.0);
        QCOMPARE(scene->layers()[1].transform.rotationAt(10), 110.0);
    }
    void layoutMultipleSelection() {
        ui::LayoutPanel layout;
        QVector<QRectF> boxes{QRectF(10, 10, 10, 10), QRectF(45, 20, 10, 10),
                              QRectF(100, 30, 10, 10)};
        layout.setSelectionBounds(boxes);
        auto* alignTo = layout.findChild<QComboBox*>(QStringLiteral("comboBoxAlignTo"));
        QVERIFY(alignTo); alignTo->setCurrentIndex(0);
        auto* alignLeft = layout.findChild<QToolButton*>(QStringLiteral("toolButtonAlignHorizontalLeft"));
        auto* distribute = layout.findChild<QToolButton*>(QStringLiteral("toolButtonDistributeHorizontally"));
        QVERIFY(alignLeft->isEnabled()); QVERIFY(distribute->isEnabled());
        QSignalSpy changed(&layout, &ui::LayoutPanel::selectionBoundsEdited);
        alignLeft->click(); QCOMPARE(changed.count(), 1);
        const QVector<QRectF> aligned = changed.takeFirst().at(0).value<QVector<QRectF>>();
        QCOMPARE(aligned.size(), 3); QCOMPARE(aligned[0].left(), aligned[1].left());
        layout.setSelectionBounds(boxes);
        distribute->click(); QCOMPARE(changed.count(), 1);
        const QVector<QRectF> distributed = changed.takeFirst().at(0).value<QVector<QRectF>>();
        QCOMPARE(distributed[1].center().x(), 60.0);
    }
    void maskEditingAndRender() {
        panel->addMaskToLayer(0, composition::MaskShape::Ellipse, QRectF(10, 20, 120, 80));
        QCoreApplication::processEvents();
        QCOMPARE(comp->layers()[0].masks.size(), 1);
        QCOMPARE(history.count(), 1);
        auto* opacity = panel->findChild<QDoubleSpinBox*>("timelineMaskOpacity_0_0");
        auto* invert = panel->findChild<QCheckBox*>("timelineMaskInvert_0_0");
        QVERIFY(opacity); QVERIFY(invert);
        opacity->setValue(55); QCOMPARE(comp->layers()[0].masks[0].opacity, .55);
        invert->click(); QVERIFY(comp->layers()[0].masks[0].inverted);

        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(32, 24); scene->setDurationSeconds(1);
        auto& layer = scene->addLayer("Masked Plane"); layer.kind = composition::LayerKind::Plane;
        layer.planeColor = Qt::red; composition::Clip clip; clip.durationSeconds = 1; layer.clips << clip;
        composition::LayerMask mask; mask.bounds = QRectF(0, 0, 16, 24); layer.masks << mask;
        render::RenderManager manager; manager.setComposition(scene);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 0, QSize(32, 24)); QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 5000);
        const QByteArray bytes = frames[0][1].toByteArray();
        QImage rendered(reinterpret_cast<const uchar*>(bytes.constData()), 32, 24, 32 * 4,
                        QImage::Format_RGBA8888);
        QVERIFY(rendered.pixelColor(4, 12).red() > 200);
        QVERIFY(rendered.pixelColor(28, 12).red() < 40);
    }
    void threeDLayersAreSeenThroughTheCameraWithFog() {
        // The factors of Flux's ComputeFoggedFragment.
        model3d::Fog fog; fog.enabled = true; fog.nearDistance = 900; fog.farDistance = 2000;
        QCOMPARE(fog.factor(1450), 0.5);
        QCOMPARE(fog.factor(100), 1.0);
        QCOMPARE(fog.factor(5000), 0.0);
        fog.falloff = model3d::Fog::Falloff::Exponential; fog.density = 2;
        QVERIFY(qAbs(fog.factor(500) - std::exp(-1.0)) < 1e-12);
        fog.falloff = model3d::Fog::Falloff::ExponentialSquared; fog.density = 0.1;
        QVERIFY(qAbs(fog.factor(100) - std::exp(-0.01 * 100 * 100 * 0.001)) < 1e-12);
        fog.enabled = false;
        QCOMPARE(fog.factor(1e9), 1.0);

        // The default camera stands where the z = 0 plane fills the frame.
        const double cameraDistance = 45.0 / std::tan(39.6 / 2.0 * M_PI / 180.0);
        const auto render = [](bool threeD, double z, bool fogOn, double fogFar) {
            auto scene = std::make_shared<composition::Composition>();
            scene->setSize(160, 90); scene->setDurationSeconds(1);
            auto& plane = scene->addLayer("Plane"); plane.kind = composition::LayerKind::Plane;
            plane.planeColor = Qt::red;
            plane.transform.scalePercent = QPointF(25, 25);   // 40 x 22.5 px
            if (threeD) plane.dimension = composition::LayerDimension::ThreeD;
            plane.transform.positionZ = z;
            composition::Clip clip; clip.durationSeconds = 1; plane.clips << clip;
            auto& haze = scene->renderSettings();
            haze.fogEnabled = fogOn; haze.fogNearDistance = 0; haze.fogFarDistance = fogFar;
            haze.fogColor = Qt::white;
            render::RenderManager manager; manager.setComposition(scene);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(160, 90));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 5000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 160, 90, 160 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const QImage flat = render(false, 0, false, 1);
        const QImage deep = render(true, 0, false, 1);
        QVERIFY(!flat.isNull()); QVERIFY(!deep.isNull());
        // At z = 0 a 3D layer is where the 2D one was: x 60..100, y 34..56.
        for (const QPoint at : {QPoint(80, 45), QPoint(62, 36), QPoint(98, 54)}) {
            QVERIFY(flat.pixelColor(at).red() > 200);
            QVERIFY2(deep.pixelColor(at).red() > 200, qPrintable(QString("%1,%2").arg(at.x()).arg(at.y())));
        }
        for (const QPoint at : {QPoint(56, 45), QPoint(104, 45), QPoint(80, 30)}) {
            QVERIFY(flat.pixelColor(at).red() < 60);
            QVERIFY(deep.pixelColor(at).red() < 60);
        }
        // Turned about Z it turns the same way as the 2D layer it was.
        const auto turnedPlane = [](bool threeD) {
            auto scene = std::make_shared<composition::Composition>();
            scene->setSize(160, 90); scene->setDurationSeconds(1);
            auto& plane = scene->addLayer("Plane"); plane.kind = composition::LayerKind::Plane;
            plane.planeColor = Qt::red; plane.transform.scalePercent = QPointF(50, 10);
            plane.transform.rotationDegrees = 30;
            if (threeD) plane.dimension = composition::LayerDimension::ThreeD;
            composition::Clip clip; clip.durationSeconds = 1; plane.clips << clip;
            render::RenderManager manager; manager.setComposition(scene);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(160, 90));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 5000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 160, 90, 160 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const QImage turned2D = turnedPlane(false), turned3D = turnedPlane(true);
        int differing = 0;
        for (int y = 0; y < 90; ++y)
            for (int x = 0; x < 160; ++x)
                if (qAbs(turned2D.pixelColor(x, y).red() - turned3D.pixelColor(x, y).red()) > 100) ++differing;
        QVERIFY2(differing < 160 * 90 / 50, qPrintable(QString::number(differing)));
        QVERIFY(turned2D.pixelColor(110, 62).red() > 200);   // clockwise: the right end goes down
        // Moved back by the camera's own distance it is half as large.
        const QImage away = render(true, -cameraDistance, false, 1);
        QVERIFY(away.pixelColor(80, 45).red() > 200);
        QVERIFY(away.pixelColor(88, 45).red() > 200);
        QVERIFY(away.pixelColor(93, 45).red() < 60);
        // Fog reaching the far distance at twice the camera's: half white at
        // the centre, where the plane is one camera distance away.
        const QImage fogged = render(true, 0, true, 2 * cameraDistance);
        const QColor centre = fogged.pixelColor(80, 45);
        QVERIFY2(qAbs(centre.green() - 128) <= 4 && centre.red() > 250,
                 qPrintable(centre.name()));
        QVERIFY(fogged.pixelColor(56, 45).red() < 60);   // the background is not fogged
        // A 2D layer is not in the scene and stays clear.
        QCOMPARE(render(false, 0, true, 2 * cameraDistance).pixelColor(80, 45), flat.pixelColor(80, 45));
        QVERIFY(fogged.save(testArtifactPath(QStringLiteral("fog-3d-plane.png"))));

        // Turned about Y, one edge comes nearer than the other: the near one
        // is drawn larger and less fogged, pixel by pixel.
        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(160, 90); scene->setDurationSeconds(1);
        auto& plane = scene->addLayer("Plane"); plane.kind = composition::LayerKind::Plane;
        plane.planeColor = Qt::red; plane.dimension = composition::LayerDimension::ThreeD;
        plane.transform.scalePercent = QPointF(75, 75);
        plane.transform.rotationYDegrees = 50;
        composition::Clip clip; clip.durationSeconds = 1; plane.clips << clip;
        scene->renderSettings().fogEnabled = true; scene->renderSettings().fogColor = Qt::white;
        scene->renderSettings().fogNearDistance = cameraDistance - 40;
        scene->renderSettings().fogFarDistance = cameraDistance + 60;
        render::RenderManager manager; manager.setComposition(scene);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 0.0, QSize(160, 90));
        QVERIFY(QTest::qWaitFor([&] { return frames.count() > 0; }, 5000));
        const QByteArray bytes = frames[0][1].toByteArray();
        const QImage turned = QImage(reinterpret_cast<const uchar*>(bytes.constData()), 160, 90, 160 * 4,
                                     QImage::Format_RGBA8888).copy();
        QVERIFY(turned.save(testArtifactPath(QStringLiteral("fog-3d-turned.png"))));
        int leftmost = 160, rightmost = -1;
        for (int x = 0; x < 160; ++x) {
            if (turned.pixelColor(x, 45).red() > 200) { leftmost = qMin(leftmost, x); rightmost = qMax(rightmost, x); }
        }
        QVERIFY(rightmost > leftmost);
        const QColor oneEdge = turned.pixelColor(leftmost + 2, 45);
        const QColor otherEdge = turned.pixelColor(rightmost - 2, 45);
        QVERIFY2(qAbs(oneEdge.green() - otherEdge.green()) > 40,
                 qPrintable(oneEdge.name() + " " + otherEdge.name()));
    }
    void threeDLayersShareOneScene() {
        // Red tops the stack at z = 0; blue lies under it but nearer the camera.
        const auto render = [](bool twoDBetween) {
            auto scene = std::make_shared<composition::Composition>();
            scene->setSize(160, 90); scene->setDurationSeconds(1);
            const auto plane = [&](const QString& name, QColor color, double scale, double z, bool threeD) {
                auto& layer = scene->addLayer(name); layer.kind = composition::LayerKind::Plane;
                layer.planeColor = color; layer.transform.scalePercent = QPointF(scale, scale);
                layer.transform.positionZ = z;
                if (threeD) layer.dimension = composition::LayerDimension::ThreeD;
                composition::Clip clip; clip.durationSeconds = 1; layer.clips << clip;
            };
            plane("Far red", Qt::red, 50, 0, true);
            if (twoDBetween) {
                plane("Flat green", Qt::green, 10, 0, false);
                scene->layerRef(1).transform.position = QPointF(60, 30);
            }
            plane("Near blue", Qt::blue, 20, 50, true);
            render::RenderManager manager; manager.setComposition(scene);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(160, 90));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 5000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 160, 90, 160 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const QImage together = render(false);
        QVERIFY(!together.isNull());
        // In one scene the nearer layer wins at the centre; red still shows
        // where blue does not reach.
        QVERIFY2(together.pixelColor(80, 45).blue() > 200 && together.pixelColor(80, 45).red() < 60,
                 qPrintable(together.pixelColor(80, 45).name()));
        QVERIFY(together.pixelColor(50, 45).red() > 200);
        // A 2D layer between them makes two scenes, and the stack decides again.
        const QImage split = render(true);
        QVERIFY2(split.pixelColor(80, 45).red() > 200 && split.pixelColor(80, 45).blue() < 60,
                 qPrintable(split.pixelColor(80, 45).name()));
        QVERIFY(together.save(testArtifactPath(QStringLiteral("scene-depth.png"))));
    }
    void motionBlurSmearsAnimatedLayer() {
        // A 16x4 plane crossing a 64x16 shot at 48 px per frame; at frame 1 it
        // is centred, and a 180° shutter at -90° spans 12 px either side.
        const auto render = [](bool layerBlur, bool shotBlur) {
            auto scene = std::make_shared<composition::Composition>();
            scene->setSize(64, 16); scene->setDurationSeconds(1);
            scene->renderSettings().motionBlurEnabled = shotBlur;
            auto& plane = scene->addLayer("Plane"); plane.kind = composition::LayerKind::Plane;
            plane.planeColor = Qt::red; plane.motionBlur = layerBlur;
            plane.transform.scalePercent = QPointF(25, 25);
            plane.transform.positionXCurve.set(0, -48.0); plane.transform.positionXCurve.set(2, 48.0);
            composition::Clip clip; clip.durationSeconds = 1; plane.clips << clip;
            render::RenderManager manager; manager.setComposition(scene);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            const double fps = double(scene->fpsNumerator()) / scene->fpsDenominator();
            manager.requestFrame(0, 1.0 / fps, QSize(64, 16));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 5000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 64, 16, 64 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const QImage sharp = render(false, true);
        const QImage blurred = render(true, true);
        const QImage shotOff = render(true, false);
        QVERIFY(!sharp.isNull()); QVERIFY(!blurred.isNull()); QVERIFY(!shotOff.isNull());
        // The plane spans x 24..40; 46 lies outside it but inside the shutter.
        QVERIFY(sharp.pixelColor(32, 8).red() > 200);
        QVERIFY(sharp.pixelColor(46, 8).red() < 40);
        // The 24 px sweep is wider than the plane: its centre is covered for
        // two thirds of the shutter, x 46 for a quarter.
        const int centre = blurred.pixelColor(32, 8).red();
        QVERIFY2(centre > 150 && centre < 220, qPrintable(QString::number(centre)));
        const int smear = blurred.pixelColor(46, 8).red();
        QVERIFY2(smear > 40 && smear < 200, qPrintable(QString::number(smear)));
        QVERIFY(blurred.pixelColor(56, 8).red() < 40);   // beyond the shutter
        // MotionBlurEnabled off in the shot's RenderSettings keeps it sharp.
        QCOMPARE(shotOff.pixelColor(46, 8), sharp.pixelColor(46, 8));
        // The shot's RenderSettings survive a save.
        auto scene = std::make_shared<composition::Composition>();
        scene->renderSettings().shutterAngle = 90; scene->renderSettings().shutterPhase = -45;
        scene->renderSettings().maxNumOfSamples = 7; scene->renderSettings().useAdaptiveSamples = false;
        scene->renderSettings().fogColor = QColor(255, 0, 0);
        scene->addLayer("Plane").clips.append(composition::Clip{});
        scene->layerRef(0).kind = composition::LayerKind::Plane;
        scene->layerRef(0).clips[0].durationSeconds = 1;
        QTemporaryDir temp; media::MediaManager media;
        const QString path = temp.filePath("shutter.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *scene, media).isSuccess());
        composition::Composition loaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &media).isSuccess());
        QCOMPARE(loaded.renderSettings().shutterAngle, 90.0);
        QCOMPARE(loaded.renderSettings().shutterPhase, -45.0);
        QCOMPARE(loaded.renderSettings().maxNumOfSamples, 7);
        QVERIFY(!loaded.renderSettings().useAdaptiveSamples);
        QCOMPARE(loaded.renderSettings().fogColor, QColor(255, 0, 0));
    }
    void toolbarAndActions() {
        for (const QString name : {"toolButtonPointer", "toolButtonHand", "toolButtonSlice", "toolButtonStretch"}) {
            auto* tool = panel->findChild<QToolButton*>(name); QVERIFY(tool); QVERIFY(tool->isVisible());
            tool->click(); QVERIFY(tool->isChecked()); tool->click(); QVERIFY(tool->isChecked());
            QCOMPARE(panel->tool(), panel->findChild<ui::TimelineCanvas*>()->tool());
        }
        auto* snap = panel->findChild<QToolButton*>("timelineSnap"); snap->click(); QVERIFY(!panel->snapEnabled()); snap->click(); QVERIFY(panel->snapEnabled());
        auto* graph = panel->findChild<QToolButton*>("timelineValueGraph"); graph->click();
        QCOMPARE(panel->findChild<QStackedWidget*>("stackedWidgetTimelines")->currentIndex(), 1);
        auto* autoZoom = panel->findChild<QToolButton*>("timelineGraphAutoZoom"); QVERIFY(autoZoom->isEnabled()); autoZoom->click();
        QVERIFY(!panel->findChild<ui::TimelineValueGraphView*>()->autoZoom()); graph->click(); QVERIFY(!autoZoom->isEnabled());
        QSignalSpy newLayer(panel.get(), &ui::TimelineWidget::newLayerRequested);
        auto* add = panel->findChild<QToolButton*>("timelineNewLayer");
        QCOMPARE(add->menu()->actions().size(), 6); add->menu()->actions()[1]->trigger(); QCOMPARE(newLayer.count(), 1);
        QSignalSpy cache(panel.get(), &ui::TimelineWidget::preRenderRequested);
        panel->findChild<QToolButton*>("timelineRenderCache")->click(); QCOMPARE(cache.count(), 1);
        QSignalSpy exportSignal(panel.get(), &ui::TimelineWidget::exportRequested);
        panel->findChild<QToolButton*>("timelineExport")->click(); QCOMPARE(exportSignal.count(), 1);
        QSignalSpy composite(panel.get(), &ui::TimelineWidget::makeCompositeShotRequested);
        panel->findChild<QToolButton*>("toolButtonMakeCompositeShot")->click();
        QCOMPARE(composite.count(), 1);
    }
    void compositionTabs() {
        panel->setCompositionTabs({"Root", "Nested"}, 0);
        auto* tabs = panel->findChild<QTabBar*>("timelineCompositionTabs");
        QVERIFY(tabs); QCOMPARE(tabs->count(), 2); QCOMPARE(tabs->tabText(1), QString("Nested"));
        auto* footer = panel->findChild<QWidget*>("timelineFooter");
        auto* start = panel->findChild<QToolButton*>("timelineStartTab");
        auto* pages = panel->findChild<QStackedWidget*>("timelinePanelPages");
        QVERIFY(footer); QVERIFY(start); QVERIFY(pages);
        QCOMPARE(footer->height(), 25);
        QCOMPARE(tabs->parentWidget(), footer);

        auto* startContent = new QWidget;
        startContent->setObjectName("embeddedStartContent");
        panel->setStartPage(startContent);
        start->click();
        QVERIFY(panel->isStartPageVisible());
        QVERIFY(start->isChecked());
        QCOMPARE(tabs->currentIndex(), 0);

        QSignalSpy activated(panel.get(), &ui::TimelineWidget::compositionTabActivated);
        tabs->setCurrentIndex(1); QCOMPARE(activated.count(), 1);
        QVERIFY(!panel->isStartPageVisible());
        QVERIFY(!start->isChecked());
        QSignalSpy closed(panel.get(), &ui::TimelineWidget::compositionTabCloseRequested);
        QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 1));
        QCOMPARE(closed.count(), 1);
        QVERIFY(!panel->findChild<QToolButton*>("timelineTabsMenu"));
        QVERIFY(panel->titleBarWidget());
        QCOMPARE(panel->titleBarWidget()->height(), 0);
    }
    void nestedCompositionRoundTripAndRender() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        auto root = std::make_shared<composition::Composition>();
        root->setName("Root"); root->setSize(24, 24); root->setDurationSeconds(1);
        auto child = std::make_shared<composition::Composition>();
        child->setName("Nested"); child->setSize(24, 24); child->setDurationSeconds(1);
        auto& plane = child->addLayer("Red"); plane.kind = composition::LayerKind::Plane;
        plane.planeColor = Qt::red; composition::Clip fill; fill.durationSeconds = 1;
        plane.clips.append(fill);
        auto& nested = root->addLayer("Nested"); composition::Clip reference;
        reference.durationSeconds = 1; reference.nestedComposition = child;
        reference.nestedCompositionId = child->id();
        reference.mediaId = core::Identifier("composition:" + child->id().value());
        nested.clips.append(reference);

        media::MediaManager media;
        const QString file = dir.filePath("nested.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(file, *root, media).isSuccess());
        auto restored = std::make_shared<composition::Composition>();
        QVERIFY(project::VegfxSerializer::loadFromFile(file, restored.get(), &media).isSuccess());
        QCOMPARE(restored->layers().size(), 1);
        const auto restoredChild = restored->layers()[0].clips[0].nestedComposition;
        QVERIFY(restoredChild); QCOMPARE(restoredChild->name(), QString("Nested"));
        QCOMPARE(restoredChild->layers().size(), 1);
        QCOMPARE(restoredChild->layers()[0].planeColor, QColor(Qt::red));

        render::RenderManager manager; manager.setComposition(restored);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 0, QSize(24, 24));
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 5000);
        const QByteArray bytes = frames[0][1].toByteArray();
        QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 24, 24, 24 * 4,
                     QImage::Format_RGBA8888);
        QVERIFY(image.pixelColor(12, 12).red() > 200);
        QVERIFY(image.pixelColor(12, 12).green() < 40);
    }
    void transitionsResolveRenderAndRoundTrip() {
        // Two nested solid shots on one layer, cut at 1 s. Their material is
        // longer than the clips, so a centred transition has handles.
        const auto solid = [](const QString& name, const QColor& color) {
            auto shot = std::make_shared<composition::Composition>();
            shot->setName(name); shot->setSize(16, 16); shot->setDurationSeconds(3);
            auto& plane = shot->addLayer(name); plane.kind = composition::LayerKind::Plane;
            plane.planeColor = color; composition::Clip fill; fill.durationSeconds = 3;
            plane.clips.append(fill);
            return shot;
        };
        auto root = std::make_shared<composition::Composition>();
        root->setName("Root"); root->setSize(16, 16); root->setDurationSeconds(3);
        auto& layer = root->addLayer("Cut");
        for (const auto& [start, shot] : {std::make_pair(0.0, solid("Red", Qt::red)),
                                          std::make_pair(1.0, solid("Blue", Qt::blue))}) {
            composition::Clip clip; clip.startSeconds = start; clip.durationSeconds = 1;
            clip.nestedComposition = shot; clip.nestedCompositionId = shot->id();
            clip.mediaId = core::Identifier("composition:" + shot->id().value());
            layer.clips.append(clip);
        }
        composition::Effect dissolve;
        dissolve.pluginId = core::Identifier("test.transition.unavailable");
        dissolve.name = "Dissolve";
        dissolve.transitionEdge = composition::TransitionEdge::In;
        dissolve.transitionSeconds = 0.5;
        layer.clips[1].effects.append(dissolve);

        auto windows = composition::transitionWindows(layer, 1.0 / 60.0, nullptr);
        QCOMPARE(windows.size(), 1);
        QCOMPARE(windows[0].fromClip, 0); QCOMPARE(windows[0].toClip, 1);
        QCOMPARE(windows[0].start, 0.75); QCOMPARE(windows[0].end, 1.25);
        QCOMPARE(windows[0].progressAt(1.0), 0.5);

        // Out fades the end of a clip to nothing; overlapping clips use the
        // overlap instead of a centred window; a lone In fades up.
        composition::Layer edges;
        composition::Clip single; single.durationSeconds = 2;
        composition::Effect fade = dissolve;
        fade.transitionEdge = composition::TransitionEdge::Out;
        single.effects.append(fade);
        edges.clips.append(single);
        composition::Clip overlap; overlap.startSeconds = 1.5; overlap.durationSeconds = 2;
        overlap.effects.append(dissolve);
        edges.clips.append(overlap);
        composition::Clip lone; lone.startSeconds = 10; lone.durationSeconds = 0.25;
        lone.effects.append(dissolve);
        edges.clips.append(lone);
        windows = composition::transitionWindows(edges, 1.0 / 60.0, nullptr);
        QCOMPARE(windows.size(), 3);
        QCOMPARE(windows[0].fromClip, 0); QCOMPARE(windows[0].toClip, -1);
        QCOMPARE(windows[0].start, 1.5); QCOMPARE(windows[0].end, 2.0);
        QCOMPARE(windows[1].fromClip, 0); QCOMPARE(windows[1].toClip, 1);
        QCOMPARE(windows[1].start, 1.5); QCOMPARE(windows[1].end, 2.0);
        QCOMPARE(windows[2].fromClip, -1); QCOMPARE(windows[2].toClip, 2);
        QCOMPARE(windows[2].start, 10.0); QCOMPARE(windows[2].end, 10.25);

        QTemporaryDir dir; QVERIFY(dir.isValid());
        media::MediaManager media;
        const QString file = dir.filePath("transition.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(file, *root, media).isSuccess());
        auto restored = std::make_shared<composition::Composition>();
        QVERIFY(project::VegfxSerializer::loadFromFile(file, restored.get(), &media).isSuccess());
        const auto& restoredEffect = restored->layers()[0].clips[1].effects.value(0);
        QCOMPARE(restoredEffect.transitionEdge, composition::TransitionEdge::In);
        QCOMPARE(restoredEffect.transitionSeconds, 0.5);
        QCOMPARE(restoredEffect.pluginId.value(), QString("test.transition.unavailable"));

        // A module this build cannot run still softens the cut with a dissolve.
        render::RenderManager manager; manager.setComposition(restored);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        const auto pixelAt = [&](double seconds) {
            frames.clear();
            manager.requestFrame(0, seconds, QSize(16, 16));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 5000)) return QColor();
            const QByteArray bytes = frames[0][1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 16, 16, 16 * 4,
                               QImage::Format_RGBA8888);
            return image.pixelColor(8, 8);
        };
        QColor pixel = pixelAt(0.5);
        QVERIFY2(pixel.red() > 240 && pixel.blue() < 16, qPrintable(pixel.name()));
        pixel = pixelAt(1.0);
        QVERIFY2(qAbs(pixel.red() - 128) <= 8 && qAbs(pixel.blue() - 128) <= 8, qPrintable(pixel.name()));
        pixel = pixelAt(1.2);
        QVERIFY2(pixel.blue() > pixel.red() * 3, qPrintable(pixel.name()));
        pixel = pixelAt(1.5);
        QVERIFY2(pixel.blue() > 240 && pixel.red() < 16, qPrintable(pixel.name()));
    }
    void audioTransitionsExportAsProcessedPcm() {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
        if (ffmpeg.isEmpty() || ffprobe.isEmpty()) QSKIP("FFmpeg is needed for the export audio path");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto wave = [&](const QString& name, qint16 value) {
            const QString path = dir.filePath(name);
            QFile file(path); if (!file.open(QIODevice::WriteOnly)) return QString();
            QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
            constexpr int frames = 48000 * 3;
            stream.writeRawData("RIFF", 4); stream << quint32(36 + frames * 4);
            stream.writeRawData("WAVEfmt ", 8);
            stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
                   << quint32(192000) << quint16(4) << quint16(16);
            stream.writeRawData("data", 4); stream << quint32(frames * 4);
            for (int i = 0; i < frames; ++i) stream << value << value;
            return path;
        };
        media::MediaManager media;
        const QString a = wave("a.wav", 1000), b = wave("b.wav", 3000);
        QVERIFY(media.importFile(a).isSuccess()); QVERIFY(media.importFile(b).isSuccess());
        composition::Composition scene;
        scene.setSize(16, 16); scene.setDurationSeconds(2);
        auto& layer = scene.addLayer("Audio");
        composition::Clip first; first.mediaId = media.assetByFilePath(a).id();
        first.sourceStartSeconds = 0.5; first.durationSeconds = 1;
        composition::Clip second; second.mediaId = media.assetByFilePath(b).id();
        second.startSeconds = 1; second.sourceStartSeconds = 0.5; second.durationSeconds = 1;
        // Registered as an AudioTransitions module that this build cannot
        // run: export must still combine the clips, with the fallback fade.
        const core::Identifier fadeId("test.export.audio.transition");
        plugin::registerNativeAudioTransitionModule(fadeId, dir.filePath("missing.hfpl"), dir.path(), false);
        composition::Effect fade; fade.pluginId = fadeId; fade.name = "Fade";
        fade.transitionEdge = composition::TransitionEdge::In; fade.transitionSeconds = 0.5;
        second.effects.append(fade);
        layer.clips << first << second;

        render::ExportAudioRequest request;
        request.composition = &scene; request.media = &media;
        request.ffmpeg = ffmpeg; request.ffprobe = ffprobe;
        request.temporaryDirectory = dir.path(); request.frameRate = 30;
        request.exportStart = 0; request.exportEnd = 2;
        QVector<render::ExportAudioInput> inputs; QString error;
        QVERIFY2(render::buildExportAudioInputs(request, inputs, &error), qPrintable(error));
        plugin::clearNativeEffectModules();
        QCOMPARE(inputs.size(), 2);
        const auto sampleAt = [](const render::ExportAudioInput& input, double seconds) {
            QFile file(input.path); if (!file.open(QIODevice::ReadOnly)) return INT_MIN;
            const qint64 frame = qRound64((seconds - input.delay) * 48000);
            if (!file.seek(frame * 4)) return INT_MIN;
            const QByteArray bytes = file.read(2);
            return bytes.size() == 2 ? int(qFromLittleEndian<qint16>(bytes.constData())) : INT_MIN;
        };
        // The outgoing clip plays into its handle up to 1.25 s and carries
        // the fade; the incoming one starts 0.25 s early, silenced there.
        const auto& from = inputs[0].delay < inputs[1].delay ? inputs[0] : inputs[1];
        const auto& to = inputs[0].delay < inputs[1].delay ? inputs[1] : inputs[0];
        QVERIFY(from.rawPcm && to.rawPcm);
        QCOMPARE(from.delay, 0.0); QVERIFY(qAbs(from.sourceDuration - 1.25) < 1e-6);
        QVERIFY(qAbs(to.delay - 0.75) < 1e-6); QVERIFY(qAbs(to.sourceDuration - 1.25) < 1e-6);
        QVERIFY2(qAbs(sampleAt(from, 0.5) - 1000) <= 2, qPrintable(QString::number(sampleAt(from, 0.5))));
        QVERIFY2(qAbs(sampleAt(from, 1.0) - 2000) <= 8, qPrintable(QString::number(sampleAt(from, 1.0))));
        QCOMPARE(sampleAt(to, 1.0), 0);
        QVERIFY2(qAbs(sampleAt(to, 1.5) - 3000) <= 2, qPrintable(QString::number(sampleAt(to, 1.5))));
    }
    void waveformPeaksStylesAndCache() {
        // 1 s of silence then 1 s of a half-scale sine, at the decode rate.
        media::WaveformBuilder builder(8000);
        QVector<qint16> pcm(16000, 0);
        for (int i = 8000; i < 16000; ++i) pcm[i] = qint16(std::lround(16384 * std::sin(i * 0.3)));
        builder.feed(pcm.constData(), 5000);          // chunking must not matter
        builder.feed(pcm.constData() + 5000, pcm.size() - 5000);
        const media::WaveformPeaks peaks = builder.finish();
        QCOMPARE(peaks.peak.size(), 200); QCOMPARE(peaks.rms.size(), 200);
        QCOMPARE(peaks.durationSeconds(), 2.0);
        auto columns = media::waveformColumns(peaks, 0, 2, 4, media::WaveformStyle::Peak, false);
        QCOMPARE(columns[0], 0.0f); QCOMPARE(columns[1], 0.0f);
        QVERIFY(qAbs(columns[3] - 0.5f) < 0.01f);
        const auto rms = media::waveformColumns(peaks, 0, 2, 4, media::WaveformStyle::Rms, false);
        QVERIFY2(qAbs(rms[3] - 0.3536f) < 0.02f, qPrintable(QString::number(rms[3])));
        const auto log = media::waveformColumns(peaks, 0, 2, 4, media::WaveformStyle::Peak, true);
        QVERIFY2(qAbs(log[3] - 0.8997f) < 0.01f, qPrintable(QString::number(log[3])));
        // A clip showing only the loud second (slip/stretch) is loud throughout.
        columns = media::waveformColumns(peaks, 1.0, 2.0, 2, media::WaveformStyle::Peak, false);
        QVERIFY(columns[0] > 0.45f && columns[1] > 0.45f);
        columns = media::waveformColumns(peaks, 5.0, 6.0, 2, media::WaveformStyle::Peak, false);
        QCOMPARE(columns[0], 0.0f);

        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty()) QSKIP("FFmpeg is needed to build waveforms from media");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString wav = dir.filePath("tone.wav");
        {
            QFile file(wav); QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
            constexpr int frames = 48000 * 2;
            stream.writeRawData("RIFF", 4); stream << quint32(36 + frames * 4);
            stream.writeRawData("WAVEfmt ", 8);
            stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
                   << quint32(192000) << quint16(4) << quint16(16);
            stream.writeRawData("data", 4); stream << quint32(frames * 4);
            for (int i = 0; i < frames; ++i) {
                const qint16 value = i < 48000 ? 0 : qint16(std::lround(16384 * std::sin(i * 0.05)));
                stream << value << value;
            }
        }
        const QString cacheDir = dir.filePath("cache");
        {
            media::WaveformCache cache; cache.setDecoder(ffmpeg); cache.setCacheDirectory(cacheDir);
            QSignalSpy ready(&cache, &media::WaveformCache::peaksReady);
            QVERIFY(!cache.peaks(wav));
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
            const media::WaveformPeaks* built = cache.peaks(wav);
            QVERIFY(built); QVERIFY(qAbs(built->durationSeconds() - 2.0) < 0.02);
            QVERIFY(built->peak.value(50) < 0.01f); QVERIFY(built->peak.value(150) > 0.45f);
            QVERIFY(!cache.peaks(dir.filePath("missing.wav")));
        }
        // A new cache reads the peak file without decoding again.
        media::WaveformCache reloaded; reloaded.setDecoder(QString()); reloaded.setCacheDirectory(cacheDir);
        QVERIFY(reloaded.peaks(wav));

        // The timeline draws it inside the clip, following slip and stretch.
        auto mediaManager = std::make_shared<media::MediaManager>();
        QVERIFY(mediaManager->importFile(wav).isSuccess());
        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(64, 64); scene->setDurationSeconds(4);
        auto& layer = scene->addLayer("Tone");
        composition::Clip clip; clip.mediaId = mediaManager->assetByFilePath(wav).id();
        clip.durationSeconds = 2; layer.clips.append(clip);
        ui::TimelineWidget timeline; timeline.resize(900, 300);
        timeline.setComposition(scene); timeline.setMediaManager(mediaManager);
        timeline.show(); QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        auto* canvas = timeline.findChild<ui::TimelineCanvas*>(); QVERIFY(canvas);
        const auto waveformPixels = [&] {
            const QImage image = canvas->grab().toImage();
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    // Waveform ink over a clip; a selected clip is 143,163,175.
                    if (c.blue() > 178 && c.blue() > c.red() + 35) ++count;
                }
            return count;
        };
        QTRY_VERIFY_WITH_TIMEOUT(waveformPixels() > 20, 10000);
    }
    void colorsPresetsAndEffects() {
        auto* swatch = panel->findChild<QToolButton*>("timelineParam_0_0_0_4.swatch"); QVERIFY(swatch);
        QTimer::singleShot(0, panel.get(), [] {
            if (auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) { dialog->setCurrentColor(Qt::green); dialog->accept(); }
        });
        swatch->click(); QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[4], QString("#00ff00"));
        auto* presets = panel->findChild<QComboBox*>("timelinePreset_0_0_0"); QVERIFY(presets);
        const int reset = presets->findData("reset"); QVERIFY(reset >= 0);
        QMetaObject::invokeMethod(presets, "activated", Q_ARG(int, reset)); QCoreApplication::processEvents();
        QCOMPARE(comp->layers()[0].clips[0].effects[0].parameterValues[4], QString("#ffffff"));
        QTimer::singleShot(0, panel.get(), [] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) { menu->actions().first()->trigger(); menu->close(); }
        });
        panel->findChild<QToolButton*>("timelineAddEffect_0")->click(); QCoreApplication::processEvents();
        QCOMPARE(comp->layers()[0].clips[0].effects.size(), 2);
        history.undo(); QCOMPARE(comp->layers()[0].clips[0].effects.size(), 1);
    }
    void canvasTransitionLengthDragAndUndo() {
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); const int y = ui::kTimelineRulerHeight + 11;
        panel->setSnapEnabled(false);
        const auto originalClips = comp->layers()[0].clips;
        auto& layer = comp->layerRef(0);
        composition::Clip next = layer.clips[0];
        next.effects.clear();
        next.startSeconds = layer.clips[0].endSeconds();
        composition::Effect cross; cross.pluginId = core::Identifier("test.transition.drag");
        cross.name = "Cross"; cross.transitionEdge = composition::TransitionEdge::In;
        cross.transitionSeconds = 0.5;
        next.effects.append(cross);
        layer.clips.append(next);
        panel->setComposition(comp);
        const int historyBefore = history.count();
        const double cut = next.startSeconds;
        const auto xFor = [&](double seconds) {
            return int(std::lround(seconds * canvas->pixelsPerSecond())) - canvas->hScrollOffset();
        };
        // Dragging the right edge of a centred transition resizes it around
        // the cut; releasing records one undoable edit.
        const QPoint start(xFor(cut + 0.25), y);
        const double target = cut + 0.25 + 60.0 / canvas->pixelsPerSecond();
        const QPoint finish(xFor(target), y);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, finish, canvas->mapToGlobal(finish), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, finish);
        const double length = comp->layers()[0].clips.last().effects[0].transitionSeconds;
        QVERIFY2(qAbs(length - 2.0 * (target - cut)) <= 2.0 / 30.0 + 1e-9, qPrintable(QString::number(length)));
        QCOMPARE(comp->layers()[0].clips[0].startSeconds, originalClips[0].startSeconds);
        QCOMPARE(history.count(), historyBefore + 1);
        history.undo();
        QCOMPARE(comp->layers()[0].clips.last().effects[0].transitionSeconds, 0.5);
        comp->layerRef(0).clips = originalClips;
        panel->setComposition(comp);
    }
    void proxyMediaGenerationAndRoundTrip() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString root = dir.filePath("proxies");
        const QString source = dir.filePath("clip.mp4");
        const QString performance = media::proxyFilePath(root, "Show", source, media::ProxyMode::Performance);
        const QString quality = media::proxyFilePath(root, "Show", source, media::ProxyMode::Quality);
        QVERIFY(performance.startsWith(QDir(root).filePath("Show")));
        QVERIFY(performance != quality);
        QVERIFY(media::proxyFilePath(root, QString(), source, media::ProxyMode::Quality).contains("/Untitled/"));
        QVERIFY(media::proxyFilePath(root, "Show", source, media::ProxyMode::None).isEmpty());
        QVERIFY(media::proxyEncodeArguments(source, performance, media::ProxyMode::Performance, "Medium", "libx264")
                    .join(' ').contains("trunc(iw/4)*2"));
        QVERIFY(media::proxyEncodeArguments(source, quality, media::ProxyMode::Quality, "High", "libx264")
                    .join(' ').contains("-g 1 "));

        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
        if (ffmpeg.isEmpty() || ffprobe.isEmpty()) QSKIP("FFmpeg is needed to build proxies");
        QProcess make;
        make.start(ffmpeg, {"-y", "-v", "error", "-f", "lavfi", "-i", "testsrc=s=320x240:r=30:d=1",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", source});
        QVERIFY(make.waitForFinished(30000)); QCOMPARE(make.exitCode(), 0);
        const auto frameSize = [&](const QString& file) {
            QProcess probe;
            probe.start(ffprobe, {"-v", "error", "-select_streams", "v:0", "-show_entries",
                                  "stream=width,height", "-of", "csv=p=0:s=x", file});
            probe.waitForFinished(10000);
            return QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
        };
        media::ProxyGenerator generator; generator.setFfmpeg(ffmpeg);
        QSignalSpy finished(&generator, &media::ProxyGenerator::proxyFinished);
        generator.enqueue(source, performance, media::ProxyMode::Performance, "Low", "libx264");
        generator.enqueue(source, quality, media::ProxyMode::Quality, "Medium", "libx264");
        generator.enqueue(source, quality, media::ProxyMode::Quality, "Medium", "libx264"); // dedup
        QVERIFY(generator.isPending(quality));
        QCOMPARE(generator.pendingCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 60000);
        QVERIFY(finished[0][2].toBool()); QVERIFY(finished[1][2].toBool());
        QCOMPARE(frameSize(performance), QString("160x120"));
        QCOMPARE(frameSize(quality), QString("320x240"));
        QVERIFY(!QFileInfo::exists(performance + ".part"));
        QCOMPARE(generator.pendingCount(), 0);

        // The per-asset mode travels with the project.
        media::MediaManager mediaManager;
        QVERIFY(mediaManager.importFile(source).isSuccess());
        mediaManager.assetByFilePathForEdit(source)->setProxyMode(media::ProxyMode::Quality);
        composition::Composition scene; scene.setSize(320, 240); scene.setDurationSeconds(1);
        auto& layer = scene.addLayer("Clip");
        composition::Clip clip; clip.mediaId = mediaManager.assetByFilePath(source).id();
        clip.durationSeconds = 1; layer.clips.append(clip);
        const QString file = dir.filePath("proxy.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(file, scene, mediaManager).isSuccess());
        media::MediaManager loaded; composition::Composition restored;
        QVERIFY(project::VegfxSerializer::loadFromFile(file, &restored, &loaded).isSuccess());
        QCOMPARE(loaded.assetByFilePath(source).proxyMode(), media::ProxyMode::Quality);
    }
    void exportChoosesWorkingH264Encoder() {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        QCOMPARE(render::chooseH264Encoder(ffmpeg, false), QString("libx264"));
        if (ffmpeg.isEmpty()) QSKIP("FFmpeg is needed to probe encoders");
        const QString encoder = render::chooseH264Encoder(ffmpeg, true);
        QVERIFY(QStringList({"h264_nvenc", "h264_qsv", "h264_amf", "h264_mf", "libx264"})
                    .contains(encoder));
        QCOMPARE(render::chooseH264Encoder(ffmpeg, true), encoder); // probed once
        // Whatever was chosen produces a playable file with these options.
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString output = dir.filePath("probe.mp4");
        QProcess encode;
        encode.start(ffmpeg, QStringList{"-y", "-v", "error", "-f", "lavfi", "-i",
                                         "testsrc=s=320x240:r=30:d=0.5"}
                                 + render::h264EncoderArguments(encoder) + QStringList{output});
        QVERIFY(encode.waitForFinished(30000));
        QCOMPARE(encode.exitCode(), 0);
        QVERIFY(QFileInfo(output).size() > 1000);
        qInfo().noquote() << "H.264 export encoder:" << encoder;
    }
    void imageSequenceImportRenderAndRoundTrip() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QList<QColor> colors = {Qt::red, Qt::green, Qt::blue, Qt::white};
        for (int i = 0; i < colors.size(); ++i) {
            QImage still(8, 8, QImage::Format_RGBA8888); still.fill(colors[i]);
            QVERIFY(still.save(dir.filePath(QStringLiteral("shot_%1.png").arg(9 + i, 4, 10, QChar('0')))));
        }
        QImage other(8, 8, QImage::Format_RGBA8888); other.fill(Qt::black);
        QVERIFY(other.save(dir.filePath("shot_0020.png")));   // not contiguous
        QVERIFY(other.save(dir.filePath("take_0010.png")));   // another prefix
        QVERIFY(other.save(dir.filePath("single.png")));
        const QStringList run = media::findImageSequence(dir.filePath("shot_0010.png"));
        QCOMPARE(run.size(), 4);
        QCOMPARE(QFileInfo(run.first()).fileName(), QString("shot_0009.png"));
        QCOMPARE(QFileInfo(run.last()).fileName(), QString("shot_0012.png"));
        QVERIFY(media::findImageSequence(dir.filePath("shot_0020.png")).isEmpty());
        QVERIFY(media::findImageSequence(dir.filePath("single.png")).isEmpty());

        auto mediaManager = std::make_shared<media::MediaManager>();
        QString path;
        QVERIFY(mediaManager->importImageSequence(run, 2.0, &path).isSuccess());
        QVERIFY(path.endsWith(media::kImageSequenceMarker));
        const media::MediaAsset asset = mediaManager->assetByFilePath(path);
        QVERIFY(asset.isImageSequence());
        QCOMPARE(asset.kind(), media::MediaKind::Video);
        QCOMPARE(asset.durationSeconds(), 2.0);
        QCOMPARE(asset.frameSize(), QSize(8, 8));
        QCOMPARE(QFileInfo(asset.sequenceFileAt(1.6)).fileName(), QString("shot_0012.png"));
        // The single still stays its own asset next to the sequence.
        QVERIFY(mediaManager->importFile(run.first()).isSuccess());
        QCOMPARE(mediaManager->assetByFilePath(run.first()).kind(), media::MediaKind::Image);

        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(8, 8); scene->setDurationSeconds(2);
        auto& layer = scene->addLayer("Sequence");
        composition::Clip clip; clip.mediaId = asset.id(); clip.durationSeconds = 2;
        layer.clips.append(clip);
        render::RenderManager manager; manager.setComposition(scene); manager.setMediaManager(mediaManager);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 0.6, QSize(8, 8));     // second still at 2 fps
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 5000);
        const QByteArray bytes = frames[0][1].toByteArray();
        const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 8, 8, 8 * 4,
                           QImage::Format_RGBA8888);
        QVERIFY2(image.pixelColor(4, 4).green() > 200 && image.pixelColor(4, 4).red() < 40,
                 qPrintable(image.pixelColor(4, 4).name()));
        manager.cancelAll();

        const QString file = dir.filePath("sequence.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(file, *scene, *mediaManager).isSuccess());
        media::MediaManager loadedMedia;
        auto restored = std::make_shared<composition::Composition>();
        QVERIFY(project::VegfxSerializer::loadFromFile(file, restored.get(), &loadedMedia).isSuccess());
        const media::MediaAsset loaded = loadedMedia.assetById(restored->layers()[0].clips[0].mediaId);
        QVERIFY(loaded.isImageSequence());
        QCOMPARE(loaded.sequenceFiles(), run);
        QCOMPARE(loaded.sequenceFrameRate(), 2.0);
    }
    void timelineDiskCacheSurvivesManagersAndExpires() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(16, 16); scene->setDurationSeconds(0.5);
        auto& plane = scene->addLayer("Plane"); plane.kind = composition::LayerKind::Plane;
        plane.planeColor = Qt::red;
        composition::Clip fill; fill.durationSeconds = 0.5; plane.clips.append(fill);
        const QSize size(16, 16);
        const auto cachedFiles = [&] {
            int count = 0;
            QDirIterator it(dir.path(), {"*.ovframe"}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) { it.next(); ++count; }
            return count;
        };
        {
            render::RenderManager writer; writer.setComposition(scene);
            writer.setDiskCacheDirectory(dir.path());
            QSignalSpy finished(&writer, &render::RenderManager::playbackCacheFinished);
            writer.startPlaybackCache(0, size);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
            writer.cancelAll();
        }
        const int frames = qRound(0.5 * double(scene->fpsNumerator()) / qMax(1, scene->fpsDenominator()));
        QCOMPARE(cachedFiles(), frames);

        // Another session reads the frame back instead of rendering it.
        render::RenderManager reader; reader.setComposition(scene);
        reader.setDiskCacheDirectory(dir.path());
        QSignalSpy ready(&reader, &render::RenderManager::frameReady);
        reader.requestFrame(1, 0.0, size);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
        QCOMPARE(reader.diskCacheHits(), 1);
        const QByteArray bytes = ready[0][1].toByteArray();
        const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 16, 16, 16 * 4,
                           QImage::Format_RGBA8888);
        QVERIFY(image.pixelColor(8, 8).red() > 200);
        // A changed scene is a different key: rendered, not read back stale.
        scene->layerRef(0).planeColor = Qt::blue;
        reader.requestFrame(2, 0.0, size);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 5000);
        QCOMPARE(reader.diskCacheHits(), 1);
        reader.cancelAll();

        // Retention counts from the last use.
        QDirIterator it(dir.path(), {"*.ovframe"}, QDir::Files, QDirIterator::Subdirectories);
        QVERIFY(it.hasNext());
        QFile old(it.next()); QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTime().addDays(-40), QFileDevice::FileModificationTime));
        old.close();
        render::FrameDiskCache cache; cache.setDirectory(dir.path());
        QCOMPARE(cache.prune(0), 0);
        QCOMPARE(cache.prune(30), 1);
        if (cachedFiles() != frames - 1) {
            QDirIterator all(dir.path(), QDir::Files, QDirIterator::Subdirectories);
            while (all.hasNext()) qWarning() << "cache file" << all.next();
        }
        QCOMPARE(cachedFiles(), frames - 1);
    }
    void preRenderedShotIsReadInsteadOfRendered() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QSize size(16, 16);
        auto shot = std::make_shared<composition::Composition>();
        shot->setName("Shot"); shot->setSize(16, 16); shot->setDurationSeconds(0.5);
        auto& plane = shot->addLayer("Plane"); plane.kind = composition::LayerKind::Plane;
        plane.planeColor = Qt::red;
        composition::Clip fill; fill.durationSeconds = 0.5; plane.clips.append(fill);
        auto root = std::make_shared<composition::Composition>();
        root->setSize(16, 16); root->setDurationSeconds(0.5);
        composition::Clip reference; reference.durationSeconds = 0.5;
        reference.nestedComposition = shot; reference.nestedCompositionId = shot->id();
        reference.mediaId = core::Identifier("composition:" + shot->id().value());
        root->addLayer("Shot").clips.append(reference);

        // "Make Pre-Render(s)": the shot alone, over transparency, at its own
        // size, into <project pre-renders>/<shot id>.
        const QString folder = render::preRenderFolder(dir.path(), *shot);
        QVERIFY(folder.startsWith(dir.path()));
        QCOMPARE(render::preRenderFolder(QString(), *shot), QString());
        {
            render::RenderManager task; task.setComposition(shot);
            task.setDiskCacheDirectory(folder);
            QSignalSpy finished(&task, &render::RenderManager::playbackCacheFinished);
            task.startPlaybackCache(0, size, true, true);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
            task.cancelAll();
        }
        const double fps = double(shot->fpsNumerator()) / qMax(1, shot->fpsDenominator());
        int files = 0;
        for (QDirIterator it(folder, {"*.ovframe"}, QDir::Files, QDirIterator::Subdirectories);
             it.hasNext(); it.next()) {
            ++files;
        }
        QCOMPARE(files, qRound(0.5 * fps));

        // Replace the first pre-rendered frame with green: a parent that reads
        // the pre-render shows green, one that renders the shot shows red.
        render::FrameDiskCache cache; cache.setDirectory(folder);
        const QString key = render::renderStateKey(*shot, nullptr, 0 / fps, size, true, true);
        QVERIFY(cache.contains(key));
        QImage green(size, QImage::Format_RGBA8888); green.fill(Qt::green);
        QVERIFY(cache.store(key, size, QByteArray(reinterpret_cast<const char*>(green.constBits()),
                                                  int(green.sizeInBytes()))));
        const auto centre = [&](render::RenderManager& manager, int request) {
            QSignalSpy ready(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(request, 0.0, size);
            if (!ready.wait(5000) && ready.isEmpty()) return QColor();
            const QByteArray bytes = ready[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 16, 16, 16 * 4,
                          QImage::Format_RGBA8888).pixelColor(8, 8);
        };
        {
            render::RenderManager parent; parent.setComposition(root);
            parent.setPreRenderDirectory(dir.path());
            const QColor colour = centre(parent, 1);
            QVERIFY2(colour.green() > 200 && colour.red() < 40, qPrintable(colour.name()));
            parent.cancelAll();
        }
        {
            render::RenderManager live; live.setComposition(root);
            const QColor colour = centre(live, 1);
            QVERIFY2(colour.red() > 200 && colour.green() < 40, qPrintable(colour.name()));
            live.cancelAll();
        }
        // Editing the shot makes its pre-render stale; it renders live again.
        shot->layerRef(0).planeColor = Qt::blue;
        render::RenderManager parent; parent.setComposition(root);
        parent.setPreRenderDirectory(dir.path());
        const QColor colour = centre(parent, 2);
        QVERIFY2(colour.blue() > 200 && colour.green() < 40, qPrintable(colour.name()));
        parent.cancelAll();
    }
    void textGeometryFillsGlyphsAndRunsGeometryModules() {
        composition::TextStyle style;
        style.text = "O"; style.fontSize = 120;
        style.alignH = composition::TextStyle::AlignH::Center;
        style.alignV = composition::TextStyle::AlignV::Middle;
        const QRectF box(-160, -120, 320, 240);
        const auto geometry = render::textGeometry(box, style);
        QCOMPARE(geometry.size(), 1);
        QCOMPARE(geometry[0].polygons.size(), 2);
        const auto loopOf = [](const plugin::NativeGeometryBatch& batch,
                               const plugin::NativeGeometryPolygon& polygon) {
            QPolygonF loop;
            for (qint32 index : polygon.indices) {
                loop << QPointF(batch.vertices[index].position[0], batch.vertices[index].position[1]);
            }
            return loop;
        };
        const auto areaOf = [](const QPolygonF& loop) {
            double area = 0;
            for (int i = 0; i < loop.size(); ++i) {
                const QPointF a = loop[i], b = loop[(i + 1) % loop.size()];
                area += a.x() * b.y() - b.x() * a.y();
            }
            return area / 2;
        };
        QPolygonF outer = loopOf(geometry[0], geometry[0].polygons[0]);
        QPolygonF hole = loopOf(geometry[0], geometry[0].polygons[1]);
        if (std::abs(areaOf(hole)) > std::abs(areaOf(outer))) std::swap(outer, hole);
        // Flux's winding: outer clockwise seen from the front (Y up), hole the
        // other way, host normal away from the viewer, both loops two-sided.
        QVERIFY(areaOf(outer) < 0); QVERIFY(areaOf(hole) > 0);
        QCOMPARE(geometry[0].vertices[0].normal[2], -1.0f);
        QCOMPARE(geometry[0].polygons[0].flags, quint32(3));
        QVERIFY(geometry[0].extents[3] > geometry[0].extents[2]);

        auto filled = geometry;
        render::fillGeometryPolygons(filled);
        QVERIFY(filled[0].polygons.isEmpty());
        QVERIFY(!filled[0].triangles.isEmpty());
        double covered = 0;
        for (const auto& triangle : filled[0].triangles) {
            QPolygonF corners;
            for (qint32 index : triangle.indices) {
                corners << QPointF(filled[0].vertices[index].position[0],
                                   filled[0].vertices[index].position[1]);
            }
            covered += std::abs(areaOf(corners));
            const QPointF centre = (corners[0] + corners[1] + corners[2]) / 3.0;
            QVERIFY2(!hole.containsPoint(centre, Qt::OddEvenFill), "a triangle fills the hole");
        }
        const double ring = std::abs(areaOf(outer)) - std::abs(areaOf(hole));
        QVERIFY2(std::abs(covered - ring) < ring * 0.001,
                 qPrintable(QStringLiteral("%1 vs %2").arg(covered).arg(ring)));

        const QString extrude = QStringLiteral(OPENVEGAS_SOURCE_DIR)
            + "/SAMPLES/VEGAS_Effects/Plugins/Geometry/Extrude.hfpl";
        if (!QFileInfo::exists(extrude)) QSKIP("The reference Extrude.hfpl is not in SAMPLES");
        const QString dependencies = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const auto metadata = plugin::loadNativePluginMetadata(extrude, dependencies);
        const core::Identifier id("test.native.extrude");
        plugin::registerNativeGeometryModule(id, extrude, dependencies, true, metadata.parameters);
        auto cleanup = qScopeGuard([] { plugin::clearNativeEffectModules(); });
        QStringList values;
        for (const auto& parameter : metadata.parameters) {
            values << (parameter.name == "extrusion" ? QStringLiteral("20") : parameter.defaultValue);
        }
        auto extruded = geometry;
        QVERIFY(plugin::applyNativeGeometryEffect(extruded, id, values, 0, 0, 30, 30.0));
        float nearest = 0, farthest = 0;
        for (const auto& vertex : extruded[0].vertices) {
            nearest = qMax(nearest, vertex.position[2]);
            farthest = qMin(farthest, vertex.position[2]);
        }
        QCOMPARE(nearest, 20.0f); QCOMPARE(farthest, -20.0f);
        QVERIFY(!extruded[0].triangles.isEmpty());   // the walls

        // A text clip with the effect is drawn as a lit mesh: the ring is
        // covered, the hole and the corners are not.
        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(320, 240); scene->setDurationSeconds(1);
        auto& layer = scene->addLayer("Text"); layer.kind = composition::LayerKind::Text;
        composition::Clip clip; clip.durationSeconds = 1;
        composition::Effect text; text.pluginId = core::Identifier("text"); text.name = "Text";
        text.parameterValues = composition::textStyleToParameters(style);
        composition::Effect geometryEffect; geometryEffect.pluginId = id;
        geometryEffect.name = "Extrude"; geometryEffect.parameterValues = values;
        clip.effects << text << geometryEffect;
        layer.clips.append(clip);
        render::RenderManager manager; manager.setComposition(scene);
        QSignalSpy ready(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(1, 0.0, QSize(320, 240), true, true);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        manager.cancelAll();
        const QByteArray bytes = ready[0][1].toByteArray();
        const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 320, 240, 320 * 4,
                           QImage::Format_RGBA8888);
        QRectF ringBounds = outer.boundingRect();
        const QPoint ringPixel(qRound(160 + (ringBounds.left() + QPolygonF(hole).boundingRect().left()) / 2),
                               qRound(120 - ringBounds.center().y()));
        QVERIFY2(image.pixelColor(ringPixel).alpha() > 200,
                 qPrintable(QStringLiteral("ring %1,%2").arg(ringPixel.x()).arg(ringPixel.y())));
        QCOMPARE(image.pixelColor(qRound(160 + hole.boundingRect().center().x()),
                                  qRound(120 - hole.boundingRect().center().y())).alpha(), 0);
        QCOMPARE(image.pixelColor(2, 2).alpha(), 0);
    }
    void oversizedParticleTextureWarning() {
        composition::Composition scene; scene.setSize(640, 360);
        auto big = std::make_shared<composition::Composition>(); big->setSize(2048, 1536);
        // Ids by value: a reference returned by addLayer dangles once the next
        // addLayer grows the layer vector.
        const QString textureId = scene.addLayer("Texture").id.value();
        composition::Clip nested; nested.durationSeconds = 1; nested.nestedComposition = big;
        scene.layerRef(0).clips.append(nested);
        auto& planeLayer = scene.addLayer("Plane"); planeLayer.kind = composition::LayerKind::Plane;
        const QString planeId = planeLayer.id.value();
        QCOMPARE(ui::layerTextureSize(scene, textureId, nullptr), QSize(2048, 1536));
        QCOMPARE(ui::layerTextureSize(scene, planeId, nullptr), QSize(640, 360));
        QCOMPARE(ui::layerTextureSize(scene, "missing", nullptr), QSize());

        int shown = 0;
        const auto closeNextBox = [&shown] {
            QTimer::singleShot(0, [&shown] {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    ++shown;
                    box->button(QMessageBox::Ok)->click();
                }
            });
        };
        plugin::EffectSpec particles; particles.displayName = "Particles";
        particles.category = "Particles & Simulation";
        plugin::EffectSpec blur; blur.displayName = "Blur"; blur.category = "Blurs";
        const bool enabled = ui::promptEnabled("OversizedAssets");
        closeNextBox();
        ui::warnIfOversizedTexture(nullptr, particles, scene, textureId, nullptr);
        QCOMPARE(shown, enabled ? 1 : 0);
        // Not a particle effect, or not larger than 1024 in both directions.
        ui::warnIfOversizedTexture(nullptr, blur, scene, textureId, nullptr);
        ui::warnIfOversizedTexture(nullptr, particles, scene, planeId, nullptr);
        QCoreApplication::processEvents();
        QCOMPARE(shown, enabled ? 1 : 0);
    }
    void mediaRowsDropOntoTimeline() {
        // The Media panel's drag carries the asset's path, or the shot's id.
        QTemporaryDir folder; QVERIFY(folder.isValid());
        QImage still(4, 4, QImage::Format_RGBA8888); still.fill(Qt::green);
        const QString path = QDir::cleanPath(folder.filePath("drop.png")); QVERIFY(still.save(path));
        media::MediaManager media; QVERIFY(media.importFile(path).isSuccess());
        ui::MediaPanel mediaPanel; mediaPanel.setMediaManager(&media);
        ui::MediaPanel::CompositeShotEntry entry; entry.id = "shot-1"; entry.name = "Shot";
        mediaPanel.setCompositeShots({entry});
        auto* list = mediaPanel.findChild<QListWidget*>(); QVERIFY(list);
        std::unique_ptr<QMimeData> shotDrag, mediaDrag;
        for (int row = 0; row < list->count(); ++row) {
            std::unique_ptr<QMimeData> mime(mediaPanel.dragData(list->item(row)));
            if (!mime) continue;
            if (mime->hasFormat(ui::kCompositeShotMimeType)) shotDrag = std::move(mime);
            else if (mime->hasFormat(ui::kMediaMimeType)) mediaDrag = std::move(mime);
        }
        QVERIFY(shotDrag); QVERIFY(mediaDrag);
        QCOMPARE(QString::fromUtf8(shotDrag->data(ui::kCompositeShotMimeType)), QString("shot-1"));
        QCOMPARE(QDir::cleanPath(QString::fromUtf8(mediaDrag->data(ui::kMediaMimeType))),
                 QDir::cleanPath(media.assetByFilePath(path).filePath()));
        QCOMPARE(mediaDrag->urls().size(), 1);

        // On the timeline they land anywhere, at the time under the cursor and
        // above the layer whose row they are let go over.
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); QVERIFY(canvas);
        QSignalSpy mediaDrops(panel.get(), &ui::TimelineWidget::mediaDropped);
        QSignalSpy shotDrops(panel.get(), &ui::TimelineWidget::compositeShotDropped);
        const int y = ui::kTimelineRulerHeight + 11;
        const double seconds = 2.0;
        const QPoint pos(int(std::lround(seconds * canvas->pixelsPerSecond())) - canvas->hScrollOffset(), y);
        for (QMimeData* mime : {mediaDrag.get(), shotDrag.get()}) {
            QDragEnterEvent enter(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &enter);
            QVERIFY(enter.isAccepted());
            QDragMoveEvent move(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &move);
            QVERIFY(move.isAccepted());
            QDropEvent dropEvent(QPointF(pos), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &dropEvent);
            QVERIFY(dropEvent.isAccepted());
        }
        QCOMPARE(mediaDrops.size(), 1); QCOMPARE(shotDrops.size(), 1);
        QCOMPARE(shotDrops.at(0).at(0).toString(), QString("shot-1"));
        QCOMPARE(mediaDrops.at(0).at(1).toInt(), 0);
        QVERIFY(qAbs(mediaDrops.at(0).at(2).toDouble() - seconds) < 0.05);
    }
    void effectsPanelDropOntoClip() {
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); const int y = ui::kTimelineRulerHeight + 11;
        const auto& clip = comp->layers()[0].clips[0];
        const int effectsBefore = clip.effects.size();
        const int historyBefore = history.count();
        const auto drop = [&](const QString& id, double seconds) {
            const QPoint pos(int(std::lround(seconds * canvas->pixelsPerSecond())) - canvas->hScrollOffset(), y);
            QMimeData mime; mime.setData(ui::kEffectMimeType, id.toUtf8());
            QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &enter);
            QDragMoveEvent move(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &move);
            QDropEvent dropEvent(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &dropEvent);
            return move.isAccepted() && dropEvent.isAccepted();
        };
        // A transition dropped near the end of a clip becomes its fade out.
        const double nearEnd = clip.startSeconds + clip.durationSeconds * 0.9;
        QVERIFY(drop("test.cross", nearEnd));
        QCOMPARE(comp->layers()[0].clips[0].effects.size(), effectsBefore + 1);
        const auto& added = comp->layers()[0].clips[0].effects.last();
        QCOMPARE(added.pluginId.value(), QString("test.cross"));
        QCOMPARE(added.transitionEdge, composition::TransitionEdge::Out);
        QCOMPARE(history.count(), historyBefore + 1);
        history.undo();
        QCOMPARE(comp->layers()[0].clips[0].effects.size(), effectsBefore);
        // An ordinary effect is appended to the clip like the Add menu does.
        QVERIFY(drop("test.blur", clip.startSeconds + clip.durationSeconds * 0.5));
        QCOMPARE(comp->layers()[0].clips[0].effects.last().transitionEdge, composition::TransitionEdge::None);
        history.undo();
        QCOMPARE(comp->layers()[0].clips[0].effects.size(), effectsBefore);
        // Empty space refuses the drop.
        QVERIFY(!drop("test.blur", comp->durationSeconds() + 5));
    }
    void canvasMoveSliceAndUndo() {
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); const int y = ui::kTimelineRulerHeight + 11;
        panel->setSnapEnabled(false);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(90, y));
        const QPoint finish(140, y);
        QMouseEvent move(QEvent::MouseMove, finish, canvas->mapToGlobal(finish), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move); QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, finish);
        QVERIFY(comp->layers()[0].clips[0].startSeconds > 0); QCOMPARE(history.count(), 1);
        history.undo(); QCOMPARE(comp->layers()[0].clips[0].startSeconds, 0.);
        panel->setTool(ui::EditorTool::Slice); QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(150, y));
        QCOMPARE(comp->layers()[0].clips.size(), 2); history.undo(); QCOMPARE(comp->layers()[0].clips.size(), 1);
        const double before = comp->layers()[0].clips[0].startSeconds;
        panel->setTool(ui::EditorTool::Select);
        const int paramY = ui::kTimelineRulerHeight + tree()->visualItemRect(parameter(0)).center().y();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(180, paramY));
        QCOMPARE(comp->layers()[0].clips[0].startSeconds, before);
    }
    void blurRender_data() {
        QTest::addColumn<int>("parameter"); QTest::addColumn<QString>("value");
        QTest::newRow("radius") << 0 << "4.5";
        QTest::newRow("iterations") << 1 << "3";
        QTest::newRow("direction") << 2 << "Vertical";
        QTest::newRow("edge") << 3 << "false";
    }
    void blurRender() {
        QFETCH(int, parameter); QFETCH(QString, value);
        QImage source(32, 32, QImage::Format_RGBA8888); source.fill(Qt::red);
        for (int y = 0; y < 16; ++y) for (int x = 5; x < 18; ++x) source.setPixelColor(x, y, Qt::blue);
        QStringList values{"2.0", "2", "Horizontal & Vertical", "true"};
        QImage before = source, after = source;
        QVERIFY(plugin::applyEffectToImage(before, core::Identifier(plugin::kBuiltinBlur), values));
        values[parameter] = value;
        QVERIFY(plugin::applyEffectToImage(after, core::Identifier(plugin::kBuiltinBlur), values));
        QVERIFY(before != after);
    }
    void colorRender_data() {
        QTest::addColumn<int>("parameter");
        for (int p = 0; p < 12; ++p) QTest::newRow(qPrintable(QString::number(p))) << p;
    }
    void colorRender() {
        QFETCH(int, parameter);
        QImage source(24, 24, QImage::Format_RGBA8888);
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) source.setPixelColor(x, y, QColor(x * 8, y * 8, 70));
        QStringList values{"1", "0", "#ffffff", ".3", "20", ".4", ".3", "20", ".4", ".3", "20", ".4"};
        QImage before = source, after = source;
        QVERIFY(plugin::applyEffectToImage(before, core::Identifier(plugin::kBuiltinColorWheels), values));
        values[parameter] = parameter == 2 ? "#bfff8f" : QString::number(values[parameter].toDouble() + .5);
        QVERIFY(plugin::applyEffectToImage(after, core::Identifier(plugin::kBuiltinColorWheels), values));
        QVERIFY(before != after);
    }
    void compositionProperties() {
        QSignalSpy changes(panel.get(), &ui::TimelineWidget::compositionPropertiesChanged);
        QTimer::singleShot(0, [this] {
            auto* dialog = panel->findChild<QDialog*>("CompositionSettingsDialog"); QVERIFY(dialog);
            dialog->findChild<QSpinBox*>("spinBoxWidth")->setValue(1280);
            dialog->findChild<QDoubleSpinBox*>("spinBoxDuration")->setValue(12.5);
            dialog->findChild<QLineEdit*>("lineEditName")->setText("Edited shot");
            dialog->findChild<QDoubleSpinBox*>("doubleSpinBoxShutterAngle")->setValue(90);
            dialog->findChild<QSpinBox*>("spinBoxMaxSamples")->setValue(8);
            // The reference's Fog group: its fields follow the Enable box.
            auto* fogOn = dialog->findChild<QCheckBox*>("checkBoxFogEnable"); QVERIFY(fogOn);
            auto* fogFar = dialog->findChild<QDoubleSpinBox*>("doubleSpinBoxFarClipDistance"); QVERIFY(fogFar);
            QVERIFY(!fogFar->isEnabled());
            fogOn->setChecked(true); QVERIFY(fogFar->isEnabled());
            QCOMPARE(fogFar->maximum(), 999999999.0);
            fogFar->setValue(3000);
            auto* falloff = dialog->findChild<QComboBox*>("comboBoxFallOff"); QVERIFY(falloff);
            QCOMPARE(falloff->count(), 3);
            falloff->setCurrentIndex(2);
            auto* aspect = dialog->findChild<QComboBox*>("comboBoxAspectRatio"); QVERIFY(aspect);
            aspect->setCurrentIndex(composition::Composition::DvPal);
            dialog->accept();
        });
        auto* settings = panel->findChild<QToolButton*>("timelineSettings");
        QVERIFY(settings);
        settings->click();
        QCOMPARE(comp->width(), 1280); QCOMPARE(comp->durationSeconds(), 12.5); QCOMPARE(comp->name(), QString("Edited shot"));
        QCOMPARE(comp->renderSettings().shutterAngle, 90.0); QCOMPARE(comp->renderSettings().maxNumOfSamples, 8);
        QVERIFY(comp->renderSettings().fogEnabled);
        QCOMPARE(comp->renderSettings().fogFarDistance, 3000.0);
        QCOMPARE(comp->renderSettings().fogFalloff, 2);
        QCOMPARE(comp->pixelAspect(), int(composition::Composition::DvPal));
        QCOMPARE(comp->pixelAspectValue(), 12.0 / 11.0);
        QCOMPARE(changes.count(), 1); history.undo(); QCOMPARE(comp->durationSeconds(), 50.);
        QCOMPARE(comp->renderSettings().shutterAngle, 180.0);
        QVERIFY(!comp->renderSettings().fogEnabled);
        history.redo(); QCOMPARE(comp->durationSeconds(), 12.5);
        QCOMPARE(comp->renderSettings().shutterAngle, 90.0);
        QTimer::singleShot(0, [this] { panel->findChild<QDialog*>("CompositionSettingsDialog")->reject(); });
        panel->editCompositionProperties(); QCOMPARE(history.count(), 1);
    }
    void exportQueueRunsTasksFromSnapshots() {
        // Options > Export > Time Format, for Duration and Elapsed.
        QCOMPARE(ui::formatExportTime(65.5, 25, ui::ExportTimeFormat::Timecode), QString("00:01:05:12"));
        QCOMPARE(ui::formatExportTime(3725, 30, ui::ExportTimeFormat::Natural), QString("1h 02m 05s"));
        QCOMPARE(ui::formatExportTime(65, 30, ui::ExportTimeFormat::Natural), QString("1m 05s"));
        QCOMPARE(ui::formatExportTime(65.5, 30, ui::ExportTimeFormat::Seconds), QString("65.5 s"));

        QTemporaryDir folder; QVERIFY(folder.isValid());
        auto media = std::make_shared<media::MediaManager>();
        composition::Composition project; project.setName("Main"); project.setSize(16, 16);
        project.setFrameRate(10, 1); project.setDurationSeconds(1);
        auto& plane = project.addLayer("Plane"); plane.kind = composition::LayerKind::Plane;
        plane.planeColor = Qt::red;
        composition::Clip clip; clip.durationSeconds = 1; plane.clips << clip;
        auto extra = std::make_shared<composition::Composition>();
        extra->setName("Extra"); extra->setSize(8, 8); extra->setFrameRate(10, 1);
        project.addCompositeShot(extra);

        ui::ExportQueue queue;
        queue.setMediaManager(media);
        queue.setSnapshotDirectory(folder.filePath("snapshots"));
        queue.setTasksFile(folder.filePath("ExportTasks.xml"));
        const QString output = folder.filePath("out/main.png");
        const QString id = queue.addTask(project, project, *media, "PNG (.png)", output, 0, 2);
        QVERIFY(!id.isEmpty());
        const QString other = queue.addTask(project, *extra, *media, "PNG (.png)", folder.filePath("out/extra.png"), 0, 0);
        QCOMPARE(queue.tasks().size(), 2);
        const ui::ExportTask task = queue.tasks().first();
        QVERIFY(QFileInfo::exists(task.snapshotPath));
        QCOMPARE(task.name, QString("Main"));
        QCOMPARE(task.frameCount(), 3);
        QCOMPARE(task.durationSeconds(), 0.3);
        QCOMPARE(queue.tasks().at(1).shotId, extra->id().value());
        // A later edit does not reach the queued task: it exports its snapshot.
        project.layerRef(0).planeColor = Qt::blue;

        // The queue survives a restart.
        ui::ExportQueue restored; restored.setTasksFile(queue.tasksFile());
        QVERIFY(restored.load());
        QCOMPARE(restored.tasks().size(), 2);
        QCOMPARE(restored.tasks().first().outputPath, output);
        QCOMPARE(restored.tasks().first().lastFrame, 2);

        // Duplicates get snapshots of their own; removing a task removes its own.
        queue.duplicateTasks({other});
        QCOMPARE(queue.tasks().size(), 3);
        QVERIFY(queue.tasks().at(2).snapshotPath != queue.tasks().at(1).snapshotPath);
        const QString duplicateSnapshot = queue.tasks().at(2).snapshotPath;
        queue.removeTasks({queue.tasks().at(2).id});
        QCOMPARE(queue.tasks().size(), 2);
        QVERIFY(!QFileInfo::exists(duplicateSnapshot));

        // Run: every Ready task, one after the other, then queueFinished.
        ui::ExportQueueView view(&queue);
        auto* start = view.findChild<QPushButton*>("pushButtonStartExporting"); QVERIFY(start);
        auto* tree = view.findChild<QTreeWidget*>("treeViewTasks"); QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("Main"));
        QCOMPARE(tree->topLevelItem(0)->text(3), QString("Ready"));
        QSignalSpy done(&queue, &ui::ExportQueue::queueFinished);
        start->click();
        QVERIFY(queue.isRunning());
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 20000));
        QVERIFY(!queue.isRunning());
        QCOMPARE(queue.tasks().at(0).state, ui::ExportTask::State::Finished);
        QCOMPARE(queue.tasks().at(1).state, ui::ExportTask::State::Finished);
        QVERIFY(!QFileInfo::exists(task.snapshotPath));
        QVERIFY(queue.tasks().at(0).elapsedMilliseconds > 0);
        QCOMPARE(tree->topLevelItem(0)->text(3), QString("Finished"));
        // Three frames of the shot as it was queued: red, not blue.
        for (int frame = 1; frame <= 3; ++frame) {
            const QImage image(folder.filePath(QStringLiteral("out/main-%1.png").arg(frame, 8, 10, QLatin1Char('0'))));
            QVERIFY2(!image.isNull(), qPrintable(QString::number(frame)));
            QVERIFY(image.pixelColor(8, 8).red() > 200 && image.pixelColor(8, 8).blue() < 60);
        }
        QVERIFY(QFileInfo::exists(folder.filePath("out/extra-00000001.png")));
        queue.removeFinishedTasks();
        QCOMPARE(queue.tasks().size(), 0);
        QVERIFY(view.findChild<QLabel*>("labelEmptyQueue")->isVisibleTo(&view));
        view.resize(520, 220);
    }
    void compositionTemplatesAndFormats() {
        QTemporaryDir folder; QVERIFY(folder.isValid());
        app::setUserTemplateFolderOverride(folder.path());
        const auto restore = qScopeGuard([] { app::setUserTemplateFolderOverride(QString()); });
        // The rates of the reference's Frame Rate list read back as fractions.
        int numerator = 0, denominator = 0;
        composition::Composition::frameRateFraction(29.97, &numerator, &denominator);
        QCOMPARE(numerator, 30000); QCOMPARE(denominator, 1001);
        composition::Composition::frameRateFraction(59.940, &numerator, &denominator);
        QCOMPARE(numerator, 60000); QCOMPARE(denominator, 1001);
        composition::Composition::frameRateFraction(25, &numerator, &denominator);
        QCOMPARE(numerator, 25); QCOMPARE(denominator, 1);
        composition::Composition::frameRateFraction(12.5, &numerator, &denominator);
        QCOMPARE(double(numerator) / denominator, 12.5);
        // The PAR values of Project.dll.
        QCOMPARE(composition::Composition::pixelAspectValue(composition::Composition::DvNtsc, 1), 10.0 / 11.0);
        QCOMPARE(composition::Composition::pixelAspectValue(composition::Composition::HdAnamorphic1080, 1), 4.0 / 3.0);
        QCOMPARE(composition::Composition::pixelAspectValue(composition::Composition::CustomAspect, 1.25), 1.25);

        ui::CompositionSettingsDialog::Values values;
        values.name = "Shot"; values.width = 1920; values.height = 1080;
        values.fpsNumerator = 30; values.fpsDenominator = 1;
        ui::CompositionSettingsDialog::Values timeline = values;
        timeline.width = 1280; timeline.height = 720; timeline.fpsNumerator = 25;
        ui::CompositionSettingsDialog dialog(values, timeline);
        auto* templates = dialog.findChild<QComboBox*>("comboBoxTemplate"); QVERIFY(templates);
        auto* rate = dialog.findChild<QComboBox*>("comboBoxFrameRate"); QVERIFY(rate);
        auto* width = dialog.findChild<QSpinBox*>("spinBoxWidth");
        auto* remove = dialog.findChild<QToolButton*>("toolButtonDelete"); QVERIFY(remove);
        QCOMPARE(rate->count(), 8);
        QCOMPARE(dialog.findChild<QComboBox*>("comboBoxAspectRatio")->count(), 9);
        QCOMPARE(dialog.findChild<QLabel*>("labelValueAudioSampleRate")->text(), QString("48000 Hz"));
        // 1920x1080 at 30 is a built-in template, selected by itself.
        QCOMPARE(templates->currentData().toString(), QString("fullhd30"));
        QVERIFY(!remove->isEnabled());
        // A template applies its format; NTSC stays a fraction.
        templates->setCurrentIndex(templates->findData("fullhd2997"));
        emit templates->activated(templates->currentIndex());
        ui::CompositionSettingsDialog::Values picked = dialog.values();
        QCOMPARE(picked.fpsNumerator, 30000); QCOMPARE(picked.fpsDenominator, 1001);
        QCOMPARE(picked.width, 1920);
        // Editing a field leaves the template: Custom.
        width->setValue(1000);
        QCOMPARE(templates->currentIndex(), 0);
        // Match Timeline takes the editor timeline's format.
        dialog.findChild<QToolButton*>("toolButtonMatchTimeline")->click();
        picked = dialog.values();
        QCOMPARE(QSize(picked.width, picked.height), QSize(1280, 720));
        QCOMPARE(picked.fpsNumerator, 25);
        // A typed rate is taken as typed.
        rate->setEditText("12.5");
        picked = dialog.values();
        QCOMPARE(double(picked.fpsNumerator) / picked.fpsDenominator, 12.5);

        // User templates: the reference's .hft format, listed and deletable.
        app::AVTemplate mine; mine.name = "Square 1080"; mine.width = 1080; mine.height = 1080;
        mine.frameRate = 30000.0 / 1001.0; mine.pixelAspect = composition::Composition::CustomAspect;
        mine.pixelAspectValue = 1.25;
        const app::AVTemplate saved = app::saveUserTemplate(mine);
        QVERIFY(!saved.id.isEmpty());
        QDomDocument file; QFile hft(saved.filePath);
        QVERIFY(hft.open(QIODevice::ReadOnly)); QVERIFY(file.setContent(hft.readAll())); hft.close();
        const QDomElement node = file.documentElement().firstChildElement("Template");
        QCOMPARE(file.documentElement().tagName(), QString("Templates"));
        QCOMPARE(node.attribute("Version"), QString("0"));
        QCOMPARE(node.firstChildElement("SystemTemplate").text(), QString("0"));
        QCOMPARE(node.firstChildElement("FrameRate").text(), QString("29.97"));
        QCOMPARE(node.firstChildElement("PAR").text(), QString("8"));
        QCOMPARE(node.firstChildElement("PARValue").text(), QString("1.25"));
        const QVector<app::AVTemplate> loaded = app::loadTemplates(folder.path());
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().name, QString("Square 1080"));
        QCOMPARE(loaded.first().pixelAspectValue, 1.25);
        QCOMPARE(app::templateById(saved.id).width, 1080);
        ui::CompositionSettingsDialog withUser(values, timeline);
        auto* userTemplates = withUser.findChild<QComboBox*>("comboBoxTemplate");
        QVERIFY(userTemplates->findData(saved.id) > 0);
        QVERIFY(app::deleteUserTemplate(loaded.first()));
        QVERIFY(app::loadTemplates(folder.path()).isEmpty());
        QVERIFY(!app::deleteUserTemplate(app::templateById("fullhd30")));   // built-in

        // PAR, the sample rate and an NTSC rate survive a save.
        composition::Composition shot; shot.setSize(720, 480); shot.setFrameRate(30000, 1001);
        shot.setPixelAspect(composition::Composition::CustomAspect, 1.1); shot.setAudioSampleRate(44100);
        media::MediaManager media;
        const QString path = folder.filePath("Format.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, shot, media).isSuccess());
        composition::Composition back; media::MediaManager backMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &back, &backMedia).isSuccess());
        QCOMPARE(back.fpsNumerator(), 30000); QCOMPARE(back.fpsDenominator(), 1001);
        QCOMPARE(back.pixelAspect(), int(composition::Composition::CustomAspect));
        QCOMPARE(back.customPixelAspect(), 1.1);
        QCOMPARE(back.audioSampleRate(), 44100);
        dialog.show(); QTest::qWait(10);
        QVERIFY(dialog.grab().save(testArtifactPath(QStringLiteral("composition-settings.png"))));
    }
    void trimmerPanel() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString mediaPath = dir.filePath("source.png");
        QImage(32, 18, QImage::Format_ARGB32_Premultiplied).save(mediaPath);
        media::MediaManager manager; QVERIFY(manager.importFile(mediaPath).isSuccess());
        auto* stored = manager.assetByFilePathForEdit(mediaPath); QVERIFY(stored);
        stored->setDurationSeconds(10.0); stored->setTrimInPoint(30); stored->setTrimOutPoint(240);

        ui::TrimmerPanel trimmer; trimmer.resize(760, 280); trimmer.setFrameRate(30.0);
        trimmer.setMediaManager(&manager); trimmer.openAsset(manager.assetByFilePath(mediaPath));
        trimmer.show(); QTest::qWait(20);
        QCOMPARE(trimmer.objectName(), QString("trimmer-panel"));
        QVERIFY(trimmer.findChild<QWidget*>("trimmer-toolbar-top"));
        QVERIFY(trimmer.findChild<QWidget*>("trimmer-toolbar-bottom"));
        QVERIFY(trimmer.findChild<QWidget*>("trimmer-in-out"));
        QVERIFY(trimmer.findChild<QSlider*>("TrimmerSlider"));
        QCOMPARE(trimmer.trimInPoint(), 30); QCOMPARE(trimmer.trimOutPoint(), 240);

        QSignalSpy changed(&trimmer, &ui::TrimmerPanel::trimRangeChanged);
        trimmer.trimmerWidget()->setPositionSeconds(2.0); trimmer.setTrimInPoint();
        trimmer.trimmerWidget()->setPositionSeconds(8.0); trimmer.setTrimOutPoint();
        QCOMPARE(trimmer.trimInPoint(), 60); QCOMPARE(trimmer.trimOutPoint(), 240);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(manager.assetByFilePath(mediaPath).trimInPoint(), 60);

        QSignalSpy inserted(&trimmer, &ui::TrimmerPanel::insertRequested);
        QSignalSpy overlaid(&trimmer, &ui::TrimmerPanel::overlayRequested);
        trimmer.findChild<QToolButton*>("trimmerInsert")->click();
        trimmer.findChild<QToolButton*>("trimmerOverlay")->click();
        QCOMPARE(inserted.count(), 1); QCOMPARE(overlaid.count(), 1);
        QCOMPARE(inserted.at(0).at(1).toInt(), 60);
        QCOMPARE(inserted.at(0).at(2).toInt(), 240);
        QVERIFY(trimmer.grab().save(testArtifactPath(QStringLiteral("trimmer-panel.png"))));
    }
    void menuBarInventory() {
        QFile file(QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/ui/MainWindow.ui");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&file);
        QSet<QString> widgets, actions;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            if (xml.name() == QLatin1String("widget")) widgets.insert(xml.attributes().value("name").toString());
            if (xml.name() == QLatin1String("action")) actions.insert(xml.attributes().value("name").toString());
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        const QStringList menus{"menuBar", "menuFile", "menuOpenRecent", "menuRecord",
            "menuImport", "menuEdit", "menuEffects", "menuExport", "menuHelp", "menuDebug"};
        for (const QString& name : menus) QVERIFY2(widgets.contains(name), qPrintable(name));
        const QStringList referenceActions{"actionNew", "actionOpen", "actionRecoveredSaves",
            "actionSave", "actionSaveAs", "actionProjectSettings", "actionOptions", "actionExit",
            "actionUndo", "actionRedo", "actionSlice", "actionSelectAll", "actionCut", "actionCopy",
            "actionPaste", "actionDuplicate", "actionDelete", "actionRippleDelete",
            "actionPasteAttributes", "actionRemoveAttributes", "actionRemoveEffects", "actionAbout",
            "actionOnlineHelp", "actionReloadExternalStylesheet", "actionShowRenderTimings",
            "actionExportFontList", "actionShowHardwareDecodingIndicator", "actionPrintAnalytics",
            "actionEnableEffectPresetCreation"};
        QCOMPARE(referenceActions.size(), 29);
        for (const QString& name : referenceActions) QVERIFY2(actions.contains(name), qPrintable(name));
    }
    void playbackCache() {
        auto scene = std::make_shared<composition::Composition>(); scene->setSize(24, 24); scene->setFrameRate(4, 1); scene->setDurationSeconds(1);
        auto& layer = scene->addLayer("Plane"); layer.kind = composition::LayerKind::Plane; layer.planeColor = Qt::red;
        composition::Clip clip; clip.durationSeconds = 1; layer.clips.append(clip);
        render::RenderManager manager; manager.setComposition(scene);
        QSignalSpy done(&manager, &render::RenderManager::playbackCacheFinished);
        manager.startPlaybackCache(0, QSize(24, 24)); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QCOMPARE(manager.cachedFrameCount(), 4);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(42, 0, QSize(24, 24)); QTRY_COMPARE(frames.count(), 1);
        const QByteArray red = frames[0][1].toByteArray(); scene->layerRef(0).planeColor = Qt::green;
        manager.requestFrame(43, 0, QSize(24, 24)); QTRY_COMPARE(frames.count(), 2);
        QVERIFY(red != frames[1][1].toByteArray());
        scene->layerRef(0).planeColor = Qt::red; scene->layerRef(0).kind = composition::LayerKind::Point;
        manager.requestFrame(44, 0, QSize(24, 24)); QTRY_COMPARE(frames.count(), 3);
        QVERIFY(red != frames[2][1].toByteArray());
        manager.cancelAll();
    }
};
int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/arial.ttf");
    app.setFont(QFont("Arial", 9)); ui::applyTheme(&app);
    TimelineRegression tests; return QTest::qExec(&tests, argc, argv);
}
#include "timeline_regression.moc"
