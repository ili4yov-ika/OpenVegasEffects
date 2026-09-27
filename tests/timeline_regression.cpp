#include <QDialog>
#include <QTimer>
#include <QSettings>
#include <QScopeGuard>
#include "ui/TimelineWidget.h"
#include "ui/EffectInspector.h"
#include "ui/LayerPanel.h"
#include "ui/TrackPanel.h"
#include "ui/Preview360VideoPanel.h"
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
#include "ui/TimelineValueGraphView.h"
#include "ui/Theme.h"
#include "plugin/PluginManager.h"
#include "plugin/EffectRender.h"
#include "render/RenderManager.h"
#include "project/VegfxSerializer.h"
#include "app/ProjectDefaults.h"
#include "composition/MotionTracker.h"
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
    TestPlugins() {
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
        return id == behavior.id ? behavior : effect;
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
        dimension->setCurrentIndex(1); QTest::qWait(10);
        QCOMPARE(comp->layers()[0].dimension, composition::LayerDimension::ThreeD);
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
            auto* dialog = panel->findChild<QDialog*>("timelineCompositionProperties"); QVERIFY(dialog);
            dialog->findChild<QSpinBox*>("compositionWidth")->setValue(1280);
            dialog->findChild<QDoubleSpinBox*>("compositionDuration")->setValue(12.5);
            dialog->findChild<QLineEdit*>("compositionName")->setText("Edited shot");
            dialog->accept();
        });
        auto* settings = panel->findChild<QToolButton*>("timelineSettings");
        QVERIFY(settings);
        settings->click();
        QCOMPARE(comp->width(), 1280); QCOMPARE(comp->durationSeconds(), 12.5); QCOMPARE(comp->name(), QString("Edited shot"));
        QCOMPARE(changes.count(), 1); history.undo(); QCOMPARE(comp->durationSeconds(), 50.);
        history.redo(); QCOMPARE(comp->durationSeconds(), 12.5);
        QTimer::singleShot(0, [this] { panel->findChild<QDialog*>("timelineCompositionProperties")->reject(); });
        panel->editCompositionProperties(); QCOMPARE(history.count(), 1);
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
