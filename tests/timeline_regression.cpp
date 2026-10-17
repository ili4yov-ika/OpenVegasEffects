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
#include "cache/CacheDB.h"
#include "media/DecodedFrameCache.h"
#include "ui/ModelNodeOverlay.h"
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
#include "ui/ScrubberSpinBox.h"
#include "ui/LayoutTransformCommand.h"
#include <QScrollArea>
#include "ui/TimelineValueGraphView.h"
#include "ui/Theme.h"
#include "ui/TimelineParameterEditor.h"
#include "plugin/PluginManager.h"
#include "plugin/EffectRender.h"
#include "render/PreRenderManager.h"
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
#include "media/ConformedAudio.h"
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
#include "model3d/ModelLoader.h"
#include "ui/ModelAnimationDialog.h"
#include "ui/Model3DSettingsDialog.h"
#include "ui/SceneObjectOverlay.h"
#include "ui/CameraRule.h"
#include "model3d/Renderer3D.h"
#include "render/FrameDiskCache.h"
#include "render/TextGeometry.h"
#include "render/LayerGeometry.h"
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
#include <QTranslator>
#include <QSqlDatabase>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#undef near // Win32 legacy macro conflicts with geometry helpers.
#endif
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
// A small FBX writer for the tests: records with values, written as the
// binary format (7.4 with 32-bit offsets, or 7.5 with 64-bit ones, arrays
// raw or deflated) or as ASCII.
struct FbxValue
{
    char type = 'S';                 // S, L, I, D, or arrays d, i, l, f
    QByteArray text;
    double number = 0.0;
    QVector<double> values;
    static FbxValue S(const QByteArray& t) { FbxValue v; v.type = 'S'; v.text = t; return v; }
    static FbxValue L(qint64 n) { FbxValue v; v.type = 'L'; v.number = double(n); return v; }
    static FbxValue I(int n) { FbxValue v; v.type = 'I'; v.number = n; return v; }
    static FbxValue D(double n) { FbxValue v; v.type = 'D'; v.number = n; return v; }
    static FbxValue A(char type, const QVector<double>& list) { FbxValue v; v.type = type; v.values = list; return v; }
};
struct FbxRecord
{
    QByteArray name;
    QVector<FbxValue> values;
    QVector<FbxRecord> children;
};

QByteArray fbxBinaryRecord(const FbxRecord& record, qint64 offset, bool wide, bool deflate)
{
    QByteArray values;
    {
        QDataStream out(&values, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        for (const FbxValue& v : record.values) {
            out << qint8(v.type);
            switch (v.type) {
            case 'S': out << quint32(v.text.size()); out.writeRawData(v.text.constData(), int(v.text.size())); break;
            case 'L': out << qint64(v.number); break;
            case 'I': out << qint32(v.number); break;
            case 'D': out.setFloatingPointPrecision(QDataStream::DoublePrecision); out << v.number; break;
            default: {
                QByteArray raw;
                QDataStream data(&raw, QIODevice::WriteOnly);
                data.setByteOrder(QDataStream::LittleEndian);
                for (double x : v.values) {
                    if (v.type == 'd') { data.setFloatingPointPrecision(QDataStream::DoublePrecision); data << x; }
                    else if (v.type == 'f') { data.setFloatingPointPrecision(QDataStream::SinglePrecision); data << float(x); }
                    else if (v.type == 'l') data << qint64(x);
                    else data << qint32(x);
                }
                QByteArray stored = deflate ? qCompress(raw).mid(4) : raw;
                out << quint32(v.values.size()) << quint32(deflate ? 1 : 0) << quint32(stored.size());
                out.writeRawData(stored.constData(), int(stored.size()));
            }
            }
        }
    }
    const int header = wide ? 25 : 13;
    qint64 at = offset + header + record.name.size() + values.size();
    QByteArray children;
    for (const FbxRecord& child : record.children) {
        const QByteArray bytes = fbxBinaryRecord(child, at, wide, deflate);
        children += bytes;
        at += bytes.size();
    }
    if (!record.children.isEmpty()) { children += QByteArray(header, '\0'); at += header; }
    QByteArray out;
    QDataStream head(&out, QIODevice::WriteOnly);
    head.setByteOrder(QDataStream::LittleEndian);
    if (wide) head << quint64(at) << quint64(record.values.size()) << quint64(values.size());
    else head << quint32(at) << quint32(record.values.size()) << quint32(values.size());
    head << quint8(record.name.size());
    out += record.name + values + children;
    return out;
}

QByteArray fbxBinary(const QVector<FbxRecord>& records, quint32 version, bool deflate)
{
    QByteArray out("Kaydara FBX Binary  ", 20);
    out += '\0'; out += char(0x1A); out += '\0';
    QByteArray number(4, '\0'); qToLittleEndian<quint32>(version, number.data()); out += number;
    const bool wide = version >= 7500;
    for (const FbxRecord& record : records) out += fbxBinaryRecord(record, out.size(), wide, deflate);
    out += QByteArray(wide ? 25 : 13, '\0');
    return out;
}

QByteArray fbxAscii(const QVector<FbxRecord>& records, int depth = 0)
{
    QByteArray out;
    const QByteArray indent(depth, '\t');
    for (const FbxRecord& record : records) {
        out += indent + record.name + ": ";
        QByteArrayList values;
        QByteArray array;
        for (const FbxValue& v : record.values) {
            if (v.type == 'S') {
                QByteArray text = v.text;
                const int split = text.indexOf(QByteArray("\x00\x01", 2));
                if (split >= 0) text = text.mid(split + 2) + "::" + text.left(split);
                values << '"' + text + '"';
            } else if (v.type == 'L' || v.type == 'I') {
                values << QByteArray::number(qint64(v.number));
            } else if (v.type == 'D') {
                values << QByteArray::number(v.number, 'g', 17);
            } else {
                QByteArrayList items;
                for (double x : v.values) items << QByteArray::number(x, 'g', 17);
                values << "*" + QByteArray::number(v.values.size());
                array = items.join(',');
            }
        }
        out += values.join(", ");
        if (!array.isEmpty()) {
            out += " {\n" + indent + "\ta: " + array + "\n" + indent + "}\n";
        } else if (!record.children.isEmpty()) {
            out += " {\n" + fbxAscii(record.children, depth + 1) + indent + "}\n";
        } else {
            out += "\n";
        }
    }
    return out;
}

// A glTF file for the model animation tests: "Arm" holds "Hand", a 40-unit
// quad; its animation "Slide" moves the arm 50 along x in a second, "Lift"
// 10 up y.
QString writeAnimatedArm(const QString& directory)
{
    QByteArray buffer;
    {
        QDataStream out(&buffer, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for (float v : {-20.f, -20.f, 0.f, 20.f, -20.f, 0.f, 20.f, 20.f, 0.f, -20.f, 20.f, 0.f}) out << v;  // 0, 48
        for (quint16 i : {0, 1, 2, 0, 2, 3}) out << i;                                                   // 48, 12
        for (float v : {0.f, 1.f}) out << v;                                                               // 60, 8
        for (float v : {0.f, 0.f, 0.f, 50.f, 0.f, 0.f}) out << v;                                          // 68, 24
        for (float v : {0.f, 0.f, 0.f, 0.f, 10.f, 0.f}) out << v;                                          // 92, 24
    }
    const auto view = [](int offset, int length) {
        return QJsonObject{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
    };
    const auto accessor = [](int view, int type, int count, const char* kind) {
        return QJsonObject{{"bufferView", view}, {"componentType", type}, {"count", count}, {"type", kind}};
    };
    const auto animation = [](const char* name, int output) {
        return QJsonObject{{"name", name},
                           {"samplers", QJsonArray{QJsonObject{{"input", 2}, {"output", output}}}},
                           {"channels", QJsonArray{QJsonObject{{"sampler", 0},
                               {"target", QJsonObject{{"node", 0}, {"path", "translation"}}}}}}};
    };
    const QJsonObject gltf{
        {"asset", QJsonObject{{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}},
        {"nodes", QJsonArray{QJsonObject{{"name", "Arm"}, {"children", QJsonArray{1}}},
                             QJsonObject{{"name", "Hand"}, {"mesh", 0}}}},
        {"meshes", QJsonArray{QJsonObject{{"primitives", QJsonArray{QJsonObject{
            {"attributes", QJsonObject{{"POSITION", 0}}}, {"indices", 1}}}}}}},
        {"buffers", QJsonArray{QJsonObject{{"byteLength", int(buffer.size())},
            {"uri", QString("data:application/octet-stream;base64," + buffer.toBase64())}}}},
        {"bufferViews", QJsonArray{view(0, 48), view(48, 12), view(60, 8), view(68, 24), view(92, 24)}},
        {"accessors", QJsonArray{accessor(0, 5126, 4, "VEC3"), accessor(1, 5123, 6, "SCALAR"),
                                 accessor(2, 5126, 2, "SCALAR"), accessor(3, 5126, 2, "VEC3"),
                                 accessor(4, 5126, 2, "VEC3")}},
        {"animations", QJsonArray{animation("Slide", 3), animation("Lift", 4)}},
    };
    const QString file = QDir(directory).filePath("arm.gltf");
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly)) return QString();
    out.write(QJsonDocument(gltf).toJson(QJsonDocument::Compact));
    return file;
}

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
        // Watched for a while rather than looked for once: a prompt may open
        // a few turns of the event loop later (the Controls panel's 3D switch
        // runs after its combo box's signal).
        // One answer pending at a time: a new one replaces one whose prompt
        // never came (a prompt silenced in Options), so it cannot click the
        // next prompt with the wrong button.
        static QPointer<QTimer> pending;
        if (pending) { pending->stop(); pending->deleteLater(); }
        auto* watch = new QTimer(qApp);
        pending = watch;
        auto* clock = new QElapsedTimer; clock->start();
        QObject::connect(watch, &QObject::destroyed, [clock] { delete clock; });
        QObject::connect(watch, &QTimer::timeout, watch, [watch, clock, key, button, shown] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box && box->objectName() == QStringLiteral("prompt") + key) {
                if (shown) ++*shown;
                box->button(button)->click();
            } else if (clock->elapsed() < 5000) {
                return;
            }
            watch->stop(); watch->deleteLater();
        });
        watch->start(0);
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
    // A model's objects are its nodes (the reference's Model3DGroup and
    // Model3DTransformNode): each with its pivot, a row under the layer's
    // Models branch with a Transform of its own, keyed like the layer's; a
    // node moved is drawn there; the Viewer picks the node under a click,
    // outlines the selected one and drags it; and a project keeps it all.
    void modelNodesAreSelectedMovedAndSaved() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString file = temp.filePath("pair.obj");
        {
            QFile obj(file); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&obj);
            const int quads[6][4] = {{1, 2, 4, 3}, {5, 7, 8, 6}, {1, 5, 6, 2}, {3, 4, 8, 7}, {1, 3, 7, 5}, {2, 6, 8, 4}};
            int base = 0;
            for (const auto& [name, centre] : {std::pair<QString, int>{"Left", -100}, {"Right", 100}}) {
                out << "o " << name << '\n';
                for (int i = 0; i < 8; ++i)
                    out << "v " << centre + (i & 1 ? 30 : -30) << ' ' << (i & 2 ? 30 : -30) << ' ' << (i & 4 ? 30 : -30) << '\n';
                for (const auto& q : quads)
                    out << "f " << base + q[0] << ' ' << base + q[1] << ' ' << base + q[2]
                        << "\nf " << base + q[0] << ' ' << base + q[2] << ' ' << base + q[3] << '\n';
                base += 8;
            }
        }
        auto media = std::make_shared<media::MediaManager>();
        // Half as big again, centred, so the pair fits a 480x270 frame.
        model3d::ImportSettings sized;
        sized.autoNormalize = false; sized.singleUnitScale = 1.5;
        QVERIFY(media->importModel(file, sized).isSuccess());
        const core::Identifier modelId = media->assetByFilePath(QFileInfo(file).absoluteFilePath()).id();
        const model3d::Mesh mesh = media->modelMesh(modelId);
        QCOMPARE(mesh.nodes.size(), 2);
        QCOMPARE(mesh.nodePath(1), QString("Right"));
        QCOMPARE(mesh.nodeByPath("Left"), 0);
        for (const auto& tri : mesh.triangles) QVERIFY(tri.node == 0 || tri.node == 1);
        // Pivots: each cube's centre, after the import's centring and sizing.
        QVERIFY(mesh.nodes[0].pivot.x() < -100 && std::abs(mesh.nodes[0].pivot.y()) < 1e-3);
        QVERIFY(std::abs(mesh.nodes[0].pivot.x() + mesh.nodes[1].pivot.x()) < 1e-3);
        // Posed: the right cube alone goes up, clear of where it was.
        float half = 0.0f;
        for (const QVector3D& p : mesh.positions) half = qMax(half, std::abs(p.y()));
        QVERIFY(std::abs(half - 45.0f) < 1e-3f);
        QMatrix4x4 up; up.translate(0, 3 * half, 0);
        const model3d::Mesh posed = mesh.posed({QMatrix4x4(), up});
        for (const auto& tri : posed.triangles)
            for (int c = 0; c < 3; ++c) {
                const float y = posed.positions[tri.position[c]].y();
                QVERIFY(tri.node == 1 ? y > half : std::abs(y) <= half + 1e-3f);
            }

        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(480, 270); shot->setDurationSeconds(2);
        {
            auto& layer = shot->addLayer("Pair");
            layer.kind = composition::LayerKind::Model3D;
            layer.dimension = composition::LayerDimension::ThreeD;
            layer.modelAssetId = modelId;
            layer.transform.rotationYDegrees = 20;
        }
        const auto screenOf = [&](const QVector3D& point) {
            const QSize canvas = shot->displaySize();
            const QMatrix4x4 m = render::sceneCameraOf(shot.get(), canvas, 0).viewProjection(canvas)
                                 * render::layerModelMatrixAt(shot->layers()[0].transform, 0);
            const QVector4D c = m * QVector4D(point, 1.0f);
            return QPointF((c.x() / c.w() * 0.5 + 0.5) * canvas.width(),
                           (1.0 - (c.y() / c.w() * 0.5 + 0.5)) * canvas.height());
        };
        const QPointF left = screenOf(mesh.nodes[0].pivot), right = screenOf(mesh.nodes[1].pivot);

        QString selected, lastPick; int picks = 0, finished = 0;
        QVector<composition::Layer> before;
        ui::ModelNodeOverlay overlay(
            [&] {
                ui::ModelNodeOverlay::Target target;
                target.composition = shot.get(); target.layer = &shot->layerRef(0); target.node = selected;
                return target;
            },
            media, [&](const QString& path) { ++picks; lastPick = path; selected = path; },
            [&] { before = shot->layers(); }, [] {}, [&] { ++finished; });
        QCOMPARE(overlay.nodeAt(left), QString("Left"));
        QCOMPARE(overlay.nodeAt(right), QString("Right"));
        QVERIFY(overlay.nodeAt(QPointF(4, 4)).isNull());
        QVERIFY(overlay.outline().isEmpty());

        // A 480x270 frame at 100% beside the 24 px tool strip.
        ui::ViewerWidget viewer;
        viewer.resize(504, 270); viewer.show(); QTest::qWait(20);
        viewer.setProjectSize(QSize(480, 270));
        viewer.setFrame(QImage(480, 270, QImage::Format_ARGB32_Premultiplied));
        viewer.addOverlay(&overlay);
        const ui::ViewerMapping mapping = viewer.mapping();
        QSignalSpy layerDrags(&viewer, &ui::ViewerWidget::layerMoved);
        // A click on the left cube picks it; the press is still the tool's.
        QTest::mouseClick(&viewer, Qt::LeftButton, Qt::NoModifier, mapping.toWidget(left).toPoint());
        QCOMPARE(picks, 1); QCOMPARE(lastPick, QString("Left"));
        const QVector<QLineF> outline = overlay.outline();
        QCOMPARE(outline.size(), 18);   // a cube's 12 edges and 6 diagonals
        for (const QLineF& line : outline) QVERIFY(line.p1().x() < 240 && line.p2().x() < 240);
        QVERIFY(viewer.grab().save(testArtifactPath(QStringLiteral("model-node-outline.png"))));

        // Dragged from itself, the selected node moves with the pointer.
        const QPointF to = left + QPointF(70, -50);
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, mapping.toWidget(left).toPoint());
        QTest::mouseMove(&viewer, mapping.toWidget(to).toPoint());
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, mapping.toWidget(to).toPoint());
        QCOMPARE(finished, 1); QCOMPARE(layerDrags.count(), 0); QCOMPARE(picks, 1);
        const composition::ModelNodeTransform* moved = shot->layers()[0].modelNode("Left");
        QVERIFY(moved);
        const QVector3D position(float(moved->transform.position.x()), float(moved->transform.position.y()),
                                 float(moved->transform.positionZ));
        QVERIFY2(QLineF(screenOf(position), to).length() < 1.5,
                 qPrintable(QStringLiteral("%1,%2").arg(screenOf(position).x()).arg(screenOf(position).y())));
        QCOMPARE(QVector3D(float(moved->transform.anchorPoint.x()), float(moved->transform.anchorPoint.y()),
                           float(moved->transform.anchorPointZ)), mesh.nodes[0].pivot);
        QCOMPARE(overlay.nodeAt(to), QString("Left"));
        QVERIFY(composition::layerState(before) != composition::layerState(shot->layers()));
        QVERIFY(shot->layers()[0].modelNode("Right") == nullptr);
        viewer.removeOverlay(&overlay);

        // The renderer draws the node where it was moved to.
        {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0, QSize(480, 270));
            QVERIFY(QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000));
            const QByteArray bytes = frames.last()[1].toByteArray();
            const QImage frame(reinterpret_cast<const uchar*>(bytes.constData()), 480, 270, 480 * 4,
                               QImage::Format_RGBA8888);
            const auto bright = [&](const QPointF& at) { return qGray(frame.pixel(at.toPoint())); };
            QVERIFY2(bright(to) > 60 && bright(left) < 40 && bright(right) > 60,
                     qPrintable(QStringLiteral("%1 %2 %3").arg(bright(to)).arg(bright(left)).arg(bright(right))));
            frame.save(testArtifactPath(QStringLiteral("model-node-moved.png")));
        }

        // The timeline: a row per node under Models, a Transform under each.
        panel->setMediaManager(media); panel->setComposition(shot); QTest::qWait(20);
        const auto rowOf = [&](const QString& path, int property) -> QTreeWidgetItem* {
            for (QTreeWidgetItemIterator it(tree()); *it; ++it) {
                QTreeWidgetItem* row = *it;
                if (row->data(0, ui::kModelNodeRole).toString() != path) continue;
                const QVariant prop = row->data(0, Qt::UserRole + 4);
                if (property < 0 ? (!prop.isValid() && row->parent()->data(0, ui::kModelNodeRole).toString() != path)
                                 : (prop.isValid() && prop.toInt() == property))
                    return row;
            }
            return nullptr;
        };
        QTreeWidgetItem* rightRow = rowOf("Right", -1);
        QVERIFY(rightRow); QCOMPARE(rightRow->text(0), QString("Right"));
        QSignalSpy nodes(panel.get(), &ui::TimelineWidget::modelNodeSelected);
        tree()->setCurrentItem(rightRow);
        QVERIFY(!nodes.isEmpty()); QCOMPARE(nodes.last()[1].toString(), QString("Right"));
        panel->selectModelNode(0, "Left");
        QCOMPARE(tree()->currentItem(), rowOf("Left", -1));
        // Editing the right node's Position Y makes its transform, at its pivot.
        const int positionProp = int(composition::TransformProperty::Position);
        auto* editorRoot = panel->findChild<QWidget*>(QStringLiteral("timelineModelNode_0_Right_%1_1").arg(positionProp));
        QVERIFY(editorRoot);
        const int steps = history.count();
        editorRoot->findChild<QDoubleSpinBox*>()->setValue(77);
        const composition::ModelNodeTransform* edited = shot->layers()[0].modelNode("Right");
        QVERIFY(edited);
        QCOMPARE(edited->transform.position.y(), 77.0);
        QVERIFY(std::abs(edited->transform.position.x() - mesh.nodes[1].pivot.x()) < 1e-4);
        QCOMPARE(history.count(), steps + 1);
        // Keyed on its Position row.
        tree()->setCurrentItem(rowOf("Right", positionProp));
        panel->toggleSelectedKeyFrame();
        QVERIFY(!shot->layers()[0].modelNode("Right")->transform.positionXCurve.isEmpty());
        history.undo(); history.undo();
        QVERIFY(shot->layers()[0].modelNode("Right") == nullptr);
        history.redo(); history.redo();
        QVERIFY(shot->layers()[0].modelNode("Right")->transform.positionYCurve.contains(0));

        // Saved and opened again.
        const QString path = temp.filePath("nodes.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, *media).isSuccess());
        composition::Composition loaded; media::MediaManager loadedMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &loadedMedia).isSuccess());
        QCOMPARE(loaded.layers().size(), 1);
        const auto& nodesLoaded = loaded.layers()[0].modelNodes;
        QCOMPARE(nodesLoaded.size(), 2);
        const composition::ModelNodeTransform* leftLoaded = loaded.layers()[0].modelNode("Left");
        const composition::ModelNodeTransform* rightLoaded = loaded.layers()[0].modelNode("Right");
        QVERIFY(leftLoaded && rightLoaded);
        QVERIFY(std::abs(leftLoaded->transform.position.x() - moved->transform.position.x()) < 1e-3);
        QVERIFY(std::abs(leftLoaded->transform.positionZ - moved->transform.positionZ) < 1e-3);
        QVERIFY(std::abs(leftLoaded->transform.anchorPoint.x() - mesh.nodes[0].pivot.x()) < 1e-3);
        QVERIFY(rightLoaded->transform.positionYCurve.contains(0));
        QCOMPARE(rightLoaded->transform.positionAt(0).y(), 77.0);
        QCOMPARE(loadedMedia.modelMesh(loaded.layers()[0].modelAssetId).nodes.size(), 2);
    }
    // A model layer opened again is imported as it was, not with the
    // defaults: its node values are in the units that import gave it.
    void modelImportSettingsSurviveSave() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString file = temp.filePath("tri.obj");
        {
            QFile obj(file); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&obj);
            out << "o Piece\nv 0 0 0\nv 10 0 0\nv 0 20 5\nf 1 2 3\n";
        }
        model3d::ImportSettings settings;
        settings.autoNormalize = false; settings.centerAnchorPoint = false; settings.flipYZAxis = true;
        settings.unit = model3d::ImportSettings::Unit::Centimeter; settings.singleUnitScale = 200.0;
        settings.normalMethod = model3d::ImportSettings::NormalMethod::GenerateFaceted;
        settings.autoSmoothingAngle = 33.0; settings.flipUVCoordinates = true;
        media::MediaManager media;
        QVERIFY(media.importModel(file, settings).isSuccess());
        composition::Composition shot; shot.setSize(320, 180);
        auto& layer = shot.addLayer("Piece");
        layer.kind = composition::LayerKind::Model3D; layer.dimension = composition::LayerDimension::ThreeD;
        layer.modelAssetId = media.assetByFilePath(QFileInfo(file).absoluteFilePath()).id();
        const QVector3D pivot = media.modelMesh(layer.modelAssetId).nodes.value(0).pivot;
        QVERIFY(!pivot.isNull());
        const QString path = temp.filePath("import.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, shot, media).isSuccess());
        composition::Composition loaded; media::MediaManager loadedMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &loadedMedia).isSuccess());
        const core::Identifier id = loaded.layers().value(0).modelAssetId;
        const model3d::ImportSettings back = loadedMedia.modelSettings(id);
        QCOMPARE(back.autoNormalize, false); QCOMPARE(back.centerAnchorPoint, false);
        QCOMPARE(back.flipYZAxis, true); QCOMPARE(back.unit, model3d::ImportSettings::Unit::Centimeter);
        QCOMPARE(back.singleUnitScale, 200.0); QCOMPARE(back.autoSmoothingAngle, 33.0);
        QCOMPARE(back.normalMethod, model3d::ImportSettings::NormalMethod::GenerateFaceted);
        QCOMPARE(back.flipUVCoordinates, true);
         QVERIFY((loadedMedia.modelMesh(id).nodes.value(0).pivot - pivot).length() < 1e-4);
        // Written by the reference's codes: LinearType and NormalGenerationType.
        QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
        QString xml = QString::fromUtf8(saved.readAll());
        saved.close();
        QVERIFY(xml.contains("<ImportModelUnits>3002</ImportModelUnits>"));
        QVERIFY(xml.contains("<ImportNormalGen>2</ImportNormalGen>"));
        // A project from before them names the unit and method instead.
        xml.replace("<ImportModelUnits>3002</ImportModelUnits>", "<OpenVegasUnit>Inch</OpenVegasUnit>");
        xml.replace("<ImportNormalGen>2</ImportNormalGen>",
                    "<OpenVegasNormalMethod>FromFileAutoSmoothing</OpenVegasNormalMethod>");
        const QString legacy = temp.filePath("legacy.vegfx");
        {
            QFile out(legacy); QVERIFY(out.open(QIODevice::WriteOnly)); out.write(xml.toUtf8());
        }
        composition::Composition old; media::MediaManager oldMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(legacy, &old, &oldMedia).isSuccess());
        const model3d::ImportSettings named = oldMedia.modelSettings(old.layers().value(0).modelAssetId);
        QCOMPARE(named.unit, model3d::ImportSettings::Unit::Inch);
        QCOMPARE(named.normalMethod, model3d::ImportSettings::NormalMethod::FromFileAutoSmoothing);
     }
    // The reference's Model3DImportOptions and Model3DPostProcessor: units
    // in millimetres over its 2.8352 a pixel, Flip YZ as (-x, z, y), Auto
    // Normalize to half a diagonal of 500 pixels (Model Scale cancelling),
    // centring about the box, and smooth normals up to the angle (80) for a
    // file without them.
    void modelImportFollowsTheReference() {
        using Settings = model3d::ImportSettings;
        const Settings defaults;
        QCOMPARE(defaults.unit, Settings::Unit::Pixel);
        QCOMPARE(defaults.normalMethod, Settings::NormalMethod::FromFile);
        QCOMPARE(defaults.autoSmoothingAngle, 80.0);
        QVERIFY(defaults.autoNormalize && defaults.centerAnchorPoint && !defaults.flipYZAxis);
        QCOMPARE(model3d::unitNames().size(), 10);
        QCOMPARE(model3d::unitNames().first(), QString("Millimeter"));
        QCOMPARE(model3d::unitNames().last(), QString("Pixel"));
        QCOMPARE(model3d::normalMethodNames().value(1), QString("Generate Faceted + Auto Smoothing"));
        QCOMPARE(model3d::unitCode(Settings::Unit::Millimeter), 3001);
        QCOMPARE(model3d::unitCode(Settings::Unit::Pixel), 3010);
        Settings::Unit unit = Settings::Unit::Pixel;
        QVERIFY(model3d::unitFromCode(3007, &unit) && unit == Settings::Unit::Foot);
        QVERIFY(!model3d::unitFromCode(3011, &unit));

        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString file = temp.filePath("box.obj");
        {
            QFile obj(file); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream(&obj) << "v 0 0 0\nv 20 0 0\nv 20 40 60\nv 0 40 60\nf 1 2 3\nf 1 3 4\n";
        }
        const auto close = [](const QVector3D& a, const QVector3D& b, float tolerance = 1e-2f) {
            return (a - b).length() < tolerance;
        };
        const auto load = [&](const Settings& settings) {
            const auto loaded = model3d::loadModel(file, settings);
            return loaded.mesh;
        };
        const auto box = [](const model3d::Mesh& mesh, QVector3D* centre, float* half) {
            QVector3D lo, hi;
            mesh.bounds(&lo, &hi);
            *centre = (lo + hi) * 0.5f;
            *half = ((hi - lo) * 0.5f).length();
        };
        Settings raw;
        raw.autoNormalize = false; raw.centerAnchorPoint = false;
        QVERIFY(close(load(raw).positions[2], QVector3D(20, 40, 60)));
        // Materials as Assimp's OBJ importer gives them to the reference:
        // faces without one on "DefaultMaterial" (diffuse 0.6, no specular,
        // ambient or emission, shininess 1); a material starts the same,
        // and its Ns is the shininess.
        const model3d::Mesh bare = load(raw);
        QCOMPARE(bare.materials.size(), 1);
        QCOMPARE(bare.materials[0].name, QString("DefaultMaterial"));
        QCOMPARE(bare.materials[0].diffuseColor, QColor::fromRgbF(0.6f, 0.6f, 0.6f));
        QCOMPARE(bare.materials[0].specularColor, QColor(0, 0, 0));
        QCOMPARE(bare.materials[0].ambientColor, QColor(0, 0, 0));
        QCOMPARE(bare.materials[0].shininess, 1.0);
        QCOMPARE(bare.triangles[0].material, 0);
        {
            QFile mtl(temp.filePath("box.mtl")); QVERIFY(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream(&mtl) << "newmtl Plain\nKd 1 0 0\nnewmtl Glossy\nKs 1 1 1\nNs 50\n";
        }
        const QString dressed = temp.filePath("dressed.obj");
        {
            QFile obj(dressed); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream(&obj) << "mtllib box.mtl\nv 0 0 0\nv 20 0 0\nv 20 40 60\nv 0 40 60\n"
                                 "usemtl Plain\nf 1 2 3\nusemtl Glossy\nf 1 3 4\n";
        }
        const auto withMaterials = model3d::loadModel(dressed, raw);
        QVERIFY(withMaterials.ok);
        QCOMPARE(withMaterials.mesh.materials.size(), 2);
        const model3d::Material& plain = withMaterials.mesh.materials[0];
        QCOMPARE(plain.diffuseColor, QColor(255, 0, 0));
        QCOMPARE(plain.specularColor, QColor(0, 0, 0));
        QCOMPARE(plain.shininess, 1.0);
        const model3d::Material& glossy = withMaterials.mesh.materials[1];
        QCOMPARE(glossy.diffuseColor, QColor::fromRgbF(0.6f, 0.6f, 0.6f));
        QCOMPARE(glossy.specularColor, QColor(255, 255, 255));
        QCOMPARE(glossy.shininess, 50.0);
        // A metre is 1000 / 2.8352 pixels; Model Scale multiplies.
        Settings metres = raw; metres.unit = Settings::Unit::Meter; metres.singleUnitScale = 0.5;
        QVERIFY(close(load(metres).positions[2], QVector3D(20, 40, 60) * float(500.0 / 2.835200071334839), 0.5f));
        Settings inches = raw; inches.unit = Settings::Unit::Inch;
        QVERIFY(close(load(inches).positions[1], QVector3D(20 * 25.4f / 2.8352f, 0, 0), 0.05f));
        // Flip YZ Axis: (x, y, z) -> (-x, z, y).
        Settings flipped = raw; flipped.flipYZAxis = true;
        const model3d::Mesh turned = load(flipped);
        QVERIFY(close(turned.positions[2], QVector3D(-20, 60, 40)));
        QVERIFY(close(turned.importTransform.map(QVector3D(20, 40, 60)), QVector3D(-20, 60, 40)));
        // Centred without Auto Normalize: about the box's centre, scaled.
        Settings centred = raw; centred.centerAnchorPoint = true; centred.singleUnitScale = 2.0;
        QVERIFY(close(load(centred).positions[0], QVector3D(-20, -40, -60)));
        // Auto Normalize: half the diagonal is 500, whatever the scale and unit.
        QVector3D centre; float half = 0.0f;
        box(load(Settings()), &centre, &half);
        QVERIFY2(close(centre, QVector3D()) && std::abs(half - 500.0f) < 1e-2f,
                 qPrintable(QString("%1 %2").arg(half).arg(centre.length())));
        Settings bigger; bigger.unit = Settings::Unit::Meter; bigger.singleUnitScale = 3.0;
        const model3d::Mesh normalised = load(bigger);
        box(normalised, &centre, &half);
        QVERIFY(close(centre, QVector3D()) && std::abs(half - 500.0f) < 1e-2f);
        // Not centred, it grows about its own centre.
        Settings kept; kept.centerAnchorPoint = false;
        const model3d::Mesh grown = load(kept);
        box(grown, &centre, &half);
        QVERIFY(close(centre, QVector3D(10, 20, 30)) && std::abs(half - 500.0f) < 1e-2f);
        QVERIFY(close(grown.importTransform.map(QVector3D(20, 40, 60)), grown.positions[2]));

        // Normals: two faces 60 degrees apart share an edge. From the file,
        // which has none, they are smoothed up to 80 degrees; faceted, or
        // with a smaller angle, they stay apart.
        const QString roof = temp.filePath("roof.obj");
        {
            QFile obj(roof); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream(&obj) << "v 0 0 0\nv 10 0 0\nv 5 10 0\nv 5 -5 8.660254\nf 1 2 3\nf 2 1 4\n";
        }
        const auto shared = [&](const Settings& settings) {
            const auto loaded = model3d::loadModel(roof, settings);
            const model3d::Mesh& mesh = loaded.mesh;
            QVector<QVector3D> found;
            for (const auto& tri : mesh.triangles)
                for (int c = 0; c < 3; ++c)
                    if (tri.position[c] == 0) found.append(mesh.normals.value(tri.normal[c]));
            return found;
        };
        QVector<QVector3D> normals = shared(raw);
        QCOMPARE(normals.size(), 2);
        QVERIFY(close(normals[0], normals[1], 1e-3f));
        Settings faceted = raw; faceted.normalMethod = Settings::NormalMethod::GenerateFaceted;
        normals = shared(faceted);
        QVERIFY(!close(normals[0], normals[1], 0.1f));
        Settings narrow = raw; narrow.autoSmoothingAngle = 30.0;
        normals = shared(narrow);
        QVERIFY(!close(normals[0], normals[1], 0.1f));
    }
    void modelAssetSupportsIndependentInstances() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString file = temp.filePath("instance.obj");
        QFile obj(file); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
        obj.write("o Quad\nv -20 -20 0\nv 20 -20 0\nv 20 20 0\nv -20 20 0\nf 1 2 3\nf 1 3 4\n");
        obj.close();
        auto media = std::make_shared<media::MediaManager>();
        model3d::ImportSettings settings; settings.autoNormalize = false; settings.centerAnchorPoint = false;
        QVERIFY(media->importModel(file, settings).isSuccess());
        const core::Identifier asset = media->assetByFilePath(QFileInfo(file).absoluteFilePath()).id();
        QVERIFY(asset.isValid()); QVERIFY(media->hasModel(asset));
        composition::Composition shot; shot.setSize(320, 180); shot.setDurationSeconds(1.0);
        // Each layer is set up before the next is added: adding one may move
        // the others, so a reference to an earlier layer would not survive.
        const auto addInstance = [&](const QString& name, double x, double nodeX) {
            auto& layer = shot.addLayer(name);
            layer.kind = composition::LayerKind::Model3D; layer.dimension = composition::LayerDimension::ThreeD;
            layer.modelAssetId = asset; layer.transform.position = QPointF(x, 0);
            composition::ModelNodeTransform node; node.path = "Quad";
            node.transform = composition::restingModelNodeTransform(nodeX, 0, 0); layer.modelNodes.append(node);
        };
        addInstance("Left instance", -70, -20);
        addInstance("Right instance", 70, 20);
        QCOMPARE(shot.layers()[0].modelAssetId, shot.layers()[1].modelAssetId);
        QVERIFY(shot.layers()[0].id != shot.layers()[1].id);
        const QString path = temp.filePath("instances.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, shot, *media).isSuccess());
        composition::Composition loaded; media::MediaManager loadedMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &loadedMedia).isSuccess());
        QCOMPARE(loaded.layers().size(), 2);
        QCOMPARE(loaded.layers()[0].modelAssetId, loaded.layers()[1].modelAssetId);
        QCOMPARE(loaded.layers()[0].transform.position, QPointF(-70, 0));
        QCOMPARE(loaded.layers()[1].transform.position, QPointF(70, 0));
        QCOMPARE(loaded.layers()[0].modelNodes[0].transform.position.x(), -20.0);
        QCOMPARE(loaded.layers()[1].modelNodes[0].transform.position.x(), 20.0);
    }
    // An export with a preset is what the preset says: an MP4 at its size,
    // frame rate and profile, ProRes 4444 with alpha, a JPEG sequence of its
    // size with its prefix, and audio-only WAV and AAC, which render no
    // frames.
    void exportJobFollowsPresets() {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
        if (ffmpeg.isEmpty() || ffprobe.isEmpty()) QSKIP("FFmpeg is not available");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString tone = dir.filePath("tone.wav");
        {
            QProcess make;
            make.start(ffmpeg, {"-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=1:sample_rate=48000",
                                "-ac", "2", tone});
            QVERIFY(make.waitForFinished(30000));
            QCOMPARE(make.exitCode(), 0);
        }
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importFile(tone).isSuccess());
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(10, 1); shot->setDurationSeconds(1);
        {
            auto& plane = shot->addLayer("Plane"); plane.kind = composition::LayerKind::Plane;
            plane.planeColor = QColor(200, 40, 40); plane.transform.scalePercent = QPointF(50, 50);
            composition::Clip fill; fill.durationSeconds = 1; plane.clips << fill;
            auto& sound = shot->addLayer("Tone");
            composition::Clip clip; clip.mediaId = media->assetByFilePath(QFileInfo(tone).absoluteFilePath()).id();
            clip.durationSeconds = 1; sound.clips << clip;
        }
        const auto run = [&](const render::ExportPreset& preset, const QString& name, QString* message = nullptr) {
            render::ExportJob::Request request;
            request.composition = shot; request.media = media;
            request.outputPath = dir.filePath(name + "." + preset.extension());
            request.lastFrame = 9; request.hardwareEncoding = false; request.preset = preset;
            render::ExportJob job(request);
            QSignalSpy finished(&job, &render::ExportJob::finished);
            QSignalSpy progress(&job, &render::ExportJob::progress);
            job.start();
            if (!QTest::qWaitFor([&] { return finished.count() > 0; }, 60000)) return false;
            if (message) *message = finished[0][1].toString();
            return finished[0][0].toBool();
        };
        const auto probe = [&](const QString& path, const QString& stream) {
            QProcess process;
            process.start(ffprobe, {"-v", "error", "-select_streams", stream, "-show_entries",
                                    "stream=codec_name,width,height,r_frame_rate,profile,pix_fmt,sample_rate",
                                    "-of", "default=nw=1", path});
            process.waitForFinished(20000);
            return QString::fromUtf8(process.readAllStandardOutput());
        };
        QString message;
        render::ExportPreset mp4;
        mp4.name = "Test MP4"; mp4.format = render::ExportFormat::MP4;
        mp4.widthUseSource = mp4.heightUseSource = false; mp4.width = 640; mp4.height = 360;
        mp4.frameRateUseSource = false; mp4.frameRate = 20.0;
        mp4.h264Profile = render::ExportPreset::H264Profile::Main; mp4.bitRate = 2000000; mp4.maxBitRate = 3000000;
        mp4.audioBitRate = 128000;
        QVERIFY2(run(mp4, "movie", &message), qPrintable(message));
        QString video = probe(dir.filePath("movie.mp4"), "v:0");
        QVERIFY2(video.contains("codec_name=h264") && video.contains("width=640") && video.contains("height=360")
                     && video.contains("r_frame_rate=20/1") && video.contains("profile=Main"), qPrintable(video));
        QVERIFY2(probe(dir.filePath("movie.mp4"), "a:0").contains("codec_name=aac"), "no AAC audio");

        render::ExportPreset prores;
        prores.name = "Test ProRes"; prores.format = render::ExportFormat::MOV;
        prores.movCodec = render::ExportPreset::MovCodec::ProRes4444; prores.alpha = true;
        prores.audioCodec = render::ExportPreset::AudioCodec::Pcm;
        QVERIFY2(run(prores, "master", &message), qPrintable(message));
        video = probe(dir.filePath("master.mov"), "v:0");
        QVERIFY2(video.contains("codec_name=prores") && video.contains("profile=4444") && video.contains("pix_fmt=yuva444p")
                     && video.contains("width=320"), qPrintable(video));

        render::ExportPreset frames;
        frames.name = "Test JPG"; frames.format = render::ExportFormat::ImageSequence;
        frames.imageFormat = render::ExportPreset::ImageFormat::Jpg; frames.quality = 80; frames.prefix = "shot_";
        frames.widthUseSource = frames.heightUseSource = false; frames.width = 120; frames.height = 120;
        frames.scaleMode = render::ExportPreset::ScaleMode::KeepAspectRatioByExpanding;
        frames.exportAudio = false;
        QVERIFY2(run(frames, "frames", &message), qPrintable(message));
        const QImage first(dir.filePath("shot_00000001.jpg"));
        QCOMPARE(first.size(), QSize(120, 120));
        QVERIFY(QFileInfo::exists(dir.filePath("shot_00000010.jpg")));
        // Expanded: the plane fills the middle; the background shows at the sides.
        QVERIFY(first.pixelColor(60, 60).red() > 150);

        render::ExportPreset wav;
        wav.name = "Test WAV"; wav.format = render::ExportFormat::WAV; wav.exportVideo = false;
        wav.audioCodec = render::ExportPreset::AudioCodec::Pcm; wav.audioSampleRate = 96000;
        QVERIFY2(run(wav, "sound", &message), qPrintable(message));
        const QString audio = probe(dir.filePath("sound.wav"), "a:0");
        QVERIFY2(audio.contains("codec_name=pcm_s16le") && audio.contains("sample_rate=96000"), qPrintable(audio));
        QVERIFY(probe(dir.filePath("sound.wav"), "v:0").trimmed().isEmpty());
        render::ExportPreset aac;
        for (const render::ExportPreset& preset : render::builtInExportPresets())
            if (preset.name == "AAC Audio Only (MP4)") aac = preset;
        QVERIFY(aac.isValid());
        QVERIFY2(run(aac, "aac", &message), qPrintable(message));
        QVERIFY(probe(dir.filePath("aac.mp4"), "a:0").contains("codec_name=aac"));
        QVERIFY(probe(dir.filePath("aac.mp4"), "v:0").trimmed().isEmpty());
        // Without audio in the shot an audio-only preset says so.
        shot->removeLayer(1);
        QVERIFY(!run(wav, "nothing", &message));
        QCOMPARE(message, QString("The shot has no audio to export"));
    }
    // The media cache: a waveform's peaks, the decoded audio's summary, go to
    // the cache's files and are recorded in its database; a new cache reads
    // them back without decoding and marks them used; DaysToKeep removes the
    // record and the file; the size on disk is counted.
    void mediaCacheKeepsWaveformPeaks() {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty()) QSKIP("FFmpeg is needed to build waveforms from media");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString wav = dir.filePath("tone.wav");
        {
            QFile file(wav); QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
            constexpr int frames = 48000 / 2;
            stream.writeRawData("RIFF", 4); stream << quint32(36 + frames * 4);
            stream.writeRawData("WAVEfmt ", 8);
            stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
                   << quint32(192000) << quint16(4) << quint16(16);
            stream.writeRawData("data", 4); stream << quint32(frames * 4);
            for (int i = 0; i < frames; ++i) {
                const qint16 value = qint16(std::lround(12000 * std::sin(i * 0.05)));
                stream << value << value;
            }
        }
        cache::CacheDB database;
        QVERIFY(database.open(dir.filePath("cache.db")).isSuccess());
        QStringList stored, used;
        media::WaveformStore store;
        store.directory = dir.filePath("MediaCacheFiles/Peaks");
        store.stored = [&](const QString& key, const QString& file) {
            stored << key;
            cache::CacheEntry entry; entry.key = "peaks:" + key; entry.cacheFile = file;
            entry.createdUtcMs = entry.lastUsedUtcMs = 1000;
            QVERIFY(database.put(entry).isSuccess());
        };
        store.used = [&](const QString& key) { used << key; database.touch("peaks:" + key, 5000); };
        {
            // A cache made after the default store is set starts with it.
            media::WaveformCache::setDefaultStore(store);
            media::WaveformCache cache; cache.setDecoder(ffmpeg);
            QSignalSpy ready(&cache, &media::WaveformCache::peaksReady);
            QVERIFY(!cache.peaks(wav));
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
            QVERIFY(cache.peaks(wav));
        }
        QCOMPARE(stored.size(), 1);
        QCOMPARE(database.entryCount(), 1);
        cache::CacheEntry entry;
        QVERIFY(database.get("peaks:" + stored.first(), entry));
        QVERIFY(QFileInfo::exists(entry.cacheFile));
        QVERIFY(entry.cacheFile.startsWith(store.directory));
        QCOMPARE(database.totalBytes(), QFileInfo(entry.cacheFile).size());
        QVERIFY(database.totalBytes() > 0);
        // Opened again: read from the cache, no decoder at all.
        {
            media::WaveformCache again; again.setDecoder(QString());
            QVERIFY(again.peaks(wav));
        }
        QCOMPARE(used, QStringList{stored.first()});
        QVERIFY(database.get("peaks:" + stored.first(), entry));
        QCOMPARE(entry.lastUsedUtcMs, qint64(5000));
        // A changed file is another entry: the old one waits for its days.
        // DaysToKeep: unused since before 6000 - gone, with its file.
        QCOMPARE(database.pruneOlderThan(4000), qint64(0));
        QCOMPARE(database.pruneOlderThan(6000), qint64(1));
        QCOMPARE(database.entryCount(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(entry.cacheFile), 5000);
        media::WaveformCache::setDefaultStore({QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                                   .filePath(QStringLiteral("waveforms")), {}, {}});
        // Purging everything takes the files too.
        QFile extra(dir.filePath("extra.peaks")); QVERIFY(extra.open(QIODevice::WriteOnly)); extra.write("x"); extra.close();
        cache::CacheEntry other; other.key = "other"; other.cacheFile = extra.fileName();
        QVERIFY(database.put(other).isSuccess());
        QCOMPARE(database.purgeAll(), qint64(1));
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(extra.fileName()), 5000);
        database.close();
    }
    // Decoded frames that were slow to get go to the media cache: written in
    // the background, recorded with their size, read back by a later cache
    // without decoding, keyed by the file's identity, position and size.
    void mediaCacheKeepsSlowDecodedFrames() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString source = dir.filePath("clip.mov");
        {
            QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("first");
        }
        cache::CacheDB database;
        QVERIFY(database.open(dir.filePath("cache.db")).isSuccess());
        QStringList stored, used;
        media::DecodedFrameStore store;
        store.directory = dir.filePath("MediaCacheFiles/Frames");
        store.stored = [&](const QString& key, const QString& file, qint64 bytes) {
            stored << key;
            cache::CacheEntry entry; entry.key = "frame:" + key; entry.cacheFile = file;
            entry.createdUtcMs = entry.lastUsedUtcMs = 1000; entry.bytes = bytes;
            QVERIFY(database.put(entry).isSuccess());
        };
        store.used = [&](const QString& key) { used << key; database.touch("frame:" + key, 5000); };

        QImage picture(320, 180, QImage::Format_RGBA8888);
        for (int y = 0; y < picture.height(); ++y)
            for (int x = 0; x < picture.width(); ++x)
                picture.setPixelColor(x, y, QColor(x % 256, y, (x * y) % 256, 255 - y));
        const QSize size(320, 180);
        const QString key = media::DecodedFrameCache::keyFor(source, 1500, size);
        QVERIFY(!key.isEmpty());
        // Position, size and the file itself are in the key; no file, no key.
        QVERIFY(media::DecodedFrameCache::keyFor(source, 1501, size) != key);
        QVERIFY(media::DecodedFrameCache::keyFor(source, 1500, QSize(160, 90)) != key);
        QVERIFY(media::DecodedFrameCache::keyFor(dir.filePath("missing.mov"), 1500, size).isEmpty());

        // Kept only when reading back beats decoding again.
        QVERIFY(!media::DecodedFrameCache::worthKeeping(10, QSize(1920, 1080)));
        QVERIFY(media::DecodedFrameCache::worthKeeping(30, QSize(1920, 1080)));
        QVERIFY(!media::DecodedFrameCache::worthKeeping(40, QSize(3840, 2160)));
        QVERIFY(media::DecodedFrameCache::worthKeeping(80, QSize(3840, 2160)));
        QVERIFY(!media::DecodedFrameCache::worthKeeping(1000, QSize()));

        int decodes = 0;
        {
            media::DecodedFrameCache::setDefaultStore(store);
            media::DecodedFrameCache cache;
            QCOMPARE(cache.frameStore().directory, store.directory);
            // A quick decode is not kept.
            const QString quick = media::DecodedFrameCache::keyFor(source, 0, size);
            QCOMPARE(cache.fetch(quick, [&] { ++decodes; return picture; }), picture);
            // A slow one is.
            QCOMPARE(cache.fetch(key, [&] { ++decodes; QThread::msleep(40); return picture; }), picture);
            QCOMPARE(decodes, 2);
            cache.waitForWrites();
            QTRY_COMPARE(stored.size(), 1);
            QCOMPARE(stored.first(), key);
            QVERIFY(QFileInfo::exists(cache.pathFor(key)));
            QVERIFY(!QFileInfo::exists(cache.pathFor(quick)));
            QVERIFY(used.isEmpty());
        }
        cache::CacheEntry entry;
        QVERIFY(database.get("frame:" + key, entry));
        QVERIFY(entry.cacheFile.startsWith(store.directory));
        QCOMPARE(entry.bytes, QFileInfo(entry.cacheFile).size());
        QVERIFY(entry.bytes > 0 && entry.bytes < qint64(picture.sizeInBytes()));
        {
            // Another session: read back, no decoding, marked used.
            media::DecodedFrameCache again;
            const QImage back = again.fetch(key, [&] { ++decodes; return QImage(); });
            QCOMPARE(decodes, 2);
            QCOMPARE(back.format(), QImage::Format_RGBA8888);
            QCOMPARE(back, picture);
            QCOMPARE(used, QStringList{key});
            QVERIFY(database.get("frame:" + key, entry));
            QCOMPARE(entry.lastUsedUtcMs, qint64(5000));
            // A damaged file is a miss, not a picture.
            {
                QFile damaged(again.pathFor(key)); QVERIFY(damaged.open(QIODevice::ReadWrite));
                damaged.seek(damaged.size() / 2); damaged.write(QByteArray(64, '\x5a'));
            }
            QTest::ignoreMessage(QtWarningMsg, "qUncompress: Input data is corrupted");
            QVERIFY(again.load(key).isNull());
        }
        // A replaced file has frames of its own.
        {
            QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("second, longer");
        }
        QVERIFY(media::DecodedFrameCache::keyFor(source, 1500, size) != key);
        media::DecodedFrameCache::setDefaultStore({});
        // No folder, no cache: everything is decoded.
        {
            media::DecodedFrameCache none;
            QVERIFY(none.load(key).isNull());
            QCOMPARE(none.fetch(key, [&] { ++decodes; QThread::msleep(30); return picture; }), picture);
            QCOMPARE(decodes, 3);
        }

        // The size cap: the least recently used go first, with their files.
        cache::CacheDB sized;
        QVERIFY(sized.open(dir.filePath("sized.db")).isSuccess());
        // Two databases open at once each keep their own connection.
        QVERIFY(database.get("frame:" + key, entry));
        for (int i = 0; i < 3; ++i) {
            const QString file = dir.filePath(QStringLiteral("entry%1.bin").arg(i));
            QFile out(file); QVERIFY(out.open(QIODevice::WriteOnly)); out.write(QByteArray(100, 'x')); out.close();
            cache::CacheEntry item; item.key = QStringLiteral("item%1").arg(i); item.cacheFile = file;
            item.createdUtcMs = 1; item.lastUsedUtcMs = 10 + (i == 0 ? 50 : i); item.bytes = 100;
            QVERIFY(sized.put(item).isSuccess());
        }
        QCOMPARE(sized.pruneToBytes(300), qint64(0));
        QCOMPARE(sized.pruneToBytes(150), qint64(2));
        QCOMPARE(sized.entryCount(), 1);
        QVERIFY(sized.get("item0", entry));   // used last
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(dir.filePath("entry1.bin")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(dir.filePath("entry2.bin")), 5000);
        QVERIFY(QFileInfo::exists(dir.filePath("entry0.bin")));
        sized.close();

        // A database from before entries had sizes gets the column.
        {
            QSqlDatabase old = QSqlDatabase::addDatabase("QSQLITE", "old-media-cache");
            old.setDatabaseName(dir.filePath("old.db"));
            QVERIFY(old.open());
            QSqlQuery query(old);
            QVERIFY(query.exec("CREATE TABLE cache_entries (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                               " key TEXT NOT NULL UNIQUE, cache_file TEXT NOT NULL,"
                               " width INTEGER NOT NULL DEFAULT 0, height INTEGER NOT NULL DEFAULT 0,"
                               " created_utc_ms INTEGER NOT NULL, last_used_utc_ms INTEGER NOT NULL)"));
            QVERIFY(query.exec("INSERT INTO cache_entries (key, cache_file, created_utc_ms, last_used_utc_ms)"
                               " VALUES ('peaks:old', 'old.peaks', 1, 1)"));
            query.finish();
            old.close();
        }
        QSqlDatabase::removeDatabase("old-media-cache");
        cache::CacheDB migrated;
        QVERIFY(migrated.open(dir.filePath("old.db")).isSuccess());
        QVERIFY(migrated.get("peaks:old", entry));
        QCOMPARE(entry.bytes, qint64(0));
        cache::CacheEntry sizedEntry; sizedEntry.key = "frame:new"; sizedEntry.cacheFile = "new.ovframe";
        sizedEntry.createdUtcMs = sizedEntry.lastUsedUtcMs = 2; sizedEntry.bytes = 1234;
        QVERIFY(migrated.put(sizedEntry).isSuccess());
        QVERIFY(migrated.get("frame:new", entry));
        QCOMPARE(entry.bytes, qint64(1234));
        migrated.close();
        database.close();
    }
    // A camera's lens and clipping are saved as the reference's CameraLayer
    // keeps them: zoom (animated), focusDistance, aperture and blurDoF, and
    // its HasDoF, NearClip, FarClip and film size.
    void cameraFovProjectRoundTrip() {
        composition::Layer& camera = comp->layerRef(0);
        camera.kind = composition::LayerKind::Camera;
        camera.camera.zoom = 1234.5; camera.camera.zoomCurve.set(0, 1000.0); camera.camera.zoomCurve.set(10, 2000.0);
        camera.camera.focusDistance = 900; camera.camera.aperture = 20; camera.camera.blur = 40;
        camera.camera.depthOfField = true; camera.camera.nearClip = 5; camera.camera.farClip = 5000;
        QTemporaryDir temp; media::MediaManager assets; composition::Composition loaded;
        const QString path=temp.filePath("camera.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path,*comp,assets).isSuccess());
        {
            QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
            const QString xml = QString::fromUtf8(saved.readAll());
            QVERIFY(xml.contains("<Name>zoom</Name>") && xml.contains("<Name>blurDoF</Name>"));
            QVERIFY(xml.contains("<NearClip>5</NearClip>") && xml.contains("<FarClip>5000</FarClip>"));
        }
        QVERIFY(project::VegfxSerializer::loadFromFile(path,&loaded,&assets).isSuccess());
        const composition::CameraSettings& back = loaded.layers()[0].camera;
        QCOMPARE(back.zoomAt(10), 2000.0);
        QCOMPARE(back.focusDistance, 900.0); QCOMPARE(back.aperture, 20.0); QCOMPARE(back.blur, 40.0);
        QVERIFY(back.depthOfField); QCOMPARE(back.nearClip, 5.0); QCOMPARE(back.farClip, 5000.0);
        // The vertical angle follows from the zoom: 2 atan(height / 2 / zoom).
        const QSize canvas = loaded.displaySize();
        loaded.layerRef(0).visible = true;
        const double fov = render::sceneCameraOf(&loaded, canvas, 10).fieldOfViewDegrees;
        QVERIFY(std::abs(fov - qRadiansToDegrees(2 * std::atan(canvas.height() / 2.0 / 2000.0))) < 1e-6);
        QCOMPARE(render::sceneCameraOf(&loaded, canvas, 10).nearPlane, 5.0);
    }
    // A camera's Lens group in the timeline edits its zoom (by key when it
    // is animated) and the Layer panel its Near and Far Clip Distance.
    void cameraLensAndClippingAreEdited() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setDurationSeconds(1);
        shot->addLayer("Camera") = ui::newCameraLayer(*shot);
        QUndoStack stack;
        ui::TimelineWidget timeline; timeline.resize(900, 500);
        timeline.setUndoStack(&stack); timeline.setComposition(shot);
        auto* zoom = timeline.findChild<QWidget*>("timelineCamera_0_zoom")->findChild<QDoubleSpinBox*>();
        QVERIFY(zoom);
        QVERIFY(std::abs(zoom->value() - shot->layers()[0].camera.zoom) < 0.05);
        zoom->setValue(1500);
        QCOMPARE(shot->layers()[0].camera.zoom, 1500.0);
        QVERIFY(timeline.findChild<QWidget*>("timelineCamera_0_focusDistance"));
        QVERIFY(timeline.findChild<QWidget*>("timelineCamera_0_blur"));
        auto* dof = timeline.findChild<QWidget*>("timelineCamera_0_depthOfField")->findChild<QCheckBox*>();
        QVERIFY(dof && !dof->isChecked());
        dof->setChecked(true);
        QVERIFY(shot->layers()[0].camera.depthOfField);
        QCOMPARE(stack.undoText(), QString("Set Camera Layer Depth of Field"));
        stack.undo(); QVERIFY(!shot->layers()[0].camera.depthOfField);
        QCOMPARE(stack.count(), 2);
        stack.setIndex(1);
        ui::LayerPanel panel; panel.setUndoStack(&stack); panel.bindModel(shot); panel.setSelection(0);
        auto* nearClip = panel.findChild<QDoubleSpinBox*>("layerCameraNearClip"); QVERIFY(nearClip);
        auto* farClip = panel.findChild<QDoubleSpinBox*>("layerCameraFarClip"); QVERIFY(farClip);
        QCOMPARE(nearClip->value(), 1.0); QCOMPARE(farClip->value(), 100000.0);
        nearClip->setValue(10); emit nearClip->editingFinished();
        farClip->setValue(8000); emit farClip->editingFinished();
        QCOMPARE(shot->layers()[0].camera.nearClip, 10.0);
        QCOMPARE(shot->layers()[0].camera.farClip, 8000.0);
        QCOMPARE(render::sceneCameraOf(shot.get(), shot->displaySize(), 0).farPlane, 8000.0);
    }
    // A camera's depth of field (Flux's DepthOfFieldRenderer): the blur term
    // of the thin lens, nothing at the focus, more the farther; in a shot a
    // plane far behind the focus goes soft at its edges, one at the focus
    // stays sharp, and without HasDoF nothing changes.
    void cameraDepthOfFieldBlursAwayFromFocus() {
        model3d::DepthOfField dof; dof.enabled = true;
        dof.focalLength = 35; dof.lensDiameter = 100 / 2.8352; dof.focusDistance = 311.1 / 2.8352;
        const double atFocus = dof.blurTerm(311.1 * 0.98);   // the lens's 50.25 mm puts the sharpest just short of it
        QVERIFY(atFocus < 0.02);
        QVERIFY(dof.blurTerm(1311) > dof.blurTerm(800));
        QVERIFY(std::abs(dof.blurTerm(1311) - 16.0 / 64.0) < 0.02);   // a CoC of about 16 pixels
        dof.blurAmount = 0.5;
        QVERIFY(std::abs(dof.blurTerm(1311) - 8.0 / 64.0) < 0.01);

        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setDurationSeconds(1);
        shot->addLayer("Camera") = ui::newCameraLayer(*shot);
        composition::Clip clip; clip.durationSeconds = 1;
        {
            auto& sharp = shot->addLayer("Sharp");
            sharp.kind = composition::LayerKind::Plane; sharp.planeColor = Qt::white;
            sharp.dimension = composition::LayerDimension::ThreeD; sharp.clips << clip;
            sharp.transform.scalePercent = QPointF(20, 20); sharp.transform.position = QPointF(-90, 0);
            auto& distant = shot->addLayer("Far");
            distant.kind = composition::LayerKind::Plane; distant.planeColor = Qt::white;
            distant.dimension = composition::LayerDimension::ThreeD; distant.clips << clip;
            distant.transform.position = QPointF(300, 0); distant.transform.positionZ = -1000;   // 76 x 43 px about x 231
        }
        const auto frame = [&] {
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 10000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const auto partial = [](const QImage& image, int from, int to) {
            int count = 0;
            for (int x = from; x < to; ++x) {
                const int level = image.pixelColor(x, 90).red();
                if (level > 40 && level < 215) ++count;
            }
            return count;
        };
        const QImage crisp = frame();
        shot->layerRef(0).camera.aperture = 100;
        QCOMPARE(frame(), crisp);   // no HasDoF: as it was
        shot->layerRef(0).camera.depthOfField = true;
        const QImage soft = frame();
        QVERIFY(soft.save(testArtifactPath(QStringLiteral("camera-depth-of-field.png"))));
        QVERIFY2(partial(soft, 160, 320) > partial(crisp, 160, 320) + 6,
                 qPrintable(QStringLiteral("%1 %2").arg(partial(crisp, 160, 320)).arg(partial(soft, 160, 320))));
        QCOMPARE(partial(soft, 0, 140), partial(crisp, 0, 140));
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
    // Text under a parent turned a quarter clockwise: its frame turns with
    // the parent, and moving it 50 right and 20 down on the canvas moves it in
    // the parent's units (20 right, 50 up there).
    void textFrameFollowsItsParent() {
        ui::ViewerWidget viewer;
        viewer.resize(664, 360); viewer.show(); QTest::qWait(20);
        viewer.setProjectSize(QSize(640, 360));
        viewer.setFrame(QImage(640, 360, QImage::Format_ARGB32_Premultiplied));
        composition::Layer layer; layer.kind = composition::LayerKind::Text;
        composition::TextStyle style; style.text = "Hello"; style.fontSize = 60;
        style.alignH = composition::TextStyle::AlignH::Center;
        style.alignV = composition::TextStyle::AlignV::Middle;
        // The parent's turn about the canvas centre.
        QTransform after;
        after.translate(320, 180); after.rotate(90); after.translate(-320, -180);
        const auto targetOf = [&] {
            ui::TextTransformOverlay::Target target{&layer, style, 0, QSizeF(640, 360)};
            target.after = after;
            return target;
        };
        ui::TextTransformOverlay overlay(targetOf, [] {}, [](const composition::LayerTransform&,
                                                              const composition::LayerTransform&, const QString&) {});
        viewer.addOverlay(&overlay);
        const QRectF upright = ui::TextTransformOverlay::quadFor({&layer, style, 0, QSizeF(640, 360)}).boundingRect();
        const QRectF turned = ui::TextTransformOverlay::quadFor(targetOf()).boundingRect();
        QVERIFY2(std::abs(turned.width() - upright.height()) < 1.0 && std::abs(turned.height() - upright.width()) < 1.0,
                 qPrintable(QStringLiteral("%1x%2 %3x%4").arg(upright.width()).arg(upright.height())
                                .arg(turned.width()).arg(turned.height())));
        const QPoint centre = viewer.mapping().toWidget(turned.center()).toPoint();
        QCOMPARE(overlay.handleAt(centre, viewer.mapping()), ui::TextTransformOverlay::Handle::Move);
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, centre);
        QTest::mouseMove(&viewer, centre + QPoint(50, 20));
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, centre + QPoint(50, 20));
        QVERIFY2(std::abs(layer.transform.position.x() - 20.0) < 0.01 && std::abs(layer.transform.position.y() - 50.0) < 0.01,
                 qPrintable(QStringLiteral("%1 %2").arg(layer.transform.position.x()).arg(layer.transform.position.y())));
        viewer.removeOverlay(&overlay);
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
    // The Viewer frames footage as the renderer draws it, not only text: a
    // corner scales it evenly with the opposite corner held where it is on
    // the canvas, an edge scales one axis, also when it is turned about its
    // anchor point; the knob turns it about that anchor. Under the Text tool
    // it is not framed, and with other layers selected a drag inside it is
    // left to the viewer, which moves them all.
    void viewerFramesFootage() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString still = temp.filePath("red.png");
        QImage red(100, 50, QImage::Format_RGB32); red.fill(Qt::red);
        QVERIFY(red.save(still));
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importFile(still).isSuccess());
        const core::Identifier stillId = media->assetByFilePath(QFileInfo(still).absoluteFilePath()).id();
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(640, 360); shot->setDurationSeconds(1);
        {
            auto& layer = shot->addLayer("Still");
            composition::Clip clip; clip.mediaId = stillId; clip.durationSeconds = 1;
            layer.clips.append(clip);
            layer.transform.position = QPointF(-60, 30);
            layer.transform.rotationDegrees = 30;
            layer.transform.anchorPoint = QPointF(40, -20);
        }
        composition::Layer& layer = shot->layerRef(0);
        bool alone = true;
        QStringList commits;
        composition::LayerTransform committedAfter;
        ui::TextTransformOverlay overlay(
            [&] {
                ui::TextTransformOverlay::Target target;
                target.layer = &layer;
                target.canvasSize = QSizeF(640, 360);
                target.geometry = render::layerGeometry(*shot, layer, media.get(), 0);
                target.text = false;
                target.movesAlone = alone;
                return target;
            },
            {},
            [&](const composition::LayerTransform&, const composition::LayerTransform& after,
                const QString& title) { committedAfter = after; commits << title; });
        ui::ViewerWidget viewer;
        viewer.resize(664, 360); viewer.show(); QTest::qWait(20);
        viewer.setProjectSize(QSize(640, 360));
        viewer.setFrame(QImage(640, 360, QImage::Format_ARGB32_Premultiplied));
        viewer.addOverlay(&overlay);
        const ui::ViewerMapping mapping = viewer.mapping();
        const auto geometry = [&] { return render::layerGeometry(*shot, layer, media.get(), 0); };
        const auto close = [](const QPointF& a, const QPointF& b, double tolerance) {
            return QLineF(a, b).length() <= tolerance;
        };
        const auto describe = [](const QPointF& point) {
            return QStringLiteral("(%1, %2)").arg(point.x()).arg(point.y());
        };
        const auto drag = [&](const QPointF& from, const QPointF& to) {
            QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, from.toPoint());
            QTest::mouseMove(&viewer, to.toPoint());
            QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, to.toPoint());
        };

        // The frame is the renderer's box.
        QPolygonF quad = overlay.canvasQuad();
        QCOMPARE(quad.size(), 4);
        for (int i = 0; i < 4; ++i) QVERIFY(close(quad[i], geometry().quad[i], 1e-6));

        // The bottom-right corner pulled out along the diagonal: half as big
        // again, same proportions, the top-left corner where it was.
        const QPointF topLeft = quad[0];
        const QPointF corner = mapping.toWidget(quad[2]);
        QCOMPARE(overlay.handleAt(corner, mapping), ui::TextTransformOverlay::Handle::BottomRight);
        const QPointF cornerTo = mapping.toWidget(quad[2] + (quad[2] - quad[0]) * 0.5);
        drag(corner, cornerTo);
        QCOMPARE(commits, QStringList{"Scale"});
        const QPointF scale = layer.transform.scalePercent;
        QVERIFY2(std::abs(scale.x() - 150.0) < 2.0 && std::abs(scale.x() - scale.y()) < 1e-6,
                 qPrintable(describe(scale)));
        QCOMPARE(layer.transform.rotationDegrees, 30.0);
        QVERIFY2(close(geometry().quad[0], topLeft, 1e-6), qPrintable(describe(geometry().quad[0])));
        QVERIFY(close(geometry().quad[2], mapping.toCanvas(cornerTo), 1.5));
        QCOMPARE(committedAfter.scalePercent, scale);

        // The left edge: wider only, the right edge where it was.
        quad = overlay.canvasQuad();
        const QPointF leftMiddle = (quad[0] + quad[3]) / 2.0;
        const QPointF rightMiddle = (quad[1] + quad[2]) / 2.0;
        const QPointF edge = mapping.toWidget(leftMiddle);
        QCOMPARE(overlay.handleAt(edge, mapping), ui::TextTransformOverlay::Handle::Left);
        drag(edge, mapping.toWidget(leftMiddle + (leftMiddle - rightMiddle) * 0.25));
        QCOMPARE(commits.last(), QString("Scale"));
        QVERIFY2(std::abs(layer.transform.scalePercent.x() - scale.x() * 1.25) < 2.0,
                 qPrintable(describe(layer.transform.scalePercent)));
        QCOMPARE(layer.transform.scalePercent.y(), scale.y());
        const QPolygonF widened = geometry().quad;
        QVERIFY2(close((widened[1] + widened[2]) / 2.0, rightMiddle, 1e-6),
                 qPrintable(describe((widened[1] + widened[2]) / 2.0)));

        // The knob turns it about its anchor point: the picture's point there
        // stays put, the position does not change.
        quad = overlay.canvasQuad();
        const QPointF position = layer.transform.position;
        const QPointF pivot(320 + position.x() + 40, 180 - position.y() + 20);
        const QPointF pinned = geometry().toCanvas.inverted().map(pivot);
        const QPointF top = mapping.toWidget((quad[0] + quad[1]) / 2.0);
        QPointF up = top - mapping.toWidget(quad.boundingRect().center());
        up /= std::hypot(up.x(), up.y());
        const QPointF knob = top + up * 24.0;
        QCOMPARE(overlay.handleAt(knob, mapping), ui::TextTransformOverlay::Handle::Rotate);
        const QPointF arm = knob - mapping.toWidget(pivot);
        drag(knob, mapping.toWidget(pivot) + QPointF(-arm.y(), arm.x()));
        QCOMPARE(commits.last(), QString("Rotation Z"));
        QVERIFY2(std::abs(layer.transform.rotationDegrees - 120.0) < 2.0,
                 qPrintable(QString::number(layer.transform.rotationDegrees)));
        QCOMPARE(layer.transform.position, position);
        QVERIFY2(close(geometry().toCanvas.map(pinned), pivot, 1e-6),
                 qPrintable(describe(geometry().toCanvas.map(pinned))));

        // The renderer draws the footage in the frame the drags left.
        {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0, QSize(640, 360));
            QVERIFY(QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000));
            const QByteArray bytes = frames.last()[1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 640, 360, 640 * 4,
                               QImage::Format_RGBA8888);
            QRectF box;
            for (int y = 0; y < 360; ++y)
                for (int x = 0; x < 640; ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (c.red() > 200 && c.green() < 60) box = box.united(QRectF(x, y, 1, 1));
                }
            const QRectF expected = geometry().bounds();
            QVERIFY2(std::abs(box.left() - expected.left()) <= 2 && std::abs(box.top() - expected.top()) <= 2
                         && std::abs(box.right() - expected.right()) <= 2
                         && std::abs(box.bottom() - expected.bottom()) <= 2,
                     qPrintable(QStringLiteral("drawn %1 %2, framed %3 %4")
                                    .arg(describe(box.topLeft()), describe(box.bottomRight()),
                                         describe(expected.topLeft()), describe(expected.bottomRight()))));
        }

        // Inside the frame: moved, Y up in the model.
        const QPointF middle = mapping.toWidget(overlay.canvasQuad().boundingRect().center());
        QCOMPARE(overlay.handleAt(middle, mapping), ui::TextTransformOverlay::Handle::Move);
        drag(middle, middle + QPointF(30, -10));
        QCOMPARE(commits.last(), QString("Move Layer"));
        QCOMPARE(layer.transform.position, position + QPointF(30, 10));
        QVERIFY(viewer.grab().save(testArtifactPath(QStringLiteral("viewer-footage-frame.png"))));

        // With other layers selected the drag is the viewer's.
        alone = false;
        QSignalSpy moved(&viewer, &ui::ViewerWidget::layerMoved);
        const int committed = commits.size();
        drag(middle, middle + QPointF(20, 0));
        QVERIFY(moved.count() > 0);
        QCOMPARE(commits.size(), committed);
        QCOMPARE(layer.transform.position, position + QPointF(30, 10));
        alone = true;

        // The Text tool leaves footage alone.
        viewer.setTool(ui::ViewerWidget::ViewerTool::Text);
        QVERIFY(overlay.canvasQuad().isEmpty());
        QCOMPARE(overlay.handleAt(middle, mapping), ui::TextTransformOverlay::Handle::None);
        viewer.setTool(ui::ViewerWidget::ViewerTool::Select);
        QCOMPARE(overlay.canvasQuad().size(), 4);
        viewer.removeOverlay(&overlay);
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
    // A Half preview draws native 2D effects at half size: RadialBlur reads
    // the scale in its RenderContext (+0x2c/+0x30; +0x24 stays the
    // normalisation factor) and the host reduces its moved centre; the host
    // reduces BoxBlur's radius in px and Magnify's iris radius, in pixels
    // without saying so (unmarkedPixelValues). Emboss, which depends on the
    // pixel grid, is drawn full and reduced. All four look like the full frame
    // reduced.
    void nativeEffectsDrawReducedPreviews() {
        const QString dir = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const QString radial = dir + "/Plugins/2D/RadialBlur.hfpl";
        const QString box = dir + "/Plugins/2D/BoxBlur.hfpl";
        const QString magnify = dir + "/Plugins/2D/Magnify.hfpl";
        const QString emboss = dir + "/Plugins/2D/Emboss.hfpl";
        if (!QFileInfo::exists(radial) || !QFileInfo::exists(box) || !QFileInfo::exists(magnify)
            || !QFileInfo::exists(emboss))
            QSKIP("The reference modules are not in SAMPLES");
        const core::Identifier radialId("test.native.radialblur"), boxId("test.native.boxblur"),
            magnifyId("test.native.magnify"), embossId("test.native.emboss");
        const auto radialMetadata = plugin::loadNativePluginMetadata(radial, dir);
        const auto boxMetadata = plugin::loadNativePluginMetadata(box, dir);
        const auto magnifyMetadata = plugin::loadNativePluginMetadata(magnify, dir);
        const auto embossMetadata = plugin::loadNativePluginMetadata(emboss, dir);
        plugin::registerNativeEffectModule(radialId, radial, dir, true, radialMetadata.parameters);
        plugin::registerNativeEffectModule(boxId, box, dir, true, boxMetadata.parameters);
        plugin::registerNativeEffectModule(magnifyId, magnify, dir, true, magnifyMetadata.parameters);
        plugin::registerNativeEffectModule(embossId, emboss, dir, true, embossMetadata.parameters);
        auto cleanup = qScopeGuard([] {
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        QVERIFY(plugin::nativeEffectRendersReduced(radialId));
        QVERIFY(plugin::nativeEffectRendersReduced(boxId));
        QVERIFY(plugin::nativeEffectRendersReduced(magnifyId));
        QVERIFY(!plugin::nativeEffectRendersReduced(embossId));
        // The units the modules register: BoxBlur's radius in px.
        for (const auto& parameter : boxMetadata.parameters)
            if (parameter.name.startsWith("radius")) QCOMPARE(parameter.unit, QString("px"));
        {
            QImage probe(8, 8, QImage::Format_RGBA8888); probe.fill(Qt::white);
            QStringList values;
            for (const auto& parameter : radialMetadata.parameters) values << parameter.defaultValue;
            if (!plugin::applyEffectToImage(probe, radialId, values, plugin::NativeFrameTime()))
                QSKIP("No OpenGL 4.1 context for native modules on this platform");
        }
        struct Case { core::Identifier id; plugin::NativePluginInfo metadata; QHash<QString, QString> values; };
        const QVector<Case> cases {
            {radialId, radialMetadata, {{"center", "60,-30"}, {"angle", "12"}}},
            {boxId, boxMetadata, {{"radius", "12"}, {"radius_f", "12"}}},
            {magnifyId, magnifyMetadata, {{"center", "-40,20"}, {"irisRadius", "50"}}},
            {embossId, embossMetadata, {}},
        };
        for (const Case& test : cases) {
            auto shot = std::make_shared<composition::Composition>();
            shot->setSize(320, 180); shot->setDurationSeconds(1);
            auto& plane = shot->addLayer("Plane");
            plane.kind = composition::LayerKind::Plane; plane.planeColor = QColor(240, 220, 40);
            plane.transform.scalePercent = QPointF(40, 40);
            plane.transform.rotationDegrees = 20;
            composition::Clip clip; clip.durationSeconds = 1;
            composition::Effect fx; fx.pluginId = test.id; fx.name = test.id.value();
            for (const auto& parameter : test.metadata.parameters)
                fx.parameterValues << test.values.value(parameter.name, parameter.defaultValue);
            clip.effects << fx; plane.clips << clip;
            const auto frameAt = [&](const QSize& size) {
                render::RenderManager manager; manager.setComposition(shot);
                QSignalSpy frames(&manager, &render::RenderManager::frameReady);
                manager.requestFrame(0, 0, size);
                if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 10000)) return QImage();
                const QByteArray bytes = frames.last()[1].toByteArray();
                const QSize got = frames.last()[2].toSize();
                return QImage(reinterpret_cast<const uchar*>(bytes.constData()), got.width(), got.height(),
                              got.width() * 4, QImage::Format_RGBA8888).copy();
            };
            const QImage full = frameAt(QSize(320, 180));
            const QImage half = frameAt(QSize(160, 90));
            QVERIFY(!full.isNull() && !half.isNull());
            const QImage expected = full.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                                        .scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                        .convertToFormat(QImage::Format_RGBA8888);
            double sum = 0.0;
            for (int y = 0; y < 90; ++y)
                for (int x = 0; x < 160; ++x) {
                    const QColor a = half.pixelColor(x, y), b = expected.pixelColor(x, y);
                    sum += (qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue())) / 3.0;
                }
            const double mean = sum / (160.0 * 90.0);
            half.save(testArtifactPath(QStringLiteral("half-native-%1.png").arg(test.id.value().section('.', -1))));
            QVERIFY2(mean < 3.0, qPrintable(QStringLiteral("%1: mean %2").arg(test.id.value()).arg(mean)));
        }
        // Without the host reducing them the moved centre and the radius
        // would be twice as far and as wide in the Half preview.
        QImage pattern(128, 128, QImage::Format_RGBA8888);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
                pattern.setPixelColor(x, y, QColor((x / 16 + y / 16) % 2 ? 230 : 30, x * 2, y * 2));
        QStringList boxValues;
        for (const auto& parameter : boxMetadata.parameters)
            boxValues << (parameter.name.startsWith("radius") ? QString("12") : parameter.defaultValue);
        const QImage halfPattern = pattern.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage reduced = halfPattern, unreduced = halfPattern;
        plugin::NativeFrameTime time; time.renderScale = 0.5f;
        QVERIFY(plugin::applyEffectToImage(reduced, boxId, boxValues, time));
        time.reducedValues = plugin::NativeReducesNothing;
        QVERIFY(plugin::applyEffectToImage(unreduced, boxId, boxValues, time));
        QVERIFY(reduced != unreduced);
    }
    // Behaviors get their time in milliseconds, their opacity is read back
    // from the context, and they are told where their layer stands and how
    // big it is (PluginBehaviorEffect::TransformationAtTime): FadeBehavior
    // fades in over its first 25 %, DownInsert brings a layer down from the
    // top of the frame and leaves it in place.
    // A simulated body moves in the world: the reference puts the simulated
    // body's matrix in place of the layer's whole world (FUN_18036c010 after
    // SimulateLayers), so Gravity pulls a plane down the frame even under a
    // parent turned a quarter, where a Behavior inside the parent would push
    // it sideways. Its box and its drawn pixels agree.
    void simulatedBehaviorsMoveInTheWorld() {
        const QString dir = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const QString gravity = dir + "/Plugins/Behavior/Gravity.hfpl";
        if (!QFileInfo::exists(gravity)) QSKIP("The reference Gravity module is not in SAMPLES");
        const core::Identifier id("test.native.gravity");
        const auto metadata = plugin::loadNativePluginMetadata(gravity, dir);
        plugin::registerNativeBehaviorModule(id, gravity, dir, true, metadata.parameters);
        auto cleanup = qScopeGuard([] {
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        if (!plugin::nativeBehaviorSimulationRenderingVerified(id)) QSKIP("Gravity is not a verified simulation here");
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(30, 1); shot->setDurationSeconds(2);
        {
            auto& plane = shot->addLayer("Plane");
            plane.kind = composition::LayerKind::Plane;
            plane.planeColor = Qt::red;
            plane.transform.scalePercent = QPointF(10, 10);
            composition::Effect fx; fx.pluginId = id; fx.name = "Gravity";
            for (const auto& parameter : metadata.parameters) fx.parameterValues << parameter.defaultValue;
            composition::Clip clip; clip.durationSeconds = 2; clip.effects << fx;
            plane.clips << clip;
            auto& parent = shot->addLayer("Parent");
            parent.kind = composition::LayerKind::Point;
            parent.transform.rotationDegrees = 90.0;
            shot->layerRef(0).parentLayerId = parent.id;
        }
        const composition::Layer& plane = shot->layers()[0];
        const QPointF start = render::layerGeometry(*shot, plane, nullptr, 0).bounds().center();
        const QPointF later = render::layerGeometry(*shot, plane, nullptr, 8).bounds().center();
        if (std::abs(later.x() - start.x()) < 0.01 && std::abs(later.y() - start.y()) < 0.01)
            QSKIP("Native simulation does not run on this platform");
        QVERIFY2(later.y() - start.y() > 1.0 && std::abs(later.x() - start.x()) < 0.01,
                 qPrintable(QStringLiteral("%1,%2 -> %3,%4").arg(start.x()).arg(start.y()).arg(later.x()).arg(later.y())));
        render::RenderManager manager; manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 8.0 / 30.0, QSize(320, 180));
        QVERIFY(QTest::qWaitFor([&] { return frames.count() > 0; }, 30000));
        const QByteArray bytes = frames[0][1].toByteArray();
        const QImage image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                                    QImage::Format_RGBA8888).copy();
        QRect drawn;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.red() > 128 && c.green() < 80) drawn |= QRect(x, y, 1, 1);
            }
        QVERIFY2(!drawn.isEmpty(), qPrintable(QStringLiteral("nothing drawn, box %1,%2").arg(later.x()).arg(later.y())));
        const QPointF centre = QRectF(drawn).center() + QPointF(0.5, 0.5);
        QVERIFY2(std::abs(centre.x() - later.x()) <= 2.0 && std::abs(centre.y() - later.y()) <= 2.0,
                 qPrintable(QStringLiteral("drawn %1,%2 box %3,%4").arg(centre.x()).arg(centre.y())
                                .arg(later.x()).arg(later.y())));
        // A child of the falling plane falls with it: a simulated parent
        // puts its children under its body (ParentingWorldTransformationAtTime
        // with the simulation on).
        {
            composition::Layer child;
            child.name = "Child"; child.kind = composition::LayerKind::Plane;
            child.planeColor = Qt::blue;
            child.transform.position = QPointF(0, 300);
            child.parentLayerId = shot->layers()[0].id;
            composition::Clip clip; clip.durationSeconds = 2; child.clips << clip;
            QVERIFY(shot->insertLayer(0, child));
            const composition::Layer& placed = shot->layers()[0];
            const QPointF childStart = render::layerGeometry(*shot, placed, nullptr, 0).bounds().center();
            const QPointF childLater = render::layerGeometry(*shot, placed, nullptr, 8).bounds().center();
            const QPointF fall = later - start;
            QVERIFY2(std::abs(childLater.x() - childStart.x() - fall.x()) < 0.05
                         && std::abs(childLater.y() - childStart.y() - fall.y()) < 0.05,
                     qPrintable(QStringLiteral("%1,%2 against %3,%4").arg(childLater.x() - childStart.x())
                                    .arg(childLater.y() - childStart.y()).arg(fall.x()).arg(fall.y())));
        }
        // The run is kept and gone on from: an earlier frame read after a
        // later one is what a fresh run gives (the layer renamed: another
        // state of the shot); between frames the body is taken back along
        // its velocity, between where it is at the frames either side.
        {
            const composition::Layer& falling = shot->layers()[1];
            QVector3D at4, at7, at8, between, fresh;
            QVERIFY(render::layerSimulationOffsetAt(*shot, nullptr, falling, 4, &at4));
            QVERIFY(render::layerSimulationOffsetAt(*shot, nullptr, falling, 7, &at7));
            QVERIFY(render::layerSimulationOffsetAt(*shot, nullptr, falling, 8, &at8));
            QVERIFY(render::layerSimulationOffsetAt(*shot, nullptr, falling, 7.5, &between));
            QVERIFY2(between.y() < at7.y() && between.y() > at8.y(),
                     qPrintable(QStringLiteral("%1 %2 %3").arg(at7.y()).arg(between.y()).arg(at8.y())));
            shot->layerRef(1).name = QStringLiteral("Renamed");
            QVERIFY(render::layerSimulationOffsetAt(*shot, nullptr, shot->layers()[1], 4, &fresh));
            QVERIFY2((fresh - at4).length() < 1e-4f,
                     qPrintable(QStringLiteral("%1 against %2").arg(fresh.y()).arg(at4.y())));
        }
        // A body moving without keys is blurred along its fall: its samples
        // take it between frames.
        {
            auto falling = std::make_shared<composition::Composition>();
            falling->setSize(320, 180); falling->setFrameRate(30, 1); falling->setDurationSeconds(2);
            auto& plane = falling->addLayer("Falling");
            plane.kind = composition::LayerKind::Plane;
            plane.planeColor = Qt::red;
            plane.transform.scalePercent = QPointF(10, 10);
            composition::Effect fx; fx.pluginId = id; fx.name = "Gravity";
            for (const auto& parameter : metadata.parameters) fx.parameterValues << parameter.defaultValue;
            composition::Clip clip; clip.durationSeconds = 2; clip.effects << fx;
            plane.clips << clip;
            const auto redRows = [&](bool blurred) {
                falling->layerRef(0).motionBlur = blurred;
                render::RenderManager manager; manager.setComposition(falling);
                QSignalSpy frames(&manager, &render::RenderManager::frameReady);
                manager.requestFrame(0, 16.0 / 30.0, QSize(320, 180));
                if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 30000)) return -1;
                const QByteArray bytes = frames[0][1].toByteArray();
                const QImage image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                                            QImage::Format_RGBA8888).copy();
                int rows = 0;
                for (int y = 0; y < image.height(); ++y) {
                    bool red = false;
                    for (int x = 0; x < image.width() && !red; ++x) red = image.pixelColor(x, y).red() > 40;
                    rows += red ? 1 : 0;
                }
                return rows;
            };
            const int sharp = redRows(false), smeared = redRows(true);
            QVERIFY2(sharp > 0 && smeared >= sharp + 2, qPrintable(QStringLiteral("%1 %2").arg(sharp).arg(smeared)));
        }
        const QString drag = dir + "/Plugins/Behavior/Drag.hfpl";
        if (!QFileInfo::exists(drag)) return;
        const core::Identifier dragId("test.native.drag");
        const auto dragMetadata = plugin::loadNativePluginMetadata(drag, dir);
        plugin::registerNativeBehaviorModule(dragId, drag, dir, true, dragMetadata.parameters);
        // A child that is a body itself: its body stands for its world, so
        // the parent's fall does not carry it again; the parent's forces
        // come to it instead (SimulateLayers' list by ParentLayer) - it falls
        // about as far, a little less with its own Drag, not twice as far.
        {
            composition::Layer& child = shot->layerRef(0);
            composition::Effect fx; fx.pluginId = dragId; fx.name = "Drag";
            for (const auto& parameter : dragMetadata.parameters) fx.parameterValues << parameter.defaultValue;
            child.clips[0].effects << fx;
            const composition::Layer& placed = shot->layers()[0];
            const QPointF childStart = render::layerGeometry(*shot, placed, nullptr, 0).bounds().center();
            const QPointF childLater = render::layerGeometry(*shot, placed, nullptr, 8).bounds().center();
            const double fall = later.y() - start.y();
            const double childFall = childLater.y() - childStart.y();
            QVERIFY2(childFall > 0.5 * fall && childFall < fall + 0.05,
                     qPrintable(QStringLiteral("%1 against %2").arg(childFall).arg(fall)));
        }
        // Drag damps the whole velocity, the animation's too: a plane keyed
        // to cross the frame at a steady speed lags behind its keys with it.
        auto moving = std::make_shared<composition::Composition>();
        moving->setSize(320, 180); moving->setFrameRate(30, 1); moving->setDurationSeconds(2);
        {
            auto& plane = moving->addLayer("Moving");
            plane.kind = composition::LayerKind::Plane;
            plane.transform.scalePercent = QPointF(10, 10);
            plane.transform.positionXCurve.set(0, -120.0);
            plane.transform.positionXCurve.set(30, 120.0);
            composition::Effect fx; fx.pluginId = dragId; fx.name = "Drag";
            for (const auto& parameter : dragMetadata.parameters) fx.parameterValues << parameter.defaultValue;
            composition::Clip clip; clip.durationSeconds = 2; clip.effects << fx;
            plane.clips << clip;
        }
        const double keyed = 160.0 + moving->layers()[0].transform.positionAt(15).x();
        const double dragged = render::layerGeometry(*moving, moving->layers()[0], nullptr, 15).bounds().center().x();
        QVERIFY2(dragged < keyed - 1.0 && dragged > 160.0 - 120.0,
                 qPrintable(QStringLiteral("%1 against %2").arg(dragged).arg(keyed)));
    }
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
        // A Half preview draws the layer reduced and the module still sees
        // the shot at full size: the plane is moved by half as many pixels,
        // and the picture is the full frame reduced.
        for (double seconds : {0.0, 0.25, 2.0}) {
            const QImage full = frameAt(seconds);
            QVERIFY(!full.isNull());
            frames.clear();
            manager.requestFrame(0, seconds, QSize(100, 50));
            QVERIFY(QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000));
            const QByteArray bytes = frames.last()[1].toByteArray();
            QCOMPARE(frames.last()[2].toSize(), QSize(100, 50));
            const QImage half = QImage(reinterpret_cast<const uchar*>(bytes.constData()), 100, 50, 100 * 4,
                                       QImage::Format_RGBA8888).copy();
            const QImage expected = full.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                                        .scaled(100, 50, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                        .convertToFormat(QImage::Format_RGBA8888);
            // The plane is small: count the pixels that are plainly off
            // rather than average them away.
            int off = 0;
            for (int y = 0; y < 50; ++y)
                for (int x = 0; x < 100; ++x) {
                    const QColor a = half.pixelColor(x, y), b = expected.pixelColor(x, y);
                    if (qMax(qAbs(a.red() - b.red()), qMax(qAbs(a.green() - b.green()), qAbs(a.blue() - b.blue()))) > 80)
                        ++off;
                }
            half.save(testArtifactPath(QStringLiteral("half-behavior-%1.png").arg(seconds)));
            expected.save(testArtifactPath(QStringLiteral("half-behavior-expected-%1.png").arg(seconds)));
            QVERIFY2(off <= 6, qPrintable(QStringLiteral("%1 s: %2 pixels off").arg(seconds).arg(off)));
        }
    }
    // A native plug-in's controls in the interface language: its name,
    // labels, units and combo items through its own catalogue (context
    // "Effect_Glow"), and a combo that still stores the module's item text,
    // which is what the module reads its value by.
    void nativePluginControlsAreTranslated() {
        const QString glow = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects/Plugins/2D/Glow.hfpl";
        if (!QFileInfo::exists(glow)) QSKIP("The reference Glow.hfpl is not in SAMPLES");
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("Plugins")));
        QVERIFY(QFile::copy(glow, dir.filePath("Plugins/Glow.hfpl")));
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(TRANSLATION_TEST_ROOT) + "/ru/openvegaseffects_plugins_ru.qm"));
        QCoreApplication::installTranslator(&translator);
        auto restore = qScopeGuard([&] {
            QCoreApplication::removeTranslator(&translator);
            QStandardPaths::setTestModeEnabled(false);
            // The controls asked the module, which keeps the copied file loaded.
            plugin::clearNativeControlStates();
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        auto manager = plugin::createPluginManager();
        manager->setPluginDirs({dir.filePath("Plugins")});
        QVERIFY(manager->scan().isSuccess());
        plugin::EffectSpec spec;
        for (const auto& id : manager->allPluginIds())
            if (manager->spec(id).translationContext == QLatin1String("Effect_Glow")) spec = manager->spec(id);
        if (spec.parameters.isEmpty()) QSKIP("Glow's controls could not be read on this platform");
        QCOMPARE(spec.displayName, QString("Свечение"));
        const auto parameter = [&](const QString& name) {
            for (const auto& p : spec.parameters) if (p.name == name) return p;
            return plugin::EffectParameterSpec();
        };
        QCOMPARE(parameter("radius_f").displayName, QString("Радиус"));
        QCOMPARE(parameter("radius_f").unit, QString("px"));
        QCOMPARE(parameter("intensity").displayName, QString("Интенсивность"));
        const auto blend = parameter("blendType");
        QCOMPARE(blend.choices.size(), blend.choiceValues.size());
        const int screen = blend.choiceValues.indexOf("Screen");
        QVERIFY(screen >= 0);
        QCOMPARE(blend.choices.at(screen), QString("Экран"));
        // The stored default is still the module's own item.
        QVERIFY(blend.choiceValues.contains(blend.defaultValue));
        const auto dimension = parameter("dimension");
        QCOMPARE(dimension.choices.at(dimension.choiceValues.indexOf("Horizontal & Vertical")),
                 QString("По горизонтали и вертикали"));

        // Controls: the combo shows the Russian item and stores the module's.
        auto shot = std::make_shared<composition::Composition>(); shot->setSize(320, 180);
        auto& layer = shot->addLayer("Plane"); layer.kind = composition::LayerKind::Plane;
        composition::Clip clip; clip.durationSeconds = 1;
        composition::Effect fx; fx.pluginId = spec.id; fx.name = spec.displayName;
        for (const auto& p : spec.parameters) fx.parameterValues << p.defaultValue;
        clip.effects << fx; layer.clips << clip;
        ui::EffectInspector controls;
        controls.setPluginManager(manager.get());
        controls.setComposition(shot);
        controls.resize(460, 760); controls.show();
        controls.setSelection(0, 0);
        QTest::qWait(50);
        bool shown = false;
        for (auto* combo : controls.findChildren<QComboBox*>())
            if (combo->currentText() == QStringLiteral("Экран") && combo->currentData().toString() == QLatin1String("Screen"))
                shown = true;
        controls.grab().save(testArtifactPath("controls-glow-ru.png"));
        QVERIFY(shown);
    }
    // A plug-in's controls come and go as the module says: those registered
    // with option 0x2 start hidden, and the module switches them with
    // SetPropertyState when it is told a property changed (Notify 7) -
    // Animated Lasers shows the controls of as many beams as asked for.
    // Glow's old integer radius stays hidden; it never answers.
    void nativeControlsFollowTheModule() {
        const QString dir = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const QString lasers = dir + "/Plugins/2D/AnimatedLasers.hfpl";
        const QString glow = dir + "/Plugins/2D/Glow.hfpl";
        if (!QFileInfo::exists(lasers) || !QFileInfo::exists(glow))
            QSKIP("The reference Animated Lasers and Glow are not in SAMPLES");
        const core::Identifier lasersId("test.native.lasers"), glowId("test.native.glow");
        const auto lasersMetadata = plugin::loadNativePluginMetadata(lasers, dir);
        const auto glowMetadata = plugin::loadNativePluginMetadata(glow, dir);
        plugin::registerNativeEffectModule(lasersId, lasers, dir, true, lasersMetadata.parameters);
        plugin::registerNativeEffectModule(glowId, glow, dir, true, glowMetadata.parameters);
        auto cleanup = qScopeGuard([] {
            plugin::clearNativeControlStates();
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        const auto spec = [](const plugin::NativePluginInfo& info, const QString& name) {
            for (const auto& p : info.parameters) if (p.name == name) return p;
            return plugin::EffectParameterSpec();
        };
        // Registered: the first beam's controls shown, the second's hidden.
        QVERIFY(!spec(lasersMetadata, "coreColor0").hidden);
        QVERIFY(spec(lasersMetadata, "coreColor1").hidden);
        QVERIFY(spec(glowMetadata, "radius").hidden);
        QVERIFY(!spec(glowMetadata, "radius_f").hidden);
        const auto shown = [&](const QString& key, const plugin::NativePluginInfo& info, const QString& name) {
            return plugin::nativeControlShown(key, name, !spec(info, name).hidden);
        };
        QStringList values;
        int beams = -1;
        for (int i = 0; i < lasersMetadata.parameters.size(); ++i) {
            values << lasersMetadata.parameters[i].defaultValue;
            if (lasersMetadata.parameters[i].name == "numberOfBeams") beams = i;
        }
        QVERIFY(beams >= 0);
        plugin::refreshNativeControlStates(lasersId, values, "lasers/0/0");
        QVERIFY(shown("lasers/0/0", lasersMetadata, "coreColor0"));
        QVERIFY(!shown("lasers/0/0", lasersMetadata, "coreColor1"));
        values[beams] = "3";
        plugin::refreshNativeControlStates(lasersId, values, "lasers/0/0");
        if (!shown("lasers/0/0", lasersMetadata, "coreColor1"))
            QSKIP("The module did not answer (no OpenGL 4.1 context on this platform)");
        QVERIFY(shown("lasers/0/0", lasersMetadata, "coreColor2"));
        QVERIFY(!shown("lasers/0/0", lasersMetadata, "coreColor3"));
        // Another effect of the same plug-in keeps its own controls.
        values[beams] = "1";
        plugin::refreshNativeControlStates(lasersId, values, "lasers/0/1");
        QVERIFY(!shown("lasers/0/1", lasersMetadata, "coreColor1"));
        QVERIFY(shown("lasers/0/0", lasersMetadata, "coreColor1"));
        // Glow says nothing: its old radius stays as registered.
        QStringList glowValues;
        for (const auto& p : glowMetadata.parameters) glowValues << p.defaultValue;
        plugin::refreshNativeControlStates(glowId, glowValues, "glow/0/0");
        QVERIFY(!shown("glow/0/0", glowMetadata, "radius"));
        QVERIFY(shown("glow/0/0", glowMetadata, "radius_f"));
    }
    // Glyph behaviors move the letters in the shot's units under the text
    // painter's scale, so a Half preview draws them reduced: the letters
    // stand where the full frame reduced has them.
    void nativeGlyphBehaviorsDrawReducedPreviews() {
        const QString dir = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        const QString drop = dir + "/Plugins/Behavior/DropInByChar.hfpl";
        if (!QFileInfo::exists(drop)) QSKIP("The reference DropInByChar.hfpl is not in SAMPLES");
        const core::Identifier id("test.native.dropinbychar");
        const auto metadata = plugin::loadNativePluginMetadata(drop, dir);
        plugin::registerNativeBehaviorModule(id, drop, dir, true, metadata.parameters);
        auto cleanup = qScopeGuard([] {
            plugin::releaseNativeEffectThreadRenderer();
            plugin::clearNativeEffectModules();
        });
        if (!plugin::nativeBehaviorSubObjectRenderingVerified(id))
            QSKIP("DropInByChar is not a verified glyph behavior here");
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setDurationSeconds(4);
        {
            auto& layer = shot->addLayer("Text"); layer.kind = composition::LayerKind::Text;
            composition::TextStyle style;
            style.text = "DROP"; style.fontSize = 48;
            style.alignH = composition::TextStyle::AlignH::Center;
            style.alignV = composition::TextStyle::AlignV::Middle;
            composition::Clip clip; clip.durationSeconds = 4;
            composition::Effect text; text.pluginId = core::Identifier("text"); text.name = "Text";
            text.parameterValues = composition::textStyleToParameters(style);
            composition::Effect behavior; behavior.pluginId = id; behavior.name = "Drop In By Char";
            for (const auto& parameter : metadata.parameters) behavior.parameterValues << parameter.defaultValue;
            clip.effects << text << behavior;
            layer.clips.append(clip);
        }
        // The Layout box does not follow the glyphs: the reference frames a
        // layer by its own extent times WorldTransformationAtTime
        // (FUN_1403186c0), and a glyph Behavior moves no layer - while the
        // letters drop in, the box is the one of the same text without it.
        {
            composition::Layer plain = shot->layers()[0];
            plain.clips[0].effects.removeLast();
            for (int frame : {6, 15, 90}) {
                QCOMPARE(render::layerGeometry(*shot, shot->layers()[0], nullptr, frame).bounds(),
                         render::layerGeometry(*shot, plain, nullptr, frame).bounds());
                QCOMPARE(render::layerBehaviorTransformAt(*shot, nullptr, shot->layers()[0], frame), QTransform());
            }
        }
        render::RenderManager manager; manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        const auto frameAt = [&](double seconds, const QSize& size) {
            frames.clear();
            manager.requestFrame(0, seconds, size);
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 10000)) return QImage();
            const QByteArray bytes = frames.last()[1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), size.width(), size.height(),
                          size.width() * 4, QImage::Format_RGBA8888).copy();
        };
        // Where the letters are: the bright pixels' box and their centre.
        struct Ink { QRect box; QPointF centre; double mass = 0; };
        const auto inkOf = [](const QImage& image) {
            Ink ink; double sx = 0, sy = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const double weight = qMax(0, image.pixelColor(x, y).lightness() - 30);
                    if (weight <= 0) continue;
                    if (weight > 100) ink.box |= QRect(x, y, 1, 1);
                    sx += weight * x; sy += weight * y; ink.mass += weight;
                }
            if (ink.mass > 0) ink.centre = QPointF(sx / ink.mass, sy / ink.mass);
            return ink;
        };
        QVector<QPointF> centres;
        for (double seconds : {0.2, 0.5, 3.0}) {
            const QImage full = frameAt(seconds, QSize(320, 180));
            const QImage half = frameAt(seconds, QSize(160, 90));
            QVERIFY(!full.isNull() && !half.isNull());
            const QImage expected = full.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                                        .scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                        .convertToFormat(QImage::Format_RGBA8888);
            half.save(testArtifactPath(QStringLiteral("half-glyphs-%1.png").arg(seconds)));
            expected.save(testArtifactPath(QStringLiteral("half-glyphs-expected-%1.png").arg(seconds)));
            const Ink got = inkOf(half), want = inkOf(expected);
            const QString where = QStringLiteral("%1 s: box (%2,%3 %4x%5) vs (%6,%7 %8x%9), centre (%10,%11) vs (%12,%13)")
                .arg(seconds).arg(got.box.x()).arg(got.box.y()).arg(got.box.width()).arg(got.box.height())
                .arg(want.box.x()).arg(want.box.y()).arg(want.box.width()).arg(want.box.height())
                .arg(got.centre.x()).arg(got.centre.y()).arg(want.centre.x()).arg(want.centre.y());
            QCOMPARE(got.mass > 0, want.mass > 0);
            if (want.mass <= 0) continue;
            QVERIFY2(qAbs(got.box.left() - want.box.left()) <= 1 && qAbs(got.box.right() - want.box.right()) <= 1
                         && qAbs(got.box.top() - want.box.top()) <= 1
                         && qAbs(got.box.bottom() - want.box.bottom()) <= 1,
                     qPrintable(where));
            QVERIFY2(QLineF(got.centre, want.centre).length() < 0.75, qPrintable(where));
            centres << want.centre;
        }
        // The letters were on their way down and then stood: the frames
        // compared show them in different places.
        QCOMPARE(centres.size(), 3);
        QVERIFY(QLineF(centres[0], centres[1]).length() > 3 && QLineF(centres[1], centres[2]).length() > 3);
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
    // AutomaticStabilizer and MotionLock on their own footage, as the
    // application drives them: told they were put on the layer, the footage
    // analysed in the background, the frames drawn by the renderer's own
    // instance from the effect's data (the shake gone); then their buttons -
    // a point layer with the motion's keys and "Apply To" pointed at it, keys
    // on a layer - each one step named as the module names it. Needs the
    // OpenGL 4.1 context native modules run in.
    void stabilizersSteadyTheirOwnLayer() {
        const QString dependencies = QStringLiteral(OPENVEGAS_SOURCE_DIR) + "/SAMPLES/VEGAS_Effects";
        for (const QString name : {QStringLiteral("AutomaticStabilizer"), QStringLiteral("MotionLock")}) {
            const QString module = dependencies + "/Plugins/2D/" + name + ".hfpl";
            if (!QFileInfo::exists(module)) QSKIP("The reference stabilizers are not in SAMPLES");
            const auto metadata = plugin::loadNativePluginMetadata(module, dependencies);
            const core::Identifier id(QStringLiteral("test.native.") + name.toLower());
            plugin::registerNativeEffectModule(id, module, dependencies, true, metadata.parameters);
            auto cleanup = qScopeGuard([] {
                plugin::clearNativeControlStates();
                plugin::clearNativeTrackedFeatures();
                plugin::releaseNativeEffectThreadRenderer();
                plugin::clearNativeEffectModules();
            });
            QVERIFY(plugin::nativeEffectAnalysesOwnLayer(id) && plugin::nativeEffectUsesInstance(id));
            QVERIFY(!plugin::nativeBehaviorUsesInstance(id));
            QStringList defaults;
            const auto indexOf = [&](const QString& key) {
                for (int i = 0; i < metadata.parameters.size(); ++i)
                    if (metadata.parameters.at(i).name == key) return i;
                return -1;
            };
            for (const auto& parameter : metadata.parameters) defaults << parameter.defaultValue;
            {
                plugin::NativeCustomUiView probe;
                probe.area = QSize(64, 64);
                probe.instanceKey = QStringLiteral("gl-check");
                if (!plugin::nativeCustomUiSetup(id, defaults, probe).handled)
                    QSKIP("No OpenGL 4.1 context for native modules on this platform");
                plugin::nativeCustomUiShutdown(id, defaults, probe);
            }

            // Footage: blurred noise shaking up to 6 px about where it starts.
            constexpr int kFrames = 30;
            const QSize size(320, 180);
            const int pad = 16;
            QImage texture(size.width() + 2 * pad, size.height() + 2 * pad, QImage::Format_RGBA8888);
            quint32 seed = 54321u;
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
            QVector<QPoint> offsets(kFrames);
            quint32 jitter = 777u;
            for (int f = 1; f < kFrames; ++f) {
                jitter = jitter * 1664525u + 1013904223u;
                const int dx = int((jitter >> 16) % 13u) - 6;
                jitter = jitter * 1664525u + 1013904223u;
                offsets[f] = QPoint(dx, int((jitter >> 16) % 13u) - 6);
            }
            QTemporaryDir temp; QVERIFY(temp.isValid());
            QStringList files;
            QVector<QImage> footage;
            for (int f = 0; f < kFrames; ++f) {
                footage << texture.copy(QRect(QPoint(pad, pad) - offsets.at(f), size));
                files << temp.filePath(QStringLiteral("frame%1.png").arg(f, 3, 10, QLatin1Char('0')));
                QVERIFY(footage.last().save(files.last()));
            }
            auto media = std::make_shared<media::MediaManager>();
            QString assetPath;
            QVERIFY(media->importImageSequence(files, 30.0, &assetPath).isSuccess());
            const media::MediaAsset asset = media->assetByFilePath(assetPath);
            QVERIFY(asset.isValid());

            auto shot = std::make_shared<composition::Composition>();
            shot->setSize(size.width(), size.height()); shot->setFrameRate(30, 1); shot->setDurationSeconds(1);
            core::Identifier footageId;
            {
                auto& layer = shot->addLayer("Footage");
                composition::Effect fx; fx.pluginId = id; fx.name = name; fx.parameterValues = defaults;
                composition::Clip clip; clip.mediaId = asset.id(); clip.durationSeconds = 1;
                clip.effects.append(fx);
                layer.clips.append(clip);
                footageId = layer.id;
            }
            const auto effect = [&]() -> const composition::Effect& {
                for (const composition::Layer& layer : shot->layers())
                    if (layer.id == footageId) return layer.clips[0].effects[0];
                return shot->layers()[0].clips[0].effects[0];
            };
            ui::NativeInstanceHost host([shot] { return shot; }, [media] { return media; });
            QSignalSpy edits(&host, &ui::NativeInstanceHost::layersEdited);
            const ui::NativeInstanceHost::Ref ref {footageId, 0, 0};
            // Put on its layer: the module analyses it.
            host.sync();
            QVERIFY(host.isBusy());
            QTRY_VERIFY_WITH_TIMEOUT(!host.isBusy() && !effect().instanceData.isEmpty(), 120000);

            // Where frame b's middle is found in frame a.
            const auto shiftOf = [](const QImage& a, const QImage& b) {
                const QRect middle(a.width() / 3, a.height() / 3, a.width() / 3, a.height() / 3);
                QPoint best;
                double bestError = 1e30;
                for (int dy = -14; dy <= 14; ++dy) {
                    for (int dx = -14; dx <= 14; ++dx) {
                        double sum = 0.0;
                        for (int y = middle.top(); y <= middle.bottom(); y += 2)
                            for (int x = middle.left(); x <= middle.right(); x += 2)
                                sum += std::abs(qRed(a.pixel(x + dx, y + dy)) - qRed(b.pixel(x, y)));
                        if (sum < bestError) { bestError = sum; best = QPoint(dx, dy); }
                    }
                }
                return best;
            };
            // The renderer's frames: the effect draws from its data.
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            const auto frameAt = [&](int f) {
                const int before = int(frames.count());
                manager.requestFrame(f, f / 30.0, size);
                if (!QTest::qWaitFor([&] { return frames.count() > before; }, 20000)) return QImage();
                const QByteArray bytes = frames.last()[1].toByteArray();
                return QImage(reinterpret_cast<const uchar*>(bytes.constData()), size.width(), size.height(),
                              size.width() * 4, QImage::Format_RGBA8888).copy();
            };
            int drawnJitter = 0, footageJitter = 0;
            QImage previous = frameAt(0);
            QVERIFY(!previous.isNull());
            for (int f = 1; f < kFrames - 1; ++f) {
                const QImage current = frameAt(f);
                QVERIFY(!current.isNull());
                drawnJitter += shiftOf(current, previous).manhattanLength();
                footageJitter += shiftOf(footage.at(f), footage.at(f - 1)).manhattanLength();
                previous = current;
            }
            previous.save(testArtifactPath(name + QStringLiteral("-stabilized.png")));
            QVERIFY2(footageJitter > 100 && drawnJitter * 3 < footageJitter,
                     qPrintable(QStringLiteral("%1: drawn %2, footage %3").arg(name).arg(drawnJitter)
                                    .arg(footageJitter)));

            // The buttons: a press is the value raised and the change sent.
            const auto press = [&](const QString& key) {
                const int index = indexOf(key);
                QVERIFY(index >= 0);
                for (int i = 0; i < shot->layers().size(); ++i)
                    if (shot->layers()[i].id == footageId)
                        shot->layerRef(i).clips[0].effects[0].parameterValues[index] = QStringLiteral("true");
                host.propertyChanged(ref, index, 0.0);
            };
            if (name == QLatin1String("AutomaticStabilizer")) {
                press(QStringLiteral("createPointLayer"));
                QCOMPARE(edits.count(), 1);
                QCOMPARE(edits[0][0].value<QVector<composition::Layer>>().size(), 1);
                QCOMPARE(edits[0][1].toString(), QString("Create Point Layer"));
                QCOMPARE(shot->layers().size(), 2);
                const composition::Layer& point = shot->layers()[0];
                QCOMPARE(point.kind, composition::LayerKind::Point);
                QCOMPARE(point.name, QString("Point Layer"));
                // Over the source layer's frames: its span is the clip's.
                QCOMPARE(point.spanStartSeconds, 0.0);
                QCOMPARE(point.spanEndSeconds, 1.0);
                QCOMPARE(effect().parameterValues.at(indexOf(QStringLiteral("applyToLayer"))), point.id.value());
                // Keyed at every frame of the footage, from where it starts.
                QCOMPARE(point.transform.positionXCurve.count(), kFrames);
                QCOMPARE(point.transform.rotationCurve.count(), kFrames);
                QCOMPARE(point.transform.scaleXCurve.locations().first(), 0);
                QCOMPARE(point.transform.scaleXCurve.locations().last(), kFrames - 1);
                const auto keys = point.transform.positionXCurve.all();
                press(QStringLiteral("applyTransfromToLayer"));
                QCOMPARE(edits.count(), 2);
                QCOMPARE(edits[1][1].toString(), QString("Transform Layer"));
                QCOMPARE(shot->layers()[0].transform.positionXCurve.count(), kFrames);
                QCOMPARE(shot->layers()[0].transform.positionXCurve.all().last().value, keys.last().value);
            } else {
                press(QStringLiteral("applyInverseTransfrom"));
                QCOMPARE(edits.count(), 1);
                QCOMPARE(edits[0][1].toString(), QString("Transform Layer"));
                QCOMPARE(shot->layers().size(), 1);
                const composition::LayerTransform& t = shot->layers()[0].transform;
                // The footage's own motion as keys from its first frame.
                QVERIFY(t.positionXCurve.count() >= kFrames - 1);
                QCOMPARE(t.positionXCurve.locations().first(), 0);
                const double moved = t.positionXCurve.valueAt(1).toDouble();
                QVERIFY2(std::abs(moved - offsets.at(1).x()) < 1.0, qPrintable(QString::number(moved)));
            }
        }
    }

    // One point layer's keys from four transforms, and one transform as
    // static values (CreatePointLayer / ApplyTransformToLayer): position,
    // rotationZ and scale, at the given start plus each time.
    void layerTransformsBecomeKeys() {
        composition::Layer layer;
        layer.kind = composition::LayerKind::Point;
        QVector<plugin::NativeLayerTransform> transforms;
        for (int i = 0; i < 4; ++i) transforms.append({qint32(i * 1000 / 30), float(i), float(-i), float(i * 10), 100.0f + i});
        ui::NativeInstanceHost::applyLayerTransforms(layer, transforms, 12, 30.0);
        QCOMPARE(layer.transform.positionXCurve.locations(), (QVector<int> {12, 13, 14, 15}));
        QCOMPARE(layer.transform.positionYCurve.at(15)->value.toDouble(), -3.0);
        QCOMPARE(layer.transform.rotationCurve.at(14)->value.toDouble(), 20.0);
        QCOMPARE(layer.transform.scaleYCurve.at(13)->value.toDouble(), 101.0);
        QVERIFY(layer.transform.positionZCurve.isEmpty());   // a 2D layer
        composition::Layer still;
        still.dimension = composition::LayerDimension::ThreeD;
        ui::NativeInstanceHost::applyLayerTransforms(still, {{0, 5.0f, 6.0f, 7.0f, 80.0f}}, 0, 30.0);
        QVERIFY(still.transform.positionXCurve.isEmpty());
        QCOMPARE(still.transform.position, QPointF(5, 6));
        QCOMPARE(still.transform.rotationDegrees, 7.0);
        QCOMPARE(still.transform.scalePercent, QPointF(80, 80));
        QCOMPARE(still.transform.scaleZPercent, 80.0);
        // An animated property takes a key at that time instead.
        ui::NativeInstanceHost::applyLayerTransforms(layer, {{0, 9.0f, 9.0f, 0.0f, 100.0f}}, 20, 30.0);
        QCOMPARE(layer.transform.positionXCurve.count(), 5);
        QCOMPARE(layer.transform.positionXCurve.at(20)->value.toDouble(), 9.0);
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
        QVERIFY(std::abs(camera.transform.positionZ - double(standIn.position.z())) < 1e-3);
        QVERIFY(std::abs(composition::CameraSettings::verticalFieldOfView(camera.camera.zoom, comp->height())
                         - standIn.fieldOfViewDegrees) < 1e-6);
        // The reference's "New Camera": a 35 mm lens, focused at its zoom.
        QVERIFY(std::abs(camera.camera.zoom - comp->width() / (2 * std::tan(qDegreesToRadians(54.4322) / 2))) < 1e-6);
        QCOMPARE(camera.camera.focusDistance, camera.camera.zoom);
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
        check("zoom", a.camera.zoom, b.camera.zoom);
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
    // Layout's boxes are what the renderer draws: footage at its own size and
    // pixel aspect, turned about its anchor, a 3D sheet through the camera; a
    // box typed into the panel is where the layer ends up; a camera has none.
    // A model layer has a Layout box too: its posed points seen through the
    // camera, as drawn; a box typed in fits it, and its animation moves it.
    void layoutBoxesFrameModels() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString file = writeAnimatedArm(dir.path());
        QVERIFY(!file.isEmpty());
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false; settings.singleUnitScale = 2.0;
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importModel(file, settings).isSuccess());
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(400, 200); shot->setFrameRate(24, 1); shot->setDurationSeconds(2);
        {
            auto& model = shot->addLayer("Arm");
            model.kind = composition::LayerKind::Model3D;
            model.dimension = composition::LayerDimension::ThreeD;
            model.modelAssetId = media->assetByFilePath(QFileInfo(file).absoluteFilePath()).id();
        }
        const auto rendered = [&](int frame) {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(frame, frame / 24.0, QSize(400, 200));
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000)) return QRectF();
            const QByteArray bytes = frames.last()[1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 400, 200, 400 * 4,
                               QImage::Format_RGBA8888);
            QRect box;
            for (int y = 0; y < 200; ++y)
                for (int x = 0; x < 400; ++x)
                    if (image.pixelColor(x, y).lightness() > 80) box = box.united(QRect(x, y, 1, 1));
            return QRectF(box);
        };
        const auto boxAt = [&](int frame) {
            return render::layerGeometry(*shot, shot->layers()[0], media.get(), frame);
        };
        const auto near = [](const QRectF& a, const QRectF& b, double slack) {
            return qAbs(a.left() - b.left()) <= slack && qAbs(a.top() - b.top()) <= slack
                   && qAbs(a.right() - b.right()) <= slack && qAbs(a.bottom() - b.bottom()) <= slack;
        };
        const auto describe = [](const QRectF& r) {
            return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        };
        // The 40-unit quad at twice the size, in front of the default camera:
        // 80 px across the middle of the frame.
        render::LayerGeometry geometry = boxAt(0);
        QVERIFY(geometry.valid && geometry.inScene);
        QVERIFY2(near(geometry.bounds(), QRectF(160, 60, 80, 80), 1.0), qPrintable(describe(geometry.bounds())));
        QVERIFY2(near(rendered(0), geometry.bounds(), 2.0), qPrintable(describe(rendered(0))));
        QVERIFY(qAbs(geometry.unitsPerPixel - 1.0) < 0.01);
        // Fitted to a box typed into the panel.
        const QRectF target(40, 20, 120, 120);
        QPointF position, scale;
        QVERIFY(render::fitLayerToBox(*shot, shot->layers()[0], media.get(), 0, target, &position, &scale));
        shot->layerRef(0).transform.position = position;
        shot->layerRef(0).transform.scalePercent = scale;
        QVERIFY2(near(boxAt(0).bounds(), target, 1.0), qPrintable(describe(boxAt(0).bounds())));
        QVERIFY2(near(rendered(0), target, 2.0), qPrintable(describe(rendered(0))));
        // Its animation carries the box with it.
        composition::ModelAnimationInstance slide; slide.name = "Slide";
        shot->layerRef(0).modelAnimations.append(slide);
        const QRectF moved = boxAt(24).bounds();
        QVERIFY2(moved.left() > target.left() + 100, qPrintable(describe(moved)));
        QVERIFY2(near(rendered(24), moved, 2.0), qPrintable(describe(rendered(24)) + " / " + describe(moved)));
    }
    void layoutBoxesFollowTheRenderedLayer() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString still = temp.filePath("red.png");
        QImage red(100, 50, QImage::Format_RGB32); red.fill(Qt::red);
        QVERIFY(red.save(still));
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importFile(still).isSuccess());
        const core::Identifier stillId = media->assetByFilePath(QFileInfo(still).absoluteFilePath()).id();
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(400, 200); shot->setDurationSeconds(1);
        {
            auto& layer = shot->addLayer("Still");
            composition::Clip clip; clip.mediaId = stillId; clip.durationSeconds = 1;
            layer.clips.append(clip);
            layer.transform.position = QPointF(50, 20);
            layer.transform.scalePercent = QPointF(200, 200);
        }
        const auto renderedRed = [&] {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0, QSize(400, 200));
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000)) return QRectF();
            const QByteArray bytes = frames.last()[1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 400, 200, 400 * 4,
                               QImage::Format_RGBA8888);
            QRect box;
            for (int y = 0; y < 200; ++y)
                for (int x = 0; x < 400; ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (c.red() > 200 && c.green() < 60) box = box.united(QRect(x, y, 1, 1));
                }
            return QRectF(box);
        };
        const auto geometryBox = [&] {
            return render::layerGeometry(*shot, shot->layers()[0], media.get(), 0).bounds();
        };
        // The picture is cut by the frame; the box is not.
        const auto near = [](const QRectF& a, QRectF b) {
            b = b.intersected(QRectF(0, 0, 400, 200));
            return qAbs(a.left() - b.left()) <= 2 && qAbs(a.top() - b.top()) <= 2
                   && qAbs(a.right() - b.right()) <= 2 && qAbs(a.bottom() - b.bottom()) <= 2;
        };
        const auto describe = [](const QRectF& r) {
            return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        };
        // 100x50 at 200 %, centred on (250, 80).
        QCOMPARE(geometryBox(), QRectF(150, 30, 200, 100));
        QVERIFY2(near(renderedRed(), geometryBox()), qPrintable(describe(renderedRed())));
        // A quarter turn about an anchor 20 px right of the centre.
        shot->layerRef(0).transform.rotationDegrees = 90;
        shot->layerRef(0).transform.anchorPoint = QPointF(20, 0);
        QVERIFY2(near(renderedRed(), geometryBox()),
                 qPrintable(describe(renderedRed()) + " / " + describe(geometryBox())));
        QCOMPARE(geometryBox().size(), QSizeF(100, 200));
        // Non-square footage is wider.
        media->assetByIdForEdit(stillId)->setPixelAspectOverride(true, composition::Composition::Anamorphic2To1);
        QCOMPARE(geometryBox().size(), QSizeF(100, 400));
        media->assetByIdForEdit(stillId)->setPixelAspectOverride(false, 0);

        // A box typed into the panel: the layer lands exactly on it, turned
        // or not.
        const QRectF target(20, 30, 60, 120);
        QPointF position, scale;
        QVERIFY(render::fitLayerToBox(*shot, shot->layers()[0], media.get(), 0, target, &position, &scale));
        shot->layerRef(0).transform.position = position;
        shot->layerRef(0).transform.scalePercent = scale;
        QVERIFY2(near(geometryBox(), target), qPrintable(describe(geometryBox())));
        QVERIFY2(near(renderedRed(), target), qPrintable(describe(renderedRed())));

        // A 3D sheet in front of the default camera at z = 0 shows as it
        // would in 2D.
        shot->layerRef(0).transform.rotationDegrees = 0;
        shot->layerRef(0).transform.anchorPoint = QPointF();
        shot->layerRef(0).transform.position = QPointF(50, 20);
        shot->layerRef(0).transform.scalePercent = QPointF(200, 200);
        shot->layerRef(0).dimension = composition::LayerDimension::ThreeD;
        const render::LayerGeometry sheet = render::layerGeometry(*shot, shot->layers()[0], media.get(), 0);
        QVERIFY(sheet.valid && sheet.inScene);
        QVERIFY2(near(sheet.bounds(), QRectF(150, 30, 200, 100)), qPrintable(describe(sheet.bounds())));
        QVERIFY2(near(renderedRed(), sheet.bounds()), qPrintable(describe(renderedRed())));
        // Moved through the panel, it follows on screen.
        QVERIFY(render::fitLayerToBox(*shot, shot->layers()[0], media.get(), 0,
                                      QRectF(50, 50, 200, 100), &position, &scale));
        shot->layerRef(0).transform.position = position;
        QVERIFY2(near(geometryBox(), QRectF(50, 50, 200, 100)), qPrintable(describe(geometryBox())));

        // Text: the glyph box TextTransformOverlay frames, fitted exactly.
        composition::TextStyle style; style.text = "Layout"; style.fontSize = 40;
        {
            auto& title = shot->addLayer("Title");
            title.kind = composition::LayerKind::Text;
            title.transform.position = QPointF(-60, 30);
            title.transform.rotationDegrees = 30;
            composition::Clip clip; clip.durationSeconds = 1;
            composition::Effect text; text.pluginId = core::Identifier("text");
            text.name = "text"; text.parameterValues = composition::textStyleToParameters(style);
            clip.effects.append(text); title.clips.append(clip);
        }
        const auto titleBox = [&] {
            return render::layerGeometry(*shot, shot->layers().last(), media.get(), 0).bounds();
        };
        QVERIFY(!titleBox().isEmpty());
        QVERIFY(render::fitLayerToBox(*shot, shot->layers().last(), media.get(), 0,
                                      QRectF(10, 10, 150, 90), &position, &scale));
        shot->layerRef(shot->layers().size() - 1).transform.position = position;
        shot->layerRef(shot->layers().size() - 1).transform.scalePercent = scale;
        // Turned by 30 degrees it scales evenly, so the box keeps its shape and
        // only its centre is exact.
        QVERIFY2((titleBox().center() - QPointF(85, 55)).manhattanLength() < 1.0,
                 qPrintable(describe(titleBox())));

        // Nothing to frame for a camera.
        auto& camera = shot->addLayer("Camera");
        camera.kind = composition::LayerKind::Camera;
        QVERIFY(!render::layerGeometry(*shot, shot->layers().last(), media.get(), 0).valid);
    }
    // Motion blur in a Half preview is drawn at half size with as many
    // shutter samples as the smear needs there, and looks like the full
    // frame reduced.
    void reducedPreviewBlursMotion() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(25, 1); shot->setDurationSeconds(2);
        auto& plane = shot->addLayer("Plane");
        plane.kind = composition::LayerKind::Plane; plane.planeColor = QColor(240, 220, 60);
        plane.transform.scalePercent = QPointF(25, 30);
        plane.transform.positionXCurve.set(0, -120.0); plane.transform.positionXCurve.set(10, 120.0);
        plane.transform.positionYCurve.set(0, 0.0); plane.transform.positionYCurve.set(10, 20.0);
        plane.transform.rotationCurve.set(0, 0.0); plane.transform.rotationCurve.set(10, 45.0);
        plane.motionBlur = true;
        composition::Clip fill; fill.durationSeconds = 2; plane.clips << fill;
        const auto frameAt = [&](const QSize& size) {
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(5, 5 / 25.0, size);
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 10000)) return QImage();
            const QByteArray bytes = frames.last()[1].toByteArray();
            const QSize got = frames.last()[2].toSize();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), got.width(), got.height(),
                          got.width() * 4, QImage::Format_RGBA8888).copy();
        };
        const QImage full = frameAt(QSize(320, 180));
        const QImage half = frameAt(QSize(160, 90));
        QVERIFY(!full.isNull() && !half.isNull());
        QCOMPARE(half.size(), QSize(160, 90));
        const QImage expected = full.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                                    .scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                    .convertToFormat(QImage::Format_RGBA8888);
        double sum = 0.0; int smeared = 0;
        for (int y = 0; y < 90; ++y)
            for (int x = 0; x < 160; ++x) {
                const QColor a = half.pixelColor(x, y), b = expected.pixelColor(x, y);
                sum += (qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue())) / 3.0;
                if (a.red() > 60 && a.red() < 200) ++smeared;
            }
        const double mean = sum / (160.0 * 90.0);
        half.save(testArtifactPath(QStringLiteral("half-motion-blur.png")));
        expected.save(testArtifactPath(QStringLiteral("half-motion-blur-expected.png")));
        // The smear is there - a sharp plane would have few mid tones.
        QVERIFY2(smeared > 300, qPrintable(QString::number(smeared)));
        QVERIFY2(mean < 4.0, qPrintable(QStringLiteral("mean %1").arg(mean)));
    }
    // The 3D scene of a Half preview is drawn at half size too, through the
    // full shot's camera: sheets turned in depth with fog, a model, and the
    // nearer covering the further, with square and 2:1 pixels - the same
    // picture as the full frame reduced.
    void reducedPreviewDrawsTheSceneReduced() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString still = temp.filePath("pattern.png");
        QImage pattern(160, 90, QImage::Format_RGB32);
        for (int y = 0; y < 90; ++y)
            for (int x = 0; x < 160; ++x)
                pattern.setPixelColor(x, y, QColor((x / 20 + y / 15) % 2 ? 220 : 40, x, 255 - y));
        QVERIFY(pattern.save(still));
        const QString cube = temp.filePath("cube.obj");
        {
            QFile file(cube); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream obj(&file);
            for (int i = 0; i < 8; ++i)
                obj << "v " << (i & 1 ? 40 : -40) << ' ' << (i & 2 ? 40 : -40) << ' ' << (i & 4 ? 40 : -40) << '\n';
            const int quads[6][4] = {{1, 2, 4, 3}, {5, 7, 8, 6}, {1, 5, 6, 2}, {3, 4, 8, 7}, {1, 3, 7, 5}, {2, 6, 8, 4}};
            for (const auto& q : quads)
                obj << "f " << q[0] << ' ' << q[1] << ' ' << q[2] << "\nf " << q[0] << ' ' << q[2] << ' ' << q[3] << '\n';
        }
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importFile(still).isSuccess());
        const core::Identifier stillId = media->assetByFilePath(QFileInfo(still).absoluteFilePath()).id();
        model3d::ImportSettings settings;
        settings.unit = model3d::ImportSettings::Unit::Pixel;
        settings.autoNormalize = false;
        QVERIFY(media->importModel(cube, settings).isSuccess());
        const core::Identifier cubeId = media->assetByFilePath(QFileInfo(cube).absoluteFilePath()).id();
        QVERIFY(!media->modelMesh(cubeId).isEmpty());

        for (const int aspect : {int(composition::Composition::SquarePixels),
                                 int(composition::Composition::Anamorphic2To1)}) {
            auto shot = std::make_shared<composition::Composition>();
            shot->setSize(320, 180); shot->setDurationSeconds(1);
            shot->setPixelAspect(aspect);
            shot->renderSettings().fogEnabled = true;
            shot->renderSettings().fogNearDistance = 200;
            shot->renderSettings().fogFarDistance = 1400;
            shot->renderSettings().fogColor = QColor(90, 110, 130);
            {
                auto& title = shot->addLayer("Title");   // 2D, over the scene
                title.kind = composition::LayerKind::Text;
                title.transform.position = QPointF(-90, 60);
                composition::TextStyle style; style.text = "3D"; style.fontSize = 30;
                composition::Clip clip; clip.durationSeconds = 1;
                composition::Effect text; text.pluginId = core::Identifier("text"); text.name = "text";
                text.parameterValues = composition::textStyleToParameters(style);
                clip.effects.append(text); title.clips.append(clip);
            }
            {
                auto& model = shot->addLayer("Cube");
                model.kind = composition::LayerKind::Model3D;
                model.dimension = composition::LayerDimension::ThreeD;
                model.modelAssetId = cubeId;
                model.transform.position = QPointF(10, 0);
                model.transform.positionZ = 60;
                model.transform.rotationYDegrees = 35;
                model.transform.rotationXDegrees = 25;
            }
            {
                auto& footage = shot->addLayer("Footage");
                footage.dimension = composition::LayerDimension::ThreeD;
                composition::Clip clip; clip.mediaId = stillId; clip.durationSeconds = 1;
                footage.clips.append(clip);
                footage.transform.position = QPointF(70, -25);
                footage.transform.rotationXDegrees = 35;
                footage.transform.rotationDegrees = 10;
            }
            {
                auto& plane = shot->addLayer("Plane");
                plane.kind = composition::LayerKind::Plane; plane.planeColor = QColor(230, 60, 40);
                plane.dimension = composition::LayerDimension::ThreeD;
                plane.transform.position = QPointF(-60, 15);
                plane.transform.positionZ = -300;
                plane.transform.scalePercent = QPointF(60, 50);
                plane.transform.rotationYDegrees = 45;
                plane.transform.rotationDegrees = 15;
                composition::Clip fill; fill.durationSeconds = 1; plane.clips.append(fill);
            }
            const auto frameAt = [&](const QSize& size, qint64* elapsed) {
                render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
                QSignalSpy frames(&manager, &render::RenderManager::frameReady);
                QElapsedTimer timer; timer.start();
                manager.requestFrame(0, 0, size);
                if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 10000)) return QImage();
                if (elapsed) *elapsed = timer.elapsed();
                const QByteArray bytes = frames.last()[1].toByteArray();
                const QSize got = frames.last()[2].toSize();
                return QImage(reinterpret_cast<const uchar*>(bytes.constData()), got.width(), got.height(),
                              got.width() * 4, QImage::Format_RGBA8888).copy();
            };
            qint64 fullTime = 0, halfTime = 0;
            const QImage full = frameAt(QSize(320, 180), &fullTime);
            const QImage half = frameAt(QSize(160, 90), &halfTime);
            QVERIFY(!full.isNull() && !half.isNull());
            QCOMPARE(half.size(), QSize(160, 90));
            const QImage expected = full.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                                        .scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                        .convertToFormat(QImage::Format_RGBA8888);
            double sum = 0.0; int large = 0;
            for (int y = 0; y < 90; ++y)
                for (int x = 0; x < 160; ++x) {
                    const QColor a = half.pixelColor(x, y), b = expected.pixelColor(x, y);
                    const int d = qMax(qMax(qAbs(a.red() - b.red()), qAbs(a.green() - b.green())),
                                       qAbs(a.blue() - b.blue()));
                    sum += d;
                    if (d > 64) ++large;
                }
            const double mean = sum / (160.0 * 90.0);
            half.save(testArtifactPath(QStringLiteral("half-scene-%1.png").arg(aspect)));
            expected.save(testArtifactPath(QStringLiteral("half-scene-expected-%1.png").arg(aspect)));
            QVERIFY2(mean < 4.0 && large < 160,
                     qPrintable(QStringLiteral("aspect %1: mean %2, large %3 (full %4 ms, half %5 ms)")
                                    .arg(aspect).arg(mean).arg(large).arg(fullTime).arg(halfTime)));
        }
    }
    // A Half preview is drawn at half size, not drawn full and thrown away -
    // and looks like the full frame reduced: footage turned about its anchor
    // with a built-in blur, a plane, text, a feathered mask and a nested shot,
    // with square and 2:1 pixels.
    void reducedPreviewMatchesTheFullFrame() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString still = temp.filePath("pattern.png");
        QImage pattern(160, 90, QImage::Format_RGB32);
        for (int y = 0; y < 90; ++y)
            for (int x = 0; x < 160; ++x)
                pattern.setPixelColor(x, y, QColor((x / 20 + y / 15) % 2 ? 220 : 40, x, 255 - y));
        QVERIFY(pattern.save(still));
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importFile(still).isSuccess());
        const core::Identifier stillId = media->assetByFilePath(QFileInfo(still).absoluteFilePath()).id();

        for (const int aspect : {int(composition::Composition::SquarePixels),
                                 int(composition::Composition::Anamorphic2To1)}) {
            auto shot = std::make_shared<composition::Composition>();
            shot->setSize(320, 180); shot->setDurationSeconds(1);
            shot->setPixelAspect(aspect);
            auto child = std::make_shared<composition::Composition>();
            child->setSize(100, 60); child->setDurationSeconds(1);
            {
                auto& plane = child->addLayer("Child plane");
                plane.kind = composition::LayerKind::Plane; plane.planeColor = QColor(30, 200, 90);
                plane.transform.scalePercent = QPointF(60, 60);
                plane.transform.rotationDegrees = 20;
                composition::Clip fill; fill.durationSeconds = 1; plane.clips.append(fill);
            }
            {
                auto& footage = shot->addLayer("Footage");
                composition::Clip clip; clip.mediaId = stillId; clip.durationSeconds = 1;
                composition::Effect blur; blur.pluginId = plugin::PluginId(plugin::kBuiltinBlur);
                blur.name = "Blur"; blur.parameterValues = {"6", "1", "Horizontal & Vertical", "true"};
                clip.effects.append(blur);
                footage.clips.append(clip);
                footage.transform.position = QPointF(-40, 10);
                footage.transform.scalePercent = QPointF(120, 120);
                footage.transform.rotationDegrees = 15;
                footage.transform.anchorPoint = QPointF(20, -10);
                composition::LayerMask mask; mask.shape = composition::MaskShape::Ellipse;
                mask.bounds = QRectF(40, 20, 200, 140); mask.feather = 8;
                footage.masks.append(mask);
            }
            {
                auto& plane = shot->addLayer("Plane");
                plane.kind = composition::LayerKind::Plane; plane.planeColor = QColor(240, 200, 30);
                plane.transform.position = QPointF(90, -40);
                plane.transform.scalePercent = QPointF(25, 20);
                plane.transform.rotationDegrees = -30;
                plane.opacity = 0.8;
                composition::Clip fill; fill.durationSeconds = 1; plane.clips.append(fill);
            }
            {
                auto& title = shot->addLayer("Title");
                title.kind = composition::LayerKind::Text;
                title.transform.position = QPointF(-80, -50);
                composition::TextStyle style; style.text = "Half"; style.fontSize = 36;
                composition::Clip clip; clip.durationSeconds = 1;
                composition::Effect text; text.pluginId = core::Identifier("text"); text.name = "text";
                text.parameterValues = composition::textStyleToParameters(style);
                clip.effects.append(text); title.clips.append(clip);
            }
            {
                auto& nested = shot->addLayer("Nested");
                composition::Clip clip; clip.nestedComposition = child; clip.durationSeconds = 1;
                nested.clips.append(clip);
                nested.transform.position = QPointF(100, 50);
            }
            const auto frameAt = [&](const QSize& size, qint64* elapsed) {
                render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
                QSignalSpy frames(&manager, &render::RenderManager::frameReady);
                QElapsedTimer timer; timer.start();
                manager.requestFrame(0, 0, size);
                if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 10000)) return QImage();
                if (elapsed) *elapsed = timer.elapsed();
                const QByteArray bytes = frames.last()[1].toByteArray();
                const QSize got = frames.last()[2].toSize();
                return QImage(reinterpret_cast<const uchar*>(bytes.constData()), got.width(), got.height(),
                              got.width() * 4, QImage::Format_RGBA8888).copy();
            };
            qint64 fullTime = 0, halfTime = 0;
            const QImage full = frameAt(QSize(320, 180), &fullTime);
            const QImage half = frameAt(QSize(160, 90), &halfTime);
            QVERIFY(!full.isNull() && !half.isNull());
            QCOMPARE(half.size(), QSize(160, 90));
            const QImage expected = full.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                                        .scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                        .convertToFormat(QImage::Format_RGBA8888);
            double sum = 0.0; int large = 0;
            for (int y = 0; y < 90; ++y)
                for (int x = 0; x < 160; ++x) {
                    const QColor a = half.pixelColor(x, y), b = expected.pixelColor(x, y);
                    const int d = qMax(qMax(qAbs(a.red() - b.red()), qAbs(a.green() - b.green())),
                                       qAbs(a.blue() - b.blue()));
                    sum += d;
                    if (d > 64) ++large;
                }
            const double mean = sum / (160.0 * 90.0);
            half.save(testArtifactPath(QStringLiteral("half-preview-%1.png").arg(aspect)));
            expected.save(testArtifactPath(QStringLiteral("half-expected-%1.png").arg(aspect)));
            // Edges of shapes and glyphs land half a pixel differently; the
            // picture is the same.
            QVERIFY2(mean < 4.0 && large < 160,
                     qPrintable(QStringLiteral("aspect %1: mean %2, large %3 (full %4 ms, half %5 ms)")
                                    .arg(aspect).arg(mean).arg(large).arg(fullTime).arg(halfTime)));
        }
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
    // The 3D scene is multisampled by the project's antialiasing mode, as
    // Flux's GL target is (4x/8x/16x/32x MSAA; CSAA keeps 4 or 8 colour
    // samples): a slanted edge gets partly covered pixels whose coverage
    // comes to the true area, a face's inside is shaded as before, a 3D
    // sheet's edges are smoothed where a 2D layer's are not, and the mode is
    // part of a 3D frame's cache key.
    // A model's diffuse map (map_Kd) is read with it and drawn on the faces
    // by their UVs - v up, as OBJ gives it - times the material's colour.
    void modelTexturesAreDrawn() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        // Red bottom-left, green bottom-right, blue top-left, white top-right
        // in the texture's own UVs; the image's first row is its top.
        QImage texture(2, 2, QImage::Format_RGBA8888);
        texture.setPixelColor(0, 1, Qt::red); texture.setPixelColor(1, 1, Qt::green);
        texture.setPixelColor(0, 0, Qt::blue); texture.setPixelColor(1, 0, Qt::white);
        QVERIFY(texture.save(dir.filePath("checker.png")));
        {
            QFile mtl(dir.filePath("quad.mtl")); QVERIFY(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
            mtl.write("newmtl tex\nKd 1 1 1\nKa 0 0 0\nKs 0 0 0\nmap_Kd checker.png\n");
        }
        {
            QFile obj(dir.filePath("quad.obj")); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            obj.write("mtllib quad.mtl\n"
                      "v -80 -80 0\nv 80 -80 0\nv 80 80 0\nv -80 80 0\n"
                      "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                      "usemtl tex\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(dir.filePath("quad.obj"), settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        QCOMPARE(loaded.mesh.materials.size(), 1);
        QVERIFY(!loaded.mesh.materials[0].diffuseImage.isNull());
        const QSize size(200, 200);
        const model3d::Lighting light;   // none: the texture as it is
        const QImage image = model3d::renderMesh(loaded.mesh, QMatrix4x4(), model3d::defaultCameraForCanvas(size),
                                                 size, light, model3d::ShadingMode::Shaded, 1.0);
        image.save(testArtifactPath("model-texture.png"));
        // The quad covers 20..180; the texels' centres are at a quarter and
        // three quarters of it (bilinear in between, and repeating at the edges).
        const auto dominant = [&](int x, int y) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() > 200 && c.green() > 200 && c.blue() > 200) return 'w';
            if (c.red() > 150 && c.green() < 100 && c.blue() < 100) return 'r';
            if (c.green() > 150 && c.red() < 100 && c.blue() < 100) return 'g';
            if (c.blue() > 150 && c.red() < 100 && c.green() < 100) return 'b';
            return '?';
        };
        QCOMPARE(dominant(60, 140), 'r');    // bottom-left on screen
        QCOMPARE(dominant(140, 140), 'g');
        QCOMPARE(dominant(60, 60), 'b');
        QCOMPARE(dominant(140, 60), 'w');
        // Flip UV Coordinates (the import setting) turns it upside down.
        settings.flipUVCoordinates = true;
        const auto flipped = model3d::loadModel(dir.filePath("quad.obj"), settings);
        const QImage upsideDown = model3d::renderMesh(flipped.mesh, QMatrix4x4(), model3d::defaultCameraForCanvas(size),
                                                      size, light, model3d::ShadingMode::Shaded, 1.0);
        const QColor top = upsideDown.pixelColor(60, 60);
        QVERIFY2(top.red() > 150 && top.green() < 100, qPrintable(top.name()));
    }
    // glTF 2.0, as .gltf (buffer and image in data: URIs) and as .glb (the
    // buffer in its binary chunk): the node hierarchy with its transforms,
    // the triangles, their UVs (v down in glTF) and the base colour texture.
    void gltfModelsAreRead() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QByteArray buffer;
        {
            QDataStream out(&buffer, QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);
            out.setFloatingPointPrecision(QDataStream::SinglePrecision);
            const float positions[] = {-80, -80, 0, 80, -80, 0, 80, 80, 0, -80, 80, 0};
            for (float v : positions) out << v;                                   // 48 bytes
            for (int i = 0; i < 4; ++i) out << 0.0f << 0.0f << 1.0f;             // normals, 48
            const float uvs[] = {0, 1, 1, 1, 1, 0, 0, 0};                         // v down
            for (float v : uvs) out << v;                                         // 32
            for (quint16 i : {0, 1, 2, 0, 2, 3}) out << i;                        // 12
        }
        QImage texture(2, 2, QImage::Format_RGBA8888);
        texture.setPixelColor(0, 1, Qt::red); texture.setPixelColor(1, 1, Qt::green);
        texture.setPixelColor(0, 0, Qt::blue); texture.setPixelColor(1, 0, Qt::white);
        QByteArray png;
        { QBuffer device(&png); device.open(QIODevice::WriteOnly); texture.save(&device, "PNG"); }
        const auto json = [&](bool binary) {
            QJsonObject bufferObject{{"byteLength", int(buffer.size())}};
            if (!binary) bufferObject["uri"] = QString("data:application/octet-stream;base64," + buffer.toBase64());
            const auto view = [](int offset, int length) {
                return QJsonObject{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
            };
            const auto accessor = [](int view, int type, int count, const char* kind) {
                return QJsonObject{{"bufferView", view}, {"componentType", type}, {"count", count}, {"type", kind}};
            };
            QJsonObject root{
                {"asset", QJsonObject{{"version", "2.0"}}},
                {"scene", 0},
                {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}},
                {"nodes", QJsonArray{
                    QJsonObject{{"name", "Parent"}, {"translation", QJsonArray{20, 0, 0}}, {"children", QJsonArray{1}}},
                    QJsonObject{{"name", "Quad"}, {"mesh", 0}}}},
                {"meshes", QJsonArray{QJsonObject{{"primitives", QJsonArray{QJsonObject{
                    {"attributes", QJsonObject{{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}},
                    {"indices", 3}, {"material", 0}}}}}}},
                {"materials", QJsonArray{QJsonObject{{"name", "Checker"}, {"pbrMetallicRoughness", QJsonObject{
                    {"baseColorFactor", QJsonArray{1, 1, 1, 1}}, {"metallicFactor", 0}, {"roughnessFactor", 1},
                    {"baseColorTexture", QJsonObject{{"index", 0}}}}}}}},
                {"textures", QJsonArray{QJsonObject{{"source", 0}}}},
                {"images", QJsonArray{QJsonObject{{"uri", QString("data:image/png;base64," + png.toBase64())}}}},
                {"buffers", QJsonArray{bufferObject}},
                {"bufferViews", QJsonArray{view(0, 48), view(48, 48), view(96, 32), view(128, 12)}},
                {"accessors", QJsonArray{accessor(0, 5126, 4, "VEC3"), accessor(1, 5126, 4, "VEC3"),
                                         accessor(2, 5126, 4, "VEC2"), accessor(3, 5123, 6, "SCALAR")}},
            };
            return QJsonDocument(root).toJson(QJsonDocument::Compact);
        };
        {
            QFile file(dir.filePath("quad.gltf")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(json(false));
        }
        {
            // GLB: header, a JSON chunk and a BIN chunk, each padded to four bytes.
            QByteArray text = json(true); while (text.size() % 4) text.append(' ');
            QByteArray bin = buffer; while (bin.size() % 4) bin.append('\0');
            QByteArray glb;
            QDataStream out(&glb, QIODevice::WriteOnly); out.setByteOrder(QDataStream::LittleEndian);
            out << quint32(0x46546C67) << quint32(2) << quint32(12 + 8 + text.size() + 8 + bin.size());
            out << quint32(text.size()) << quint32(0x4E4F534A); out.writeRawData(text.constData(), text.size());
            out << quint32(bin.size()) << quint32(0x004E4942); out.writeRawData(bin.constData(), bin.size());
            QFile file(dir.filePath("quad.glb")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(glb);
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        for (const QString name : {QString("quad.gltf"), QString("quad.glb")}) {
            const auto loaded = model3d::loadModel(dir.filePath(name), settings);
            QVERIFY2(loaded.ok, qPrintable(name + ": " + loaded.message));
            const model3d::Mesh& mesh = loaded.mesh;
            QCOMPARE(mesh.triangles.size(), 2);
            QCOMPARE(mesh.nodes.size(), 2);
            QCOMPARE(mesh.nodes[0].name, QString("Parent"));
            QCOMPARE(mesh.nodes[1].parent, 0);
            QCOMPARE(mesh.nodePath(1), QString("Parent/Quad"));
            QCOMPARE(mesh.triangles[0].node, 1);
            QVector3D low, high; QVERIFY(mesh.bounds(&low, &high));
            QCOMPARE(low.x(), -60.0f); QCOMPARE(high.x(), 100.0f);   // moved by its parent
            QCOMPARE(mesh.materials.size(), 1);
            QCOMPARE(mesh.materials[0].name, QString("Checker"));
            QVERIFY(!mesh.materials[0].diffuseImage.isNull());
            const QSize size(200, 200);
            const model3d::Lighting light;   // none: the texture as it is
            const QImage image = model3d::renderMesh(mesh, QMatrix4x4(), model3d::defaultCameraForCanvas(size),
                                                     size, light, model3d::ShadingMode::Shaded, 1.0);
            image.save(testArtifactPath(QStringLiteral("gltf-%1.png").arg(QFileInfo(name).suffix())));
            const auto dominant = [&](int x, int y) {
                const QColor c = image.pixelColor(x, y);
                if (c.red() > 200 && c.green() > 200 && c.blue() > 200) return 'w';
                if (c.red() > 150 && c.green() < 100 && c.blue() < 100) return 'r';
                if (c.green() > 150 && c.red() < 100 && c.blue() < 100) return 'g';
                if (c.blue() > 150 && c.red() < 100 && c.green() < 100) return 'b';
                return '?';
            };
            QCOMPARE(dominant(80, 140), 'r');
            QCOMPARE(dominant(160, 140), 'g');
            QCOMPARE(dominant(80, 60), 'b');
            QCOMPARE(dominant(160, 60), 'w');
        }
        // A file that is not glTF 2.0 says so.
        {
            QFile file(dir.filePath("old.gltf")); QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(R"({"asset":{"version":"1.0"}})");
        }
        const auto old = model3d::loadModel(dir.filePath("old.gltf"), settings);
        QVERIFY(!old.ok);
        QVERIFY(old.message.contains("1.0"));
    }
    // LightWave (.lwo, LWO2): a layer's quad with a UV map broken at one
    // corner (VMAD), its surface "Red" with 25 % transparency and a UV-mapped
    // colour image from a clip; LightWave's left-handed space comes out
    // mirrored, still facing the camera. Then an LWOB (LightWave 5) with a
    // detail polygon, read by Assimp's rules as the reference reads it.
    // Projected LightWave textures get the UVs Assimp's GenUVCoords gives
    // them (the reference reads with it): planar across the surface's
    // bounds, cylindrical round its axis and along it - a polygon across the
    // seam having its sides swapped (RemoveUVSeams), the wrap scaling it
    // about the middle (TransformUVCoords) - and an LWOB "Planar Image Map"
    // about its TFLG axis.
    void lightWaveProjectedTexturesGetUvs() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage texture(2, 2, QImage::Format_RGBA8888); texture.fill(Qt::green);
        QVERIFY(texture.save(dir.filePath("tex.png")));
        const auto bytes = [](const std::function<void(QDataStream&)>& write) {
            QByteArray out;
            QDataStream data(&out, QIODevice::WriteOnly);   // big-endian
            data.setFloatingPointPrecision(QDataStream::SinglePrecision);
            write(data);
            return out;
        };
        const auto chunk = [&](const char* id, const QByteArray& payload) {
            QByteArray out(id, 4);
            out += bytes([&](QDataStream& d) { d << quint32(payload.size()); }) + payload;
            if (payload.size() & 1) out += '\0';
            return out;
        };
        const auto sub = [&](const char* id, const QByteArray& payload) {
            QByteArray out(id, 4);
            out += bytes([&](QDataStream& d) { d << quint16(payload.size()); }) + payload;
            if (payload.size() & 1) out += '\0';
            return out;
        };
        const auto text = [](const char* value) {
            QByteArray out(value); out += '\0';
            if (out.size() & 1) out += '\0';
            return out;
        };
        const double pi = 3.14159265358979323846;
        // A plane (x -1..3, y 0..2) mapped planar along Z, and a hexagonal
        // tube round Y (radius 1, height 2) mapped cylindrically, twice round.
        QVector<QVector3D> points = {{-1, 0, 0}, {-1, 2, 0}, {3, 2, 0}, {3, 0, 0}};
        for (int i = 0; i < 6; ++i) {
            const double angle = qDegreesToRadians(30.0 + 60.0 * i);
            points.append(QVector3D(float(std::sin(angle)), 0.0f, float(std::cos(angle))));
            points.append(QVector3D(float(std::sin(angle)), 2.0f, float(std::cos(angle))));
        }
        QByteArray body = "LWO2";
        body += chunk("TAGS", text("Flat") + text("Tube"));
        body += chunk("LAYR", bytes([](QDataStream& d) { d << quint16(0) << quint16(0) << 0.f << 0.f << 0.f; })
                                  + text("Things"));
        body += chunk("PNTS", bytes([&](QDataStream& d) { for (const QVector3D& p : points) d << p.x() << p.y() << p.z(); }));
        body += chunk("POLS", QByteArray("FACE", 4) + bytes([](QDataStream& d) {
            d << quint16(4) << quint16(0) << quint16(1) << quint16(2) << quint16(3);
            for (int i = 0; i < 6; ++i) {
                const int a = 4 + 2 * i, b = 4 + 2 * ((i + 1) % 6);
                d << quint16(4) << quint16(a) << quint16(a + 1) << quint16(b + 1) << quint16(b);
            }
        }));
        body += chunk("PTAG", QByteArray("SURF", 4) + bytes([](QDataStream& d) {
            d << quint16(0) << quint16(0);
            for (int i = 1; i <= 6; ++i) d << quint16(i) << quint16(1);
        }));
        body += chunk("CLIP", bytes([](QDataStream& d) { d << quint32(1); }) + sub("STIL", text("tex.png")));
        const auto block = [&](quint16 projection, quint16 axis, float wrap) {
            return sub("IMAP", text("\x80") + sub("CHAN", "COLR"))
                   + sub("PROJ", bytes([&](QDataStream& d) { d << projection; }))
                   + sub("AXIS", bytes([&](QDataStream& d) { d << axis; }))
                   + sub("WRPW", bytes([&](QDataStream& d) { d << wrap << quint16(0); }))
                   + sub("IMAG", bytes([](QDataStream& d) { d << quint16(1); }));
        };
        body += chunk("SURF", text("Flat") + text("") + sub("BLOK", block(0, 2, 1.0f)));
        body += chunk("SURF", text("Tube") + text("") + sub("BLOK", block(1, 1, 2.0f)));
        {
            QFile out(dir.filePath("things.lwo")); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(chunk("FORM", body));
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(dir.filePath("things.lwo"), settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        const model3d::Mesh& mesh = loaded.mesh;
        QCOMPARE(mesh.materials.size(), 2);
        QVERIFY(!mesh.materials[0].diffuseImage.isNull() && !mesh.materials[1].diffuseImage.isNull());
        const auto near = [](const QVector2D& a, const QVector2D& b) { return (a - b).length() < 1e-4f; };
        int planar = 0;
        for (const model3d::Triangle& triangle : mesh.triangles) {
            if (triangle.material != 0) continue;
            for (int c = 0; c < 3; ++c) {
                QVERIFY(triangle.uv[c] >= 0);
                const QVector3D p = mesh.positions[triangle.position[c]];
                QVERIFY2(near(mesh.uvs[triangle.uv[c]], QVector2D((p.x() + 1) / 4, p.y() / 2)),
                         qPrintable(QStringLiteral("%1 %2").arg(mesh.uvs[triangle.uv[c]].x()).arg(mesh.uvs[triangle.uv[c]].y())));
                ++planar;
            }
        }
        QCOMPARE(planar, 6);
        // The tube: round Y in the mirrored space (z negated), along Y, then
        // twice round about the middle - but on the face across the seam
        // (angles 150 and -150 degrees there) the two sides trade places.
        int across = 0, round = 0;
        for (const model3d::Triangle& triangle : mesh.triangles) {
            if (triangle.material != 1) continue;
            QVector<float> raw;
            for (int c = 0; c < 3; ++c) {
                const QVector3D p = mesh.positions[triangle.position[c]];
                raw.append(float((std::atan2(p.x(), p.z()) + pi) / (2.0 * pi)));
            }
            const bool seam = std::any_of(raw.cbegin(), raw.cend(), [](float u) { return u < 0.1f; })
                              && std::any_of(raw.cbegin(), raw.cend(), [](float u) { return u > 0.9f; });
            for (int c = 0; c < 3; ++c) {
                const QVector3D p = mesh.positions[triangle.position[c]];
                float u = raw[c];
                if (seam) u = u > 0.9f ? 0.0f : 1.0f;
                const QVector2D expected(0.5f + 2.0f * (u - 0.5f), p.y() / 2);
                QVERIFY2(near(mesh.uvs[triangle.uv[c]], expected),
                         qPrintable(QStringLiteral("%1 %2 vs %3 %4").arg(mesh.uvs[triangle.uv[c]].x())
                                        .arg(mesh.uvs[triangle.uv[c]].y()).arg(expected.x()).arg(expected.y())));
                ++round;
            }
            across += seam ? 1 : 0;
        }
        QCOMPARE(round, 36);
        QCOMPARE(across, 2);

        // LWOB: a "Planar Image Map" about Y (TFLG 2) on a floor x 0..4, z 0..2.
        QByteArray lwob = "LWOB";
        lwob += chunk("PNTS", bytes([](QDataStream& d) {
            for (float v : {0.f, 0.f, 0.f, 4.f, 0.f, 0.f, 4.f, 0.f, 2.f, 0.f, 0.f, 2.f}) d << v;
        }));
        lwob += chunk("SRFS", text("Floor"));
        lwob += chunk("POLS", bytes([](QDataStream& d) {
            d << quint16(4) << quint16(0) << quint16(1) << quint16(2) << quint16(3) << qint16(1);
        }));
        lwob += chunk("SURF", text("Floor") + sub("CTEX", text("Planar Image Map")) + sub("TIMG", text("tex.png"))
                                 + sub("TFLG", bytes([](QDataStream& d) { d << quint16(2); })));
        {
            QFile out(dir.filePath("floor.lwo")); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(chunk("FORM", lwob));
        }
        const auto floor = model3d::loadModel(dir.filePath("floor.lwo"), settings);
        QVERIFY2(floor.ok, qPrintable(floor.message));
        QVERIFY(!floor.mesh.materials[0].diffuseImage.isNull());
        for (const model3d::Triangle& triangle : floor.mesh.triangles)
            for (int c = 0; c < 3; ++c) {
                const QVector3D p = floor.mesh.positions[triangle.position[c]];   // z mirrored: -2..0
                QVERIFY(near(floor.mesh.uvs[triangle.uv[c]], QVector2D(p.x() / 4, (p.z() + 2) / 2)));
            }
    }
    void lightWaveModelsAreRead() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath("images"));
        QImage texture(2, 2, QImage::Format_RGBA8888); texture.fill(Qt::green);
        QVERIFY(texture.save(dir.filePath("images/tex.png")));
        const auto bytes = [](const std::function<void(QDataStream&)>& write) {
            QByteArray out;
            QDataStream data(&out, QIODevice::WriteOnly);   // big-endian
            data.setFloatingPointPrecision(QDataStream::SinglePrecision);
            write(data);
            return out;
        };
        const auto chunk = [&](const char* id, const QByteArray& payload) {
            QByteArray out(id, 4);
            out += bytes([&](QDataStream& d) { d << quint32(payload.size()); }) + payload;
            if (payload.size() & 1) out += '\0';
            return out;
        };
        const auto sub = [&](const char* id, const QByteArray& payload) {
            QByteArray out(id, 4);
            out += bytes([&](QDataStream& d) { d << quint16(payload.size()); }) + payload;
            if (payload.size() & 1) out += '\0';
            return out;
        };
        const auto text = [](const char* value) {
            QByteArray out(value); out += '\0';
            if (out.size() & 1) out += '\0';
            return out;
        };
        QByteArray body = "LWO2";
        body += chunk("TAGS", text("Red"));
        body += chunk("LAYR", bytes([](QDataStream& d) { d << quint16(0) << quint16(0) << 0.f << 0.f << 0.f; })
                                  + text("Panel"));
        body += chunk("PNTS", bytes([](QDataStream& d) {
            for (float v : {-1.f, -1.f, 0.f, -1.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f, -1.f, 0.f}) d << v;
        }));
        body += chunk("VMAP", QByteArray("TXUV", 4) + bytes([](QDataStream& d) { d << quint16(2); }) + text("UV")
                                  + bytes([](QDataStream& d) {
            const float uvs[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
            for (quint16 i = 0; i < 4; ++i) d << i << uvs[i][0] << uvs[i][1];
        }));
        body += chunk("POLS", QByteArray("FACE", 4) + bytes([](QDataStream& d) {
            d << quint16(4) << quint16(0) << quint16(1) << quint16(2) << quint16(3);
        }));
        body += chunk("VMAD", QByteArray("TXUV", 4) + bytes([](QDataStream& d) { d << quint16(2); }) + text("UV")
                                  + bytes([](QDataStream& d) { d << quint16(2) << quint16(0) << 0.5f << 0.5f; }));
        body += chunk("PTAG", QByteArray("SURF", 4) + bytes([](QDataStream& d) { d << quint16(0) << quint16(0); }));
        body += chunk("CLIP", bytes([](QDataStream& d) { d << quint32(1); }) + sub("STIL", text("images/tex.png")));
        const QByteArray block = sub("IMAP", text("\x80") + sub("CHAN", "COLR"))
                                 + sub("PROJ", bytes([](QDataStream& d) { d << quint16(5); }))
                                 + sub("IMAG", bytes([](QDataStream& d) { d << quint16(1); }));
        body += chunk("SURF", text("Red") + text("")
            + sub("COLR", bytes([](QDataStream& d) { d << 1.f << 0.f << 0.f << quint16(0); }))
            + sub("DIFF", bytes([](QDataStream& d) { d << 1.f << quint16(0); }))
            + sub("TRAN", bytes([](QDataStream& d) { d << 0.25f << quint16(0); }))
            + sub("BLOK", block));
        {
            QFile out(dir.filePath("panel.lwo")); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(chunk("FORM", body));
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(dir.filePath("panel.lwo"), settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        const model3d::Mesh& mesh = loaded.mesh;
        QCOMPARE(mesh.triangles.size(), 2);
        QCOMPARE(mesh.nodeByPath("Panel"), 0);
        QCOMPARE(mesh.materials.size(), 1);
        QCOMPARE(mesh.materials[0].name, QString("Red"));
        QCOMPARE(mesh.materials[0].diffuseColor, QColor(255, 0, 0));
        QVERIFY(qAbs(mesh.materials[0].opacity - 0.75) < 1e-6);
        QVERIFY(!mesh.materials[0].diffuseImage.isNull());
        QCOMPARE(mesh.triangles[0].material, 0);
        // No specularity: no highlight and a shininess of 1, as the
        // reference reads an Assimp material without one.
        QCOMPARE(mesh.materials[0].specularColor, QColor(0, 0, 0));
        QCOMPARE(mesh.materials[0].shininess, 1.0);
        // Facing +z (towards the camera) after the mirror; faceted, the
        // surface having no smoothing angle.
        const model3d::Triangle& first = mesh.triangles[0];
        QVERIFY(first.normal[0] >= 0);
        QVERIFY(mesh.normals[first.normal[0]].z() > 0.9f);
        // Point 2's corner takes the polygon's own UV.
        bool broken = false;
        for (const model3d::Triangle& triangle : mesh.triangles)
            for (int c = 0; c < 3; ++c)
                if (triangle.position[c] == 2 && triangle.uv[c] >= 0)
                    broken = broken || mesh.uvs[triangle.uv[c]] == QVector2D(0.5f, 0.5f);
        QVERIFY(broken);
        QVERIFY(first.normal[0] == first.normal[1] && first.normal[1] == first.normal[2]);

        // LWOB: "Old" (orange at half diffuse, smoothed) on two faces 60
        // degrees apart sharing an edge, the first carrying a detail
        // triangle of "Shiny" (blue, all specular, glossiness 64, colour
        // highlights, full luminosity).
        QByteArray lwob = "LWOB";
        lwob += chunk("PNTS", bytes([](QDataStream& d) {
            for (float v : {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 5.f, 10.f, 0.f, 5.f, -5.f, 8.660254f,
                            20.f, 0.f, 0.f, 30.f, 0.f, 0.f, 25.f, 5.f, 0.f}) d << v;
        }));
        lwob += chunk("SRFS", text("Old") + text("Shiny"));
        lwob += chunk("POLS", bytes([](QDataStream& d) {
            d << quint16(3) << quint16(0) << quint16(1) << quint16(2) << qint16(-1) << quint16(1);   // detail follows
            d << quint16(3) << quint16(4) << quint16(5) << quint16(6) << qint16(2);
            d << quint16(3) << quint16(1) << quint16(0) << quint16(3) << qint16(1);
        }));
        lwob += chunk("SURF", text("Old")
            + sub("COLR", bytes([](QDataStream& d) { d << quint8(255) << quint8(128) << quint8(0) << quint8(0); }))
            + sub("DIFF", bytes([](QDataStream& d) { d << quint16(128); }))
            + sub("FLAG", bytes([](QDataStream& d) { d << quint16(0x4); })));
        lwob += chunk("SURF", text("Shiny")
            + sub("COLR", bytes([](QDataStream& d) { d << quint8(0) << quint8(0) << quint8(255) << quint8(0); }))
            + sub("SPEC", bytes([](QDataStream& d) { d << quint16(255); }))
            + sub("GLOS", bytes([](QDataStream& d) { d << quint16(64); }))
            + sub("LUMI", bytes([](QDataStream& d) { d << quint16(255); }))
            + sub("FLAG", bytes([](QDataStream& d) { d << quint16(0x8); })));
        {
            QFile out(dir.filePath("old.lwo")); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(chunk("FORM", lwob));
        }
        const auto old = model3d::loadModel(dir.filePath("old.lwo"), settings);
        QVERIFY2(old.ok, qPrintable(old.message));
        const model3d::Mesh& five = old.mesh;
        QCOMPARE(five.triangles.size(), 3);
        QCOMPARE(five.materials.size(), 2);
        QCOMPARE(five.triangles[0].material, 0);
        QCOMPARE(five.triangles[1].material, 1);   // the detail polygon
        QCOMPARE(five.triangles[2].material, 0);
        const model3d::Material& plain = five.materials[0];
        QCOMPARE(plain.diffuseColor.red(), 128);
        QCOMPARE(plain.diffuseColor.green(), 64);
        QCOMPARE(plain.specularColor, QColor(0, 0, 0));
        QCOMPARE(plain.shininess, 1.0);
        const model3d::Material& shiny = five.materials[1];
        QCOMPARE(shiny.diffuseColor, QColor(0, 0, 255));
        QCOMPARE(shiny.specularColor, QColor(0, 0, 255));   // highlights take the colour
        QCOMPARE(shiny.shininess, 20.0);                    // glossiness 64: Assimp's 20, times 1
        QCOMPARE(shiny.emissiveColor, QColor(204, 204, 204));
        // "Old" is smoothed across the shared edge; "Shiny" stays faceted.
        const auto normalAt = [&](int triangle, int position) {
            const model3d::Triangle& t = five.triangles[triangle];
            for (int c = 0; c < 3; ++c)
                if (t.position[c] == position) return five.normals.value(t.normal[c]);
            return QVector3D();
        };
        QVERIFY((normalAt(0, 0) - normalAt(2, 0)).length() < 1e-4f);
        QVERIFY(!normalAt(0, 0).isNull());
        const model3d::Triangle& detail = five.triangles[1];
        QVERIFY(detail.normal[0] == detail.normal[1] && detail.normal[1] == detail.normal[2]);
        // Not LightWave at all.
        {
            QFile out(dir.filePath("other.lwo")); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(chunk("FORM", "ILBM"));
        }
        const auto other = model3d::loadModel(dir.filePath("other.lwo"), settings);
        QVERIFY(!other.ok); QVERIFY(other.message.contains("not a LightWave object"));
        QVERIFY(model3d::canReadExtension("lwo"));
    }
    // 3D Studio (.3ds): a material with a 24-bit diffuse colour, 25 %
    // transparency and a texture named in capitals, and an object's quad
    // with texture coordinates and its faces' material. Then materials and
    // normals by Assimp's rules, as the reference reads them: shininess only
    // for Phong shading, the scene's ambient light, a default material for
    // faces without one, and smoothing groups.
    void threeDsModelsAreRead() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage checker(2, 2, QImage::Format_RGBA8888); checker.fill(Qt::green);
        QVERIFY(checker.save(dir.filePath("checker.png")));
        const auto chunk = [](quint16 id, const QByteArray& payload) {
            QByteArray out;
            QDataStream data(&out, QIODevice::WriteOnly);
            data.setByteOrder(QDataStream::LittleEndian);
            data << id << quint32(6 + payload.size());
            return out + payload;
        };
        const auto bytes = [](const std::function<void(QDataStream&)>& write) {
            QByteArray out;
            QDataStream data(&out, QIODevice::WriteOnly);
            data.setByteOrder(QDataStream::LittleEndian);
            data.setFloatingPointPrecision(QDataStream::SinglePrecision);
            write(data);
            return out;
        };
        const QByteArray material = chunk(0xAFFF,
            chunk(0xA000, QByteArray("RED", 4))
            + chunk(0xA020, chunk(0x0011, QByteArray("\xff\x00\x00", 3)))
            + chunk(0xA050, chunk(0x0030, bytes([](QDataStream& d) { d << quint16(25); })))
            + chunk(0xA200, chunk(0xA300, QByteArray("CHECKER.PNG", 12))));
        const QByteArray points = chunk(0x4110, bytes([](QDataStream& d) {
            d << quint16(4);
            for (float v : {-1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 1.f, 1.f, 0.f, -1.f, 1.f, 0.f}) d << v;
        }));
        const QByteArray coordinates = chunk(0x4140, bytes([](QDataStream& d) {
            d << quint16(4);
            for (float v : {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f}) d << v;
        }));
        const QByteArray faces = chunk(0x4120, bytes([](QDataStream& d) {
            d << quint16(2);
            for (quint16 v : {0, 1, 2, 7, 0, 2, 3, 7}) d << v;
        }) + chunk(0x4130, QByteArray("RED", 4) + bytes([](QDataStream& d) { d << quint16(2) << quint16(0) << quint16(1); })));
        const QByteArray object = chunk(0x4000, QByteArray("Box", 4) + chunk(0x4100, points + coordinates + faces));
        const QByteArray file = chunk(0x4D4D, chunk(0x0002, bytes([](QDataStream& d) { d << quint32(3); }))
                                                  + chunk(0x3D3D, material + object));
        {
            QFile out(dir.filePath("box.3ds")); QVERIFY(out.open(QIODevice::WriteOnly)); out.write(file);
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(dir.filePath("box.3ds"), settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        const model3d::Mesh& mesh = loaded.mesh;
        QCOMPARE(mesh.triangles.size(), 2);
        QCOMPARE(mesh.positions.size(), 4);
        QCOMPARE(mesh.uvs.size(), 4);
        QVERIFY(!mesh.normals.isEmpty());                  // made on import
        QCOMPARE(mesh.nodeByPath("Box"), 0);
        QCOMPARE(mesh.materials.size(), 1);
        QCOMPARE(mesh.materials[0].name, QString("RED"));
        QCOMPARE(mesh.materials[0].diffuseColor, QColor(255, 0, 0));
        QVERIFY(qAbs(mesh.materials[0].opacity - 0.75) < 1e-9);
        QVERIFY(!mesh.materials[0].diffuseImage.isNull()); // found though named in capitals
        QCOMPARE(mesh.triangles[0].material, 0);
        QCOMPARE(mesh.triangles[1].uv[2], 3);
        // Gouraud by default: no shininess, so no highlight.
        QCOMPARE(mesh.materials[0].shininess, 1.0);
        QCOMPARE(mesh.materials[0].specularColor, QColor(0, 0, 0));

        // Two roofs, each two faces 60 degrees apart on a shared edge: "Hard"
        // in smoothing groups 1 and 2, "Soft" in 1 and 3. "SHINY" is Phong
        // with 40 % shininess at 50 % strength, a grey specular and 30 %
        // self-illumination; the scene's ambient light is 0.2.
        const QByteArray shiny = chunk(0xAFFF,
            chunk(0xA000, QByteArray("SHINY", 6))
            + chunk(0xA030, chunk(0x0011, QByteArray(3, char(0x80))))
            + chunk(0xA040, chunk(0x0030, bytes([](QDataStream& d) { d << quint16(40); })))
            + chunk(0xA041, chunk(0x0030, bytes([](QDataStream& d) { d << quint16(50); })))
            + chunk(0xA084, chunk(0x0030, bytes([](QDataStream& d) { d << quint16(30); })))
            + chunk(0xA100, bytes([](QDataStream& d) { d << quint16(3); })));
        const QByteArray ambient = chunk(0x2100, chunk(0x0010, bytes([](QDataStream& d) { d << 0.2f << 0.2f << 0.2f; })));
        const auto roof = [&](const char* name, quint32 second, bool lit) {
            const QByteArray roofPoints = chunk(0x4110, bytes([](QDataStream& d) {
                d << quint16(4);
                for (float v : {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 5.f, 10.f, 0.f, 5.f, -5.f, 8.660254f}) d << v;
            }));
            QByteArray list = chunk(0x4150, bytes([&](QDataStream& d) { d << quint32(1) << second; }));
            if (lit) list += chunk(0x4130, QByteArray("SHINY", 6) + bytes([](QDataStream& d) { d << quint16(2) << quint16(0) << quint16(1); }));
            const QByteArray roofFaces = chunk(0x4120, bytes([](QDataStream& d) {
                d << quint16(2);
                for (quint16 v : {0, 1, 2, 7, 1, 0, 3, 7}) d << v;
            }) + list);
            return chunk(0x4000, QByteArray(name, int(qstrlen(name)) + 1) + chunk(0x4100, roofPoints + roofFaces));
        };
        // The keyframer nests them: a dummy "Rig" holds "Hard", which holds
        // "Soft" (parents by NODE_ID).
        const auto node = [&](quint16 id, const char* name, qint16 parent, const char* instance) {
            QByteArray header = QByteArray(name, int(qstrlen(name)) + 1)
                                + bytes([&](QDataStream& d) { d << quint16(0) << quint16(0) << parent; });
            QByteArray body = chunk(0xB030, bytes([&](QDataStream& d) { d << id; })) + chunk(0xB010, header);
            if (instance) body += chunk(0xB011, QByteArray(instance, int(qstrlen(instance)) + 1));
            return chunk(0xB002, body);
        };
        const QByteArray keyframer = chunk(0xB000, node(0, "$$$DUMMY", -1, "Rig") + node(1, "Hard", 0, nullptr)
                                                       + node(2, "Soft", 1, nullptr));
        const QByteArray roofs = chunk(0x4D4D, chunk(0x3D3D, ambient + material + shiny + roof("Hard", 2, true)
                                                             + roof("Soft", 3, false)) + keyframer);
        {
            QFile out(dir.filePath("roofs.3ds")); QVERIFY(out.open(QIODevice::WriteOnly)); out.write(roofs);
        }
        const auto both = model3d::loadModel(dir.filePath("roofs.3ds"), settings);
        QVERIFY2(both.ok, qPrintable(both.message));
        const model3d::Mesh& two = both.mesh;
        QCOMPARE(two.materials.size(), 3);
        const auto near = [](int a, int b) { return std::abs(a - b) <= 1; };
        const model3d::Material& lit = two.materials[1];
        QCOMPARE(lit.name, QString("SHINY"));
        QCOMPARE(lit.shininess, 20.0);                       // 40 x 0.5
        QCOMPARE(lit.specularColor, QColor(128, 128, 128));
        QVERIFY(near(lit.emissiveColor.red(), 77));
        QVERIFY(near(lit.ambientColor.red(), 51));            // none of its own, plus the scene's 0.2
        QVERIFY(near(lit.diffuseColor.red(), 153));           // Assimp's default diffuse, 0.6
        QVERIFY(near(two.materials[0].ambientColor.red(), 51));
        // "Soft" names no material: Assimp's default, a grey of 0.3.
        const model3d::Material& fallback = two.materials[2];
        QCOMPARE(fallback.name, QString("%%%DEFAULT"));
        QVERIFY(near(fallback.diffuseColor.red(), 77) && fallback.diffuseColor.red() == fallback.diffuseColor.blue());
        QCOMPARE(two.triangles[0].material, 1);
        QCOMPARE(two.triangles[2].material, 2);
        QCOMPARE(two.triangles[0].node, two.nodeByPath("Rig/Hard"));
        QCOMPARE(two.triangles[2].node, two.nodeByPath("Rig/Hard/Soft"));
        QVERIFY(two.nodeByPath("Rig") >= 0);
        QCOMPARE(two.positions[4], QVector3D(0, 0, 0));   // where the file puts it
        const auto normalAt = [&](int triangle, int position) {
            const model3d::Triangle& t = two.triangles[triangle];
            for (int c = 0; c < 3; ++c)
                if (t.position[c] == position) return two.normals.value(t.normal[c]);
            return QVector3D();
        };
        QVERIFY((normalAt(0, 0) - normalAt(1, 0)).length() > 0.5f);    // groups 1 and 2 share nothing
        QVERIFY((normalAt(2, 4) - normalAt(3, 4)).length() < 1e-4f);   // 1 and 3 share a bit
        // Not 3DS.
        {
            QFile out(dir.filePath("text.3ds")); QVERIFY(out.open(QIODevice::WriteOnly)); out.write("hello world");
        }
        const auto junk = model3d::loadModel(dir.filePath("text.3ds"), settings);
        QVERIFY(!junk.ok); QVERIFY(junk.message.contains("3DS"));
        QVERIFY(model3d::canReadExtension("3ds"));
    }
    // A skinned glTF mesh: "Root" and "Bone" (20 up) carry a strip whose
    // lower points follow the root and upper ones the bone; "Bend" turns the
    // bone a quarter about z, folding the top over - on the layer by the
    // animation's strength.
    void skinnedModelsFollowTheirJoints() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QByteArray buffer;
        {
            QDataStream out(&buffer, QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);
            out.setFloatingPointPrecision(QDataStream::SinglePrecision);
            for (float v : {-10.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 40.f, 0.f, -10.f, 40.f, 0.f}) out << v; // 0, 48
            for (quint16 i : {0, 1, 2, 0, 2, 3}) out << i;                                               // 48, 12
            for (quint16 j : {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}) out << j;                 // 60, 32
            for (float w : {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f}) out << w; // 92, 64
            // Inverse bind matrices, column-major: the root's identity, the
            // bone's a move 20 down.
            for (float m : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f,
                            1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, -20.f, 0.f, 1.f})
                out << m;                                                                                    // 156, 128
            for (float t : {0.f, 1.f}) out << t;                                                            // 284, 8
            const float half = float(std::sqrt(0.5));
            for (float q : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, half, half}) out << q;                            // 292, 32
        }
        const auto view = [](int offset, int length) {
            return QJsonObject{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
        };
        const auto accessor = [](int view, int type, int count, const char* kind) {
            return QJsonObject{{"bufferView", view}, {"componentType", type}, {"count", count}, {"type", kind}};
        };
        const QJsonObject gltf{
            {"asset", QJsonObject{{"version", "2.0"}}},
            {"scene", 0},
            {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0, 2}}}}},
            {"nodes", QJsonArray{QJsonObject{{"name", "Root"}, {"children", QJsonArray{1}}},
                                 QJsonObject{{"name", "Bone"}, {"translation", QJsonArray{0, 20, 0}}},
                                 QJsonObject{{"name", "Strip"}, {"mesh", 0}, {"skin", 0},
                                             {"translation", QJsonArray{500, 0, 0}}}}},   // ignored: skinned
            {"meshes", QJsonArray{QJsonObject{{"primitives", QJsonArray{QJsonObject{
                {"attributes", QJsonObject{{"POSITION", 0}, {"JOINTS_0", 2}, {"WEIGHTS_0", 3}}},
                {"indices", 1}}}}}}},
            {"skins", QJsonArray{QJsonObject{{"joints", QJsonArray{0, 1}}, {"inverseBindMatrices", 4}}}},
            {"animations", QJsonArray{QJsonObject{{"name", "Bend"},
                {"samplers", QJsonArray{QJsonObject{{"input", 5}, {"output", 6}}}},
                {"channels", QJsonArray{QJsonObject{{"sampler", 0},
                    {"target", QJsonObject{{"node", 1}, {"path", "rotation"}}}}}}}}},
            {"buffers", QJsonArray{QJsonObject{{"byteLength", int(buffer.size())},
                {"uri", QString("data:application/octet-stream;base64," + buffer.toBase64())}}}},
            {"bufferViews", QJsonArray{view(0, 48), view(48, 12), view(60, 32), view(92, 64), view(156, 128),
                                       view(284, 8), view(292, 32)}},
            {"accessors", QJsonArray{accessor(0, 5126, 4, "VEC3"), accessor(1, 5123, 6, "SCALAR"),
                                     accessor(2, 5123, 4, "VEC4"), accessor(3, 5126, 4, "VEC4"),
                                     accessor(4, 5126, 2, "MAT4"), accessor(5, 5126, 2, "SCALAR"),
                                     accessor(6, 5126, 2, "VEC4")}},
        };
        const QString file = dir.filePath("strip.gltf");
        {
            QFile out(file); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(QJsonDocument(gltf).toJson(QJsonDocument::Compact));
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(file, settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        const model3d::Mesh& mesh = loaded.mesh;
        QCOMPARE(mesh.skins.size(), 1);
        QCOMPARE(mesh.skins[0].joints.size(), 2);
        QCOMPARE(mesh.skins[0].joints[1], mesh.nodeByPath("Root/Bone"));
        QVERIFY(mesh.nodes[mesh.nodeByPath("Strip")].skinned);
        // At rest the joints undo their inverse binds: the strip as authored
        // (its node's own move does not count).
        QCOMPARE(mesh.positions[2], QVector3D(10, 40, 0));
        QCOMPARE(mesh.positions[0], QVector3D(-10, 0, 0));
        // Bent: the bone turned a quarter about z at (0, 20).
        const model3d::Mesh bent = mesh.skinned(mesh.animatedNodeWorlds(mesh.animations[0], 1.0));
        const auto close = [](const QVector3D& a, const QVector3D& b) { return (a - b).length() < 1e-3f; };
        QVERIFY2(close(bent.positions[2], QVector3D(-20, 30, 0)),
                 qPrintable(QString("%1 %2").arg(bent.positions[2].x()).arg(bent.positions[2].y())));
        QVERIFY(close(bent.positions[3], QVector3D(-20, 10, 0)));
        QVERIFY(close(bent.positions[0], QVector3D(-10, 0, 0)));
        // The node's own motion leaves skinned geometry alone.
        QVERIFY(mesh.animatedNodeMatrices(0, 1.0)[mesh.nodeByPath("Strip")].isIdentity());

        // On a layer, by the animation's strength; the import's scale carries.
        model3d::ImportSettings doubled = settings; doubled.singleUnitScale = 2.0;
        const auto big = model3d::loadModel(file, doubled);
        QVERIFY(big.ok);
        composition::Layer layer;
        layer.kind = composition::LayerKind::Model3D;
        composition::ModelAnimationInstance bend; bend.name = "Bend";
        layer.modelAnimations.append(bend);
        model3d::Mesh posed = render::posedModelMesh(layer, big.mesh, 24, 24.0, file);
        QVERIFY2(close(posed.positions[2], QVector3D(-40, 60, 0)),
                 qPrintable(QString("%1 %2").arg(posed.positions[2].x()).arg(posed.positions[2].y())));
        layer.modelAnimations[0].strength = 50.0;
        posed = render::posedModelMesh(layer, big.mesh, 24, 24.0, file);
        QVERIFY(close(posed.positions[2], QVector3D(-10, 70, 0)));   // halfway from (20, 80)
        QVERIFY(close(render::posedModelMesh(layer, big.mesh, 0, 24.0, file).positions[2], QVector3D(20, 80, 0)));
    }
    // glTF morph targets: a quad whose "Raise" target lifts its top edge 10,
    // resting at weight 0.5 (the mesh's weights) and keyed from 0 to 1 by
    // "Smile"; its node's move and the import's scale carry the motion.
    void morphTargetsFollowTheirWeights() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QByteArray buffer;
        {
            QDataStream out(&buffer, QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);
            out.setFloatingPointPrecision(QDataStream::SinglePrecision);
            for (float v : {-10.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 20.f, 0.f, -10.f, 20.f, 0.f}) out << v; // 0, 48
            for (quint16 i : {0, 1, 2, 0, 2, 3}) out << i;                                               // 48, 12
            for (float v : {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 0.f}) out << v;     // 60, 48
            for (float t : {0.f, 1.f}) out << t;                                                            // 108, 8
            for (float w : {0.f, 1.f}) out << w;                                                            // 116, 8
        }
        const auto view = [](int offset, int length) {
            return QJsonObject{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
        };
        const auto accessor = [](int view, int type, int count, const char* kind) {
            return QJsonObject{{"bufferView", view}, {"componentType", type}, {"count", count}, {"type", kind}};
        };
        const QJsonObject gltf{
            {"asset", QJsonObject{{"version", "2.0"}}},
            {"scene", 0},
            {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}},
            {"nodes", QJsonArray{QJsonObject{{"name", "Face"}, {"mesh", 0}, {"translation", QJsonArray{100, 0, 0}}}}},
            {"meshes", QJsonArray{QJsonObject{
                {"primitives", QJsonArray{QJsonObject{
                    {"attributes", QJsonObject{{"POSITION", 0}}}, {"indices", 1},
                    {"targets", QJsonArray{QJsonObject{{"POSITION", 2}}}}}}},
                {"weights", QJsonArray{0.5}},
                {"extras", QJsonObject{{"targetNames", QJsonArray{"Raise"}}}}}}},
            {"animations", QJsonArray{QJsonObject{{"name", "Smile"},
                {"samplers", QJsonArray{QJsonObject{{"input", 3}, {"output", 4}}}},
                {"channels", QJsonArray{QJsonObject{{"sampler", 0},
                    {"target", QJsonObject{{"node", 0}, {"path", "weights"}}}}}}}}},
            {"buffers", QJsonArray{QJsonObject{{"byteLength", int(buffer.size())},
                {"uri", QString("data:application/octet-stream;base64," + buffer.toBase64())}}}},
            {"bufferViews", QJsonArray{view(0, 48), view(48, 12), view(60, 48), view(108, 8), view(116, 8)}},
            {"accessors", QJsonArray{accessor(0, 5126, 4, "VEC3"), accessor(1, 5123, 6, "SCALAR"),
                                     accessor(2, 5126, 4, "VEC3"), accessor(3, 5126, 2, "SCALAR"),
                                     accessor(4, 5126, 2, "SCALAR")}},
        };
        const QString file = dir.filePath("face.gltf");
        {
            QFile out(file); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(QJsonDocument(gltf).toJson(QJsonDocument::Compact));
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(file, settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        const model3d::Mesh& mesh = loaded.mesh;
        QCOMPARE(mesh.morphs.size(), 1);
        QCOMPARE(mesh.morphs[0].targets.size(), 1);
        QCOMPARE(mesh.morphs[0].targets[0].name, QString("Raise"));
        QCOMPARE(mesh.morphs[0].targets[0].positions.size(), 2);
        QCOMPARE(mesh.morphs[0].defaultWeights, QVector<float>{0.5f});
        // At rest the mesh is at its weights: the top edge raised 5.
        QCOMPARE(mesh.positions[2], QVector3D(110, 25, 0));
        QCOMPARE(mesh.positions[0], QVector3D(90, 0, 0));
        QCOMPARE(mesh.animations.size(), 1);
        QCOMPARE(mesh.morphWeights(mesh.animations[0], 0.5), (QVector<QVector<float>>{{0.5f}}));
        const auto close = [](const QVector3D& a, const QVector3D& b) { return (a - b).length() < 1e-3f; };
        const model3d::Mesh full = mesh.deformed(mesh.restNodeWorlds(), mesh.morphWeights(mesh.animations[0], 1.0));
        QVERIFY2(close(full.positions[2], QVector3D(110, 30, 0)),
                 qPrintable(QString("%1 %2").arg(full.positions[2].x()).arg(full.positions[2].y())));
        QVERIFY(close(mesh.deformed(mesh.restNodeWorlds(), mesh.morphWeights(mesh.animations[0], 0.0)).positions[2],
                      QVector3D(110, 20, 0)));

        // On a layer: by Strength, twice the size.
        model3d::ImportSettings doubled = settings; doubled.singleUnitScale = 2.0;
        const auto big = model3d::loadModel(file, doubled);
        QVERIFY(big.ok);
        QCOMPARE(big.mesh.positions[2], QVector3D(220, 50, 0));
        composition::Layer layer;
        layer.kind = composition::LayerKind::Model3D;
        composition::ModelAnimationInstance smile; smile.name = "Smile";
        layer.modelAnimations.append(smile);
        QVERIFY(close(render::posedModelMesh(layer, big.mesh, 24, 24.0, file).positions[2], QVector3D(220, 60, 0)));
        layer.modelAnimations[0].strength = 50.0;
        QVERIFY(close(render::posedModelMesh(layer, big.mesh, 24, 24.0, file).positions[2], QVector3D(220, 55, 0)));
        QVERIFY(close(render::posedModelMesh(layer, big.mesh, 0, 24.0, file).positions[2], QVector3D(220, 45, 0)));
    }
    // An FBX skin: two clusters bind a strip's lower points to "Root" and
    // its upper ones to "Bone" (20 up); the bone's Lcl Rotation curve turns
    // it a quarter about z, folding the top over as in glTF.
    void fbxSkinsFollowTheirBones() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        using V = FbxValue;
        const QByteArray sep("\x00\x01", 2);
        const auto p70 = [](const char* name, QVector<double> numbers) {
            FbxRecord p{"P", {V::S(name), V::S(name), V::S(""), V::S("A")}, {}};
            for (double n : numbers) p.values.append(V::D(n));
            return p;
        };
        const auto array = [](const char* name, char type, QVector<double> values) {
            return FbxRecord{name, {V::A(type, values)}, {}};
        };
        const auto text = [](const char* name, const char* value) { return FbxRecord{name, {V::S(value)}, {}}; };
        const auto object = [](const char* kind, qint64 id, const QByteArray& name, const char* subclass,
                               QVector<FbxRecord> children) {
            return FbxRecord{kind, {V::L(id), V::S(name), V::S(subclass)}, children};
        };
        const auto link = [](const char* kind, qint64 child, qint64 parent, const char* property = nullptr) {
            FbxRecord c{"C", {V::S(kind), V::L(child), V::L(parent)}, {}};
            if (property) c.values.append(V::S(property));
            return c;
        };
        const QVector<double> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const QVector<double> up20{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 20, 0, 1};
        const QVector<FbxRecord> scene{
            FbxRecord{"FBXHeaderExtension", {}, {FbxRecord{"FBXVersion", {V::I(7400)}, {}}}},
            FbxRecord{"Objects", {}, {
                object("Model", 10, "Root" + sep + "Model", "LimbNode", {}),
                object("Model", 11, "Bone" + sep + "Model", "LimbNode",
                       {FbxRecord{"Properties70", {}, {p70("Lcl Translation", {0, 20, 0})}}}),
                object("Model", 12, "Strip" + sep + "Model", "Mesh",
                       {FbxRecord{"Properties70", {}, {p70("Lcl Translation", {500, 0, 0})}}}),
                object("Geometry", 20, "Strip" + sep + "Geometry", "Mesh", {
                    array("Vertices", 'd', {-10, 0, 0, 10, 0, 0, 10, 40, 0, -10, 40, 0}),
                    array("PolygonVertexIndex", 'i', {0, 1, 2, -4}),
                    FbxRecord{"LayerElementNormal", {V::I(0)}, {
                        text("MappingInformationType", "ByPolygonVertex"), text("ReferenceInformationType", "Direct"),
                        array("Normals", 'd', {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1})}}}),
                object("Deformer", 30, "Skin" + sep + "Deformer", "Skin", {}),
                object("Deformer", 31, "Lower" + sep + "SubDeformer", "Cluster", {
                    array("Indexes", 'i', {0, 1}), array("Weights", 'd', {1, 1}),
                    array("Transform", 'd', identity), array("TransformLink", 'd', identity)}),
                object("Deformer", 32, "Upper" + sep + "SubDeformer", "Cluster", {
                    array("Indexes", 'i', {2, 3}), array("Weights", 'd', {1, 1}),
                    array("Transform", 'd', identity), array("TransformLink", 'd', up20)}),
                object("AnimationStack", 40, "Bend" + sep + "AnimStack", "", {}),
                object("AnimationLayer", 41, "Base" + sep + "AnimLayer", "", {}),
                object("AnimationCurveNode", 42, "R" + sep + "AnimCurveNode", "", {}),
                object("AnimationCurve", 43, sep + "AnimCurve", "", {
                    array("KeyTime", 'l', {0, 46186158000.0}), array("KeyValueFloat", 'f', {0, 90})}),
            }},
            FbxRecord{"Connections", {}, {
                link("OO", 10, 0), link("OO", 11, 10), link("OO", 12, 0), link("OO", 20, 12),
                link("OO", 30, 20), link("OO", 31, 30), link("OO", 32, 30), link("OO", 10, 31), link("OO", 11, 32),
                link("OO", 41, 40), link("OO", 42, 41), link("OP", 42, 11, "Lcl Rotation"),
                link("OP", 43, 42, "d|Z")}},
        };
        const QString file = dir.filePath("strip.fbx");
        {
            QFile out(file); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(fbxBinary(scene, 7400, true));
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(file, settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        const model3d::Mesh& mesh = loaded.mesh;
        QCOMPARE(mesh.skins.size(), 1);
        QCOMPARE(mesh.skins[0].joints.size(), 2);
        QCOMPARE(mesh.skins[0].joints[1], mesh.nodeByPath("Root/Bone"));
        QVERIFY(mesh.nodes[mesh.nodeByPath("Strip")].skinned);
        const auto close = [](const QVector3D& a, const QVector3D& b) { return (a - b).length() < 1e-3f; };
        QVERIFY2(close(mesh.positions[2], QVector3D(10, 40, 0)),
                 qPrintable(QString("%1 %2").arg(mesh.positions[2].x()).arg(mesh.positions[2].y())));
        QCOMPARE(mesh.animations.size(), 1);
        const model3d::Mesh bent = mesh.skinned(mesh.animatedNodeWorlds(mesh.animations[0], 1.0));
        QVERIFY2(close(bent.positions[2], QVector3D(-20, 30, 0)),
                 qPrintable(QString("%1 %2").arg(bent.positions[2].x()).arg(bent.positions[2].y())));
        QVERIFY(close(bent.positions[0], QVector3D(-10, 0, 0)));
        // The normals turn with the bone too: the upper corners' stay along z.
        QVERIFY(qAbs(bent.normals[2].z() - 1.0f) < 1e-3f);
    }
    // FBX pivots and time: "Arm" turns about its RotationPivot and scales
    // about its ScalingPivot (both at x 10), as FbxNode::EvaluateGlobalTransform
    // has it; its rotation is keyed 0..90 over a second in a 30-frames-a-second
    // file (GlobalSettings TimeMode 6) whose stack ends at 2 seconds.
    void fbxPivotsAndTimeFollowTheReference() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        using V = FbxValue;
        const QByteArray sep("\x00\x01", 2);
        const auto p70 = [](const char* name, QVector<double> numbers) {
            FbxRecord p{"P", {V::S(name), V::S(name), V::S(""), V::S("A")}, {}};
            for (double n : numbers) p.values.append(V::D(n));
            return p;
        };
        const auto array = [](const char* name, char type, QVector<double> values) {
            return FbxRecord{name, {V::A(type, values)}, {}};
        };
        const auto object = [](const char* kind, qint64 id, const QByteArray& name, const char* subclass,
                               QVector<FbxRecord> children) {
            return FbxRecord{kind, {V::L(id), V::S(name), V::S(subclass)}, children};
        };
        const auto link = [](const char* kind, qint64 child, qint64 parent, const char* property = nullptr) {
            FbxRecord c{"C", {V::S(kind), V::L(child), V::L(parent)}, {}};
            if (property) c.values.append(V::S(property));
            return c;
        };
        const double second = 46186158000.0;
        const QVector<FbxRecord> scene{
            FbxRecord{"FBXHeaderExtension", {}, {FbxRecord{"FBXVersion", {V::I(7400)}, {}}}},
            FbxRecord{"GlobalSettings", {}, {FbxRecord{"Properties70", {}, {p70("TimeMode", {6})}}}},
            FbxRecord{"Objects", {}, {
                object("Model", 10, "Arm" + sep + "Model", "Mesh", {FbxRecord{"Properties70", {}, {
                    p70("Lcl Translation", {100, 0, 0}), p70("Lcl Scaling", {2, 2, 2}),
                    p70("RotationPivot", {10, 0, 0}), p70("ScalingPivot", {10, 0, 0})}}}),
                object("Geometry", 20, "Arm" + sep + "Geometry", "Mesh", {
                    array("Vertices", 'd', {10, 0, 0, 20, 0, 0, 20, 5, 0}),
                    array("PolygonVertexIndex", 'i', {0, 1, -3})}),
                object("AnimationStack", 40, "Swing" + sep + "AnimStack", "",
                       {FbxRecord{"Properties70", {}, {p70("LocalStop", {2 * second})}}}),
                object("AnimationLayer", 41, "Base" + sep + "AnimLayer", "", {}),
                object("AnimationCurveNode", 42, "R" + sep + "AnimCurveNode", "", {}),
                object("AnimationCurve", 43, sep + "AnimCurve", "", {
                    array("KeyTime", 'l', {0, second}), array("KeyValueFloat", 'f', {0, 90})}),
            }},
            FbxRecord{"Connections", {}, {
                link("OO", 10, 0), link("OO", 20, 10), link("OO", 41, 40), link("OO", 42, 41),
                link("OP", 42, 10, "Lcl Rotation"), link("OP", 43, 42, "d|Z")}},
        };
        const QString file = dir.filePath("arm.fbx");
        {
            QFile out(file); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(fbxBinary(scene, 7400, false));
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(file, settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        const model3d::Mesh& mesh = loaded.mesh;
        const auto close = [](const QVector3D& a, const QVector3D& b) { return (a - b).length() < 1e-3f; };
        // At rest: scaled 2 about x 10, so the pivot point stays and x 20 goes to 30.
        QVERIFY(close(mesh.positions[0], QVector3D(110, 0, 0)));
        QVERIFY2(close(mesh.positions[1], QVector3D(130, 0, 0)),
                 qPrintable(QString("%1 %2").arg(mesh.positions[1].x()).arg(mesh.positions[1].y())));
        QCOMPARE(mesh.animations.size(), 1);
        const model3d::ModelAnimation& clip = mesh.animations[0];
        QCOMPARE(clip.frameRate, 30.0);
        QCOMPARE(clip.duration, 2.0);
        QCOMPARE(clip.frameCount(), 60);
        QCOMPARE(mesh.animationFrameCount(0), 60);
        // Turned about the pivot: a quarter turn puts x 20 (now 30) above it.
        const int arm = mesh.nodeByPath("Arm");
        const QVector3D quarter = mesh.animatedNodeMatrices(0, 1.0)[arm].map(mesh.positions[1]);
        QVERIFY2(close(quarter, QVector3D(110, 20, 0)),
                 qPrintable(QString("%1 %2").arg(quarter.x()).arg(quarter.y())));
        QVERIFY(close(mesh.animatedNodeMatrices(0, 1.0)[arm].map(mesh.positions[0]), QVector3D(110, 0, 0)));
        const QVector3D eighth = mesh.animatedNodeMatrices(0, 0.5)[arm].map(mesh.positions[1]);
        const float r = 20.0f / std::sqrt(2.0f);
        QVERIFY2(close(eighth, QVector3D(110 + r, r, 0)),
                 qPrintable(QString("%1 %2").arg(eighth.x()).arg(eighth.y())));
        // Held after its last key, to the stack's end.
        QVERIFY(close(mesh.animatedNodeMatrices(0, 1.9)[arm].map(mesh.positions[1]), QVector3D(110, 20, 0)));
    }
    // FBX blend shapes: a quad whose "Raise" channel's shape lifts its top
    // edge 10 and tilts its normals, resting at DeformPercent 50 and keyed
    // from 0 to 100 by "Smile"; its model's place carries the motion.
    void fbxBlendShapesFollowTheirWeights() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        using V = FbxValue;
        const QByteArray sep("\x00\x01", 2);
        const auto p70 = [](const char* name, QVector<double> numbers) {
            FbxRecord p{"P", {V::S(name), V::S(name), V::S(""), V::S("A")}, {}};
            for (double n : numbers) p.values.append(V::D(n));
            return p;
        };
        const auto array = [](const char* name, char type, QVector<double> values) {
            return FbxRecord{name, {V::A(type, values)}, {}};
        };
        const auto text = [](const char* name, const char* value) { return FbxRecord{name, {V::S(value)}, {}}; };
        const auto object = [](const char* kind, qint64 id, const QByteArray& name, const char* subclass,
                               QVector<FbxRecord> children) {
            return FbxRecord{kind, {V::L(id), V::S(name), V::S(subclass)}, children};
        };
        const auto link = [](const char* kind, qint64 child, qint64 parent, const char* property = nullptr) {
            FbxRecord c{"C", {V::S(kind), V::L(child), V::L(parent)}, {}};
            if (property) c.values.append(V::S(property));
            return c;
        };
        const QVector<FbxRecord> scene{
            FbxRecord{"FBXHeaderExtension", {}, {FbxRecord{"FBXVersion", {V::I(7400)}, {}}}},
            FbxRecord{"Objects", {}, {
                object("Model", 10, "Face" + sep + "Model", "Mesh",
                       {FbxRecord{"Properties70", {}, {p70("Lcl Translation", {100, 0, 0})}}}),
                object("Geometry", 20, "Face" + sep + "Geometry", "Mesh", {
                    array("Vertices", 'd', {-10, 0, 0, 10, 0, 0, 10, 20, 0, -10, 20, 0}),
                    array("PolygonVertexIndex", 'i', {0, 1, 2, -4}),
                    FbxRecord{"LayerElementNormal", {V::I(0)}, {
                        text("MappingInformationType", "ByPolygonVertex"), text("ReferenceInformationType", "Direct"),
                        array("Normals", 'd', {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1})}}}),
                object("Deformer", 30, "Face" + sep + "Deformer", "BlendShape", {}),
                object("Deformer", 31, "Raise" + sep + "SubDeformer", "BlendShapeChannel",
                       {FbxRecord{"DeformPercent", {V::D(50)}, {}}}),
                object("Geometry", 32, "Raise" + sep + "Geometry", "Shape", {
                    array("Indexes", 'i', {2, 3}), array("Vertices", 'd', {0, 10, 0, 0, 10, 0}),
                    array("Normals", 'd', {2, 0, -1, 2, 0, -1})}),
                object("AnimationStack", 40, "Smile" + sep + "AnimStack", "", {}),
                object("AnimationLayer", 41, "Base" + sep + "AnimLayer", "", {}),
                object("AnimationCurveNode", 42, "DeformPercent" + sep + "AnimCurveNode", "", {}),
                object("AnimationCurve", 43, sep + "AnimCurve", "", {
                    array("KeyTime", 'l', {0, 46186158000.0}), array("KeyValueFloat", 'f', {0, 100})}),
            }},
            FbxRecord{"Connections", {}, {
                link("OO", 10, 0), link("OO", 20, 10), link("OO", 30, 20), link("OO", 31, 30), link("OO", 32, 31),
                link("OO", 41, 40), link("OO", 42, 41), link("OP", 42, 31, "DeformPercent"),
                link("OP", 43, 42, "d|DeformPercent")}},
        };
        const QString file = dir.filePath("face.fbx");
        {
            QFile out(file); QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(fbxBinary(scene, 7400, false));
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(file, settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        const model3d::Mesh& mesh = loaded.mesh;
        QCOMPARE(mesh.morphs.size(), 1);
        QCOMPARE(mesh.morphs[0].targets.size(), 1);
        QCOMPARE(mesh.morphs[0].targets[0].name, QString("Raise"));
        QCOMPARE(mesh.morphs[0].targets[0].positions.size(), 2);
        QCOMPARE(mesh.morphs[0].defaultWeights, QVector<float>{0.5f});
        const auto close = [](const QVector3D& a, const QVector3D& b) { return (a - b).length() < 1e-3f; };
        // At rest: the top edge raised 5, its normals tilted half way.
        QVERIFY2(close(mesh.positions[2], QVector3D(110, 25, 0)),
                 qPrintable(QString("%1 %2").arg(mesh.positions[2].x()).arg(mesh.positions[2].y())));
        QVERIFY(close(mesh.positions[0], QVector3D(90, 0, 0)));
        QVERIFY(close(mesh.normals[2], QVector3D(1, 0, 0.5f).normalized()));
        QVERIFY(close(mesh.normals[0], QVector3D(0, 0, 1)));
        QCOMPARE(mesh.animations.size(), 1);
        QCOMPARE(mesh.animations[0].name, QString("Smile"));
        const auto weights = mesh.morphWeights(mesh.animations[0], 0.5);
        QCOMPARE(weights.size(), 1);
        QVERIFY(qAbs(weights[0].value(0) - 0.5f) < 1e-3f);
        const model3d::Mesh full = mesh.deformed(mesh.restNodeWorlds(), mesh.morphWeights(mesh.animations[0], 1.0));
        QVERIFY2(close(full.positions[2], QVector3D(110, 30, 0)),
                 qPrintable(QString("%1 %2").arg(full.positions[2].x()).arg(full.positions[2].y())));
        QVERIFY(close(full.normals[2], QVector3D(2, 0, 0).normalized()));
        const model3d::Mesh none = mesh.deformed(mesh.restNodeWorlds(), mesh.morphWeights(mesh.animations[0], 0.0));
        QVERIFY(close(none.positions[2], QVector3D(110, 20, 0)));
        QVERIFY(close(none.normals[2], QVector3D(0, 0, 1)));

        // On a layer, by its Strength.
        composition::Layer layer;
        layer.kind = composition::LayerKind::Model3D;
        composition::ModelAnimationInstance smile; smile.name = "Smile";
        layer.modelAnimations.append(smile);
        QVERIFY(close(render::posedModelMesh(layer, mesh, 24, 24.0, file).positions[2], QVector3D(110, 30, 0)));
        layer.modelAnimations[0].strength = 50.0;
        QVERIFY(close(render::posedModelMesh(layer, mesh, 24, 24.0, file).positions[2], QVector3D(110, 27.5f, 0)));
    }
    // FBX, binary (7.4, 7.5 with deflated arrays) and ASCII: an "Arm" null
    // holding "Hand", whose quad is turned, scaled and coloured by a red
    // half-transparent material with a texture; the arm's translation is
    // animated in "Take 001".
    void fbxModelsAreRead() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage checker(2, 2, QImage::Format_RGBA8888); checker.fill(Qt::green);
        QVERIFY(checker.save(dir.filePath("checker.png")));
        using V = FbxValue;
        const auto p70 = [](const char* name, const char* type, QVector<double> numbers) {
            FbxRecord p{"P", {V::S(name), V::S(type), V::S(""), V::S("A")}, {}};
            for (double n : numbers) p.values.append(V::D(n));
            return p;
        };
        const auto text = [](const char* name, const char* value) { return FbxRecord{name, {V::S(value)}, {}}; };
        const auto array = [](const char* name, char type, QVector<double> values) {
            return FbxRecord{name, {V::A(type, values)}, {}};
        };
        const auto object = [](const char* kind, qint64 id, const QByteArray& name, const char* subclass,
                               QVector<FbxRecord> children) {
            return FbxRecord{kind, {V::L(id), V::S(name), V::S(subclass)}, children};
        };
        const auto link = [](const char* kind, qint64 child, qint64 parent, const char* property = nullptr) {
            FbxRecord c{"C", {V::S(kind), V::L(child), V::L(parent)}, {}};
            if (property) c.values.append(V::S(property));
            return c;
        };
        const QByteArray sep("\x00\x01", 2);
        const QVector<FbxRecord> scene{
            FbxRecord{"FBXHeaderExtension", {}, {FbxRecord{"FBXVersion", {V::I(7400)}, {}}}},
            FbxRecord{"Objects", {}, {
                object("Model", 100, "Arm" + sep + "Model", "Null",
                       {FbxRecord{"Properties70", {}, {p70("Lcl Translation", "Lcl Translation", {10, 0, 0})}}}),
                object("Model", 101, "Hand" + sep + "Model", "Mesh",
                       {FbxRecord{"Properties70", {}, {p70("Lcl Rotation", "Lcl Rotation", {0, 0, 90}),
                                                       p70("Lcl Scaling", "Lcl Scaling", {2, 2, 2})}}}),
                object("Geometry", 200, "Quad" + sep + "Geometry", "Mesh", {
                    array("Vertices", 'd', {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0}),
                    array("PolygonVertexIndex", 'i', {0, 1, 2, -4}),
                    FbxRecord{"LayerElementNormal", {V::I(0)}, {
                        text("MappingInformationType", "ByPolygonVertex"), text("ReferenceInformationType", "Direct"),
                        array("Normals", 'd', {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1})}},
                    FbxRecord{"LayerElementUV", {V::I(0)}, {
                        text("MappingInformationType", "ByPolygonVertex"), text("ReferenceInformationType", "IndexToDirect"),
                        array("UV", 'd', {0, 0, 1, 0, 1, 1, 0, 1}), array("UVIndex", 'i', {0, 1, 2, 3})}},
                    FbxRecord{"LayerElementMaterial", {V::I(0)}, {
                        text("MappingInformationType", "AllSame"), text("ReferenceInformationType", "IndexToDirect"),
                        array("Materials", 'i', {0})}}}),
                object("Material", 300, "Red" + sep + "Material", "",
                       {FbxRecord{"Properties70", {}, {p70("DiffuseColor", "Color", {1, 0, 0}),
                                                       p70("Opacity", "double", {0.5})}}}),
                object("Texture", 400, "Tex" + sep + "Texture", "", {text("RelativeFilename", "checker.png")}),
                object("AnimationStack", 500, "Take 001" + sep + "AnimStack", "", {}),
                object("AnimationLayer", 501, "BaseLayer" + sep + "AnimLayer", "", {}),
                object("AnimationCurveNode", 502, "T" + sep + "AnimCurveNode", "",
                       {FbxRecord{"Properties70", {}, {p70("d|X", "Number", {10}), p70("d|Y", "Number", {0}),
                                                       p70("d|Z", "Number", {0})}}}),
                object("AnimationCurve", 503, sep + "AnimCurve", "", {
                    array("KeyTime", 'l', {0, 46186158000.0}), array("KeyValueFloat", 'f', {10, 30})}),
            }},
            FbxRecord{"Connections", {}, {
                link("OO", 100, 0), link("OO", 101, 100), link("OO", 200, 101), link("OO", 300, 101),
                link("OP", 400, 300, "DiffuseColor"), link("OO", 501, 500), link("OO", 502, 501),
                link("OP", 502, 100, "Lcl Translation"), link("OP", 503, 502, "d|X")}},
        };
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto check = [&](const QString& name, const QByteArray& bytes) {
            const QString path = dir.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) return QString("cannot write");
            file.write(bytes); file.close();
            const auto loaded = model3d::loadModel(path, settings);
            if (!loaded.ok) return loaded.message;
            const model3d::Mesh& mesh = loaded.mesh;
            if (mesh.triangles.size() != 2) return QString("triangles %1").arg(mesh.triangles.size());
            const int hand = mesh.nodeByPath("Arm/Hand");
            if (hand < 0 || mesh.nodeByPath("Arm") < 0) return QString("nodes");
            QVector3D low, high;
            mesh.bounds(&low, &high);
            // Scaled 2, turned 90 degrees about z, moved 10 along x.
            if ((low - QVector3D(8, -2, 0)).length() > 1e-4f || (high - QVector3D(12, 2, 0)).length() > 1e-4f)
                return QString("bounds %1 %2 %3 .. %4 %5 %6").arg(low.x()).arg(low.y()).arg(low.z())
                    .arg(high.x()).arg(high.y()).arg(high.z());
            if (mesh.normals.size() != 4 || (mesh.normals[0] - QVector3D(0, 0, 1)).length() > 1e-4f)
                return QString("normals");
            if (mesh.uvs.size() != 4) return QString("uvs");
            if (mesh.materials.size() != 1) return QString("materials");
            const model3d::Material& material = mesh.materials[0];
            if (material.name != "Red" || material.diffuseColor != QColor(255, 0, 0)
                || qAbs(material.opacity - 0.5) > 1e-9 || material.diffuseImage.isNull())
                return QString("material");
            if (mesh.triangles[0].material != 0) return QString("triangle material");
            if (mesh.animations.size() != 1 || mesh.animations[0].name != "Take 001"
                || qAbs(mesh.animations[0].duration - 1.0) > 1e-9)
                return QString("animation");
            const QVector3D halfway = mesh.animatedNodeMatrices(0, 0.5)[hand].map(QVector3D());
            const QVector3D end = mesh.animatedNodeMatrices(0, 1.0)[hand].map(QVector3D());
            if ((halfway - QVector3D(10, 0, 0)).length() > 1e-3f || (end - QVector3D(20, 0, 0)).length() > 1e-3f)
                return QString("moved %1 / %2").arg(halfway.x()).arg(end.x());
            return QString();
        };
        QString problem = check("model74.fbx", fbxBinary(scene, 7400, false));
        QVERIFY2(problem.isEmpty(), qPrintable("7.4: " + problem));
        problem = check("model75.fbx", fbxBinary(scene, 7500, true));
        QVERIFY2(problem.isEmpty(), qPrintable("7.5: " + problem));
        const QByteArray ascii = "; FBX 7.4.0 project file\n" + fbxAscii(scene);
        problem = check("ascii.fbx", ascii);
        QVERIFY2(problem.isEmpty(), qPrintable("ASCII: " + problem + "\n" + QString::fromUtf8(ascii.left(600))));

        // Not FBX, or too old.
        {
            QFile file(dir.filePath("text.fbx")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("hello");
        }
        const auto junk = model3d::loadModel(dir.filePath("text.fbx"), settings);
        QVERIFY(!junk.ok); QVERIFY(junk.message.contains("FBX"));
        {
            QFile file(dir.filePath("old.fbx")); QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("FBXHeaderExtension: {\n\tFBXVersion: 6100\n}\n");
        }
        const auto old = model3d::loadModel(dir.filePath("old.fbx"), settings);
        QVERIFY(!old.ok); QVERIFY2(old.message.contains("6.1"), qPrintable(old.message));
        QVERIFY(model3d::canReadExtension("FBX"));
        QVERIFY(model3d::readableFormats().contains("fbx"));
    }
    // glTF animations, each on its own, played on a model layer the way the
    // reference's Model3DAnimationAssetInstance plays them: Strength, Loop,
    // Custom Playback / Playback, a frame rate of its own; children move
    // with their parents, and the import settings' scale and axes apply.
    void modelAnimationsPlayOnTheLayer() {
        using Channel = model3d::AnimationChannel;
        // Keys: linear, held, cubic spline, and turned along the shorter arc.
        Channel linear; linear.times = {0.0f, 2.0f};
        linear.values = {QVector4D(0, 0, 0, 0), QVector4D(20, 0, 0, 0)};
        QCOMPARE(linear.sample(1.0), QVector4D(10, 0, 0, 0));
        QCOMPARE(linear.sample(-1.0), QVector4D(0, 0, 0, 0));
        QCOMPARE(linear.sample(9.0), QVector4D(20, 0, 0, 0));
        Channel step = linear; step.interpolation = Channel::Interpolation::Step;
        QCOMPARE(step.sample(1.9), QVector4D(0, 0, 0, 0));
        Channel cubic = linear; cubic.interpolation = Channel::Interpolation::CubicSpline;
        cubic.values = {QVector4D(), QVector4D(0, 0, 0, 0), QVector4D(),
                        QVector4D(), QVector4D(20, 0, 0, 0), QVector4D()};
        QVERIFY(qAbs(cubic.sample(1.0).x() - 10.0f) < 1e-4f);      // flat tangents: an ease
        QVERIFY(qAbs(cubic.sample(0.5).x() - 3.125f) < 1e-4f);
        Channel turn; turn.path = Channel::Path::Rotation; turn.times = {0.0f, 1.0f};
        const float half = float(std::sqrt(0.5));
        turn.values = {QVector4D(0, 0, 0, 1), QVector4D(0, 0, half, half)};
        const QVector4D q = turn.sample(0.5);
        QVERIFY(qAbs(QQuaternion(q.w(), q.x(), q.y(), q.z()).toEulerAngles().z() - 45.0f) < 0.01f);

        // A file: "Arm" holds "Hand", a 40-unit quad; "Slide" moves the arm
        // 50 along x in a second, "Lift" 10 up y.
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString file = writeAnimatedArm(dir.path());
        QVERIFY(!file.isEmpty());
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(file, settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        const model3d::Mesh& mesh = loaded.mesh;
        QCOMPARE(mesh.animations.size(), 2);
        QCOMPARE(mesh.animations[0].name, QString("Slide"));
        QCOMPARE(mesh.animations[1].name, QString("Lift"));
        QCOMPARE(mesh.animations[0].duration, 1.0);
        QCOMPARE(mesh.animationFrameCount(0), 24);           // 24 frames a second, as the reference counts
        const int hand = mesh.nodeByPath("Arm/Hand");
        QVERIFY(hand >= 0);
        // The hand has no keys of its own and still goes with the arm.
        QCOMPARE(mesh.animatedNodeMatrices(0, 0.5)[hand].map(QVector3D(0, 0, 0)), QVector3D(25, 0, 0));
        QCOMPARE(mesh.animatedNodeMatrices(0, 4.0)[hand].map(QVector3D(0, 0, 0)), QVector3D(50, 0, 0));
        QVERIFY(mesh.animatedNodeMatrices(0, 0.0)[hand].isIdentity());
        QCOMPARE(mesh.animatedNodeMatrices(1, 1.0)[hand].map(QVector3D(0, 0, 0)), QVector3D(0, 10, 0));
        // Imported twice the size with Y and Z swapped (the reference's
        // (x, y, z) -> (-x, z, y)): the motion is too.
        model3d::ImportSettings turned = settings;
        turned.singleUnitScale = 2.0; turned.flipYZAxis = true;
        const auto big = model3d::loadModel(file, turned);
        QVERIFY(big.ok);
        const QVector3D lifted = big.mesh.animatedNodeMatrices(1, 1.0)[hand].map(QVector3D(0, 0, 0));
        QVERIFY2((lifted - QVector3D(0, 0, 20)).length() < 1e-3f, qPrintable(QString("%1 %2 %3")
                     .arg(lifted.x()).arg(lifted.y()).arg(lifted.z())));
        // Another file's animation plays on the nodes it shares.
        const auto source = model3d::animationSource(file);
        QVERIFY(source);
        const model3d::ModelAnimation moved = model3d::retargetAnimation(source->animations[0], *source, mesh);
        QCOMPARE(moved.channels.size(), 1);
        QCOMPARE(mesh.animatedNodeMatrices(moved, 1.0)[hand].map(QVector3D(0, 0, 0)), QVector3D(50, 0, 0));

        // Which of its frames shows: the layer's time at its own rate,
        // wrapped with Loop, or Playback with Custom Playback.
        composition::ModelAnimationInstance instance;
        QCOMPARE(instance.strength, 100.0);
        QVERIFY(!instance.loop && !instance.customPlayback);
        QCOMPARE(instance.animationFrame(12, 24.0, 24, 24.0), 12.0);
        QCOMPARE(instance.animationFrame(36, 24.0, 24, 24.0), 36.0);   // held at its end later
        QCOMPARE(instance.animationFrame(15, 30.0, 24, 24.0), 12.0);   // a 30 fps shot
        instance.loop = true;
        QCOMPARE(instance.animationFrame(36, 24.0, 24, 24.0), 13.0);   // fmod(36, 24 - 1)
        instance.frameRateOverridden = true; instance.frameRate = 48.0;
        QCOMPARE(instance.animationFrame(6, 24.0, 24, 24.0), 12.0);
        instance.frameRate = 29.97;
        QCOMPARE(instance.rate(24.0), 30000.0 / 1001.0);
        instance.customPlayback = true; instance.playback = 7.0;
        QCOMPARE(instance.animationFrame(100, 24.0, 24, 24.0), 7.0);
        instance.playbackCurve.set(0, 0.0); instance.playbackCurve.set(10, 20.0);
        QCOMPARE(instance.animationFrame(5, 24.0, 24, 24.0), 10.0);

        // On a layer: nothing moves without an instance; Slide at full
        // strength carries the quad right, half strength half as far.
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importModel(file, settings).isSuccess());
        const core::Identifier modelId = media->assetByFilePath(QFileInfo(file).absoluteFilePath()).id();
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 200); shot->setFrameRate(24, 1); shot->setDurationSeconds(2);
        {
            auto& model = shot->addLayer("Arm");
            model.kind = composition::LayerKind::Model3D; model.modelAssetId = modelId;
        }
        const auto centre = [&](int frame) {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(frame, frame / 24.0, QSize(200, 200));
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000)) return -1000.0;
            const QByteArray bytes = frames.last()[1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 200, 200, 200 * 4,
                               QImage::Format_RGBA8888);
            double sum = 0.0; int count = 0;
            for (int y = 0; y < 200; ++y)
                for (int x = 0; x < 200; ++x)
                    if (image.pixelColor(x, y).lightness() > 80) { sum += x; ++count; }
            return count ? sum / count : -1000.0;
        };
        const double rest = centre(24);
        QVERIFY(qAbs(rest - 100.0) < 2.0);
        composition::ModelAnimationInstance slide; slide.name = "Slide";
        shot->layerRef(0).modelAnimations.append(slide);
        QVERIFY(qAbs(centre(0) - rest) < 1.0);
        QVERIFY2(qAbs(centre(24) - (rest + 50.0)) < 2.0, qPrintable(QString::number(centre(24))));
        QVERIFY(qAbs(centre(12) - (rest + 25.0)) < 2.0);
        QVERIFY(qAbs(centre(40) - (rest + 50.0)) < 2.0);          // held at its end
        shot->layerRef(0).modelAnimations[0].strength = 50.0;
        QVERIFY(qAbs(centre(24) - (rest + 25.0)) < 2.0);
        shot->layerRef(0).modelAnimations[0].strength = 100.0;
        shot->layerRef(0).modelAnimations[0].loop = true;
        QVERIFY(qAbs(centre(35) - (rest + 50.0 * 12 / 24)) < 2.0);  // fmod(35, 23) = 12

        // Saved and read back as the reference writes them.
        auto& saved = shot->layerRef(0).modelAnimations[0];
        saved.strengthCurve.set(0, 100.0); saved.strengthCurve.set(24, 40.0);
        saved.customPlayback = true; saved.playback = 3.0;
        saved.frameRateOverridden = true; saved.frameRate = 30.0;
        const core::Identifier savedId = saved.id;   // `saved` moves with the next append
        composition::ModelAnimationInstance other; other.name = "Lift"; other.animationIndex = 1;
        other.filePath = file;
        shot->layerRef(0).modelAnimations.append(other);
        const QString project = dir.filePath("animated.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(project, *shot, *media).isSuccess());
        {
            QFile written(project); QVERIFY(written.open(QIODevice::ReadOnly));
            const QByteArray text = written.readAll();
            QVERIFY(text.contains("<Model3DAnimationAssetInstance Version=\"1\">"));
            QVERIFY(text.contains("<Name>customPlayback</Name>"));
            QVERIFY(text.contains("<AnimationIndex>1</AnimationIndex>"));
        }
        composition::Composition reloaded; media::MediaManager reloadedMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(project, &reloaded, &reloadedMedia).isSuccess());
        const auto& animations = reloaded.layers()[0].modelAnimations;
        QCOMPARE(animations.size(), 2);
        QCOMPARE(animations[0].id, savedId);
        QCOMPARE(animations[0].name, QString("Slide"));
        QVERIFY(animations[0].loop && animations[0].customPlayback);
        QCOMPARE(animations[0].playback, 3.0);
        QVERIFY(animations[0].frameRateOverridden);
        QCOMPARE(animations[0].frameRate, 30.0);
        QCOMPARE(animations[0].strengthAt(24), 40.0);
        QVERIFY(animations[0].filePath.isEmpty());
        QCOMPARE(animations[1].animationIndex, 1);
        QCOMPARE(QFileInfo(animations[1].filePath).absoluteFilePath(), QFileInfo(file).absoluteFilePath());
        QVERIFY(!animations[1].loop && !animations[1].customPlayback);
        QCOMPARE(animations[1].strength, 100.0);
    }
    // A shot keeps its Viewer (CompositionAsset/ViewerState Version 5): the
    // layout, the views in their places with their types and cameras, the
    // active one; a shot never shown gets the reference's one 2D view.
    void viewerStateSurvivesSave() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        composition::Composition shot; shot.setSize(320, 180); shot.setDurationSeconds(1);
        const QString blank = dir.filePath("blank.vegfx");
        media::MediaManager media;
        QVERIFY(project::VegfxSerializer::saveToFile(blank, shot, media).isSuccess());
        {
            QFile saved(blank); QVERIFY(saved.open(QIODevice::ReadOnly));
            const QString xml = QString::fromUtf8(saved.readAll());
            QVERIFY(xml.contains("<ViewerState Version=\"5\">"));
            QVERIFY(xml.contains("<CurrentLayout>0</CurrentLayout>"));
            QVERIFY(xml.contains("Type=\"0\""));
        }
        composition::ViewerSetup setup;
        setup.present = true; setup.layout = 4; setup.activeView = 2;
        const int types[] = {1, 7, 3, 2};
        for (int i = 0; i < 4; ++i) {
            composition::ViewerViewSetup view;
            view.type = types[i];
            const render::ViewCameraState state = render::defaultViewCameraState(render::ViewType(types[i]));
            view.cameraPosition = state.position; view.cameraRotation = state.rotation;
            view.cameraScale = i == 2 ? 2.5 : 1.0;
            setup.views.append(view);
        }
        shot.setViewerSetup(setup);
        const QString path = dir.filePath("views.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, shot, media).isSuccess());
        QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(saved.readAll());
        QVERIFY(xml.contains("<CurrentLayout>4</CurrentLayout>"));
        QVERIFY(xml.contains("<Layout4 Pos0=") || xml.contains("Pos3="));
        QVERIFY(xml.contains("Sp1=\"0.5\""));
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &reloaded).isSuccess());
        const composition::ViewerSetup back = loaded.viewerSetup();
        QVERIFY(back.present);
        QCOMPARE(back.layout, 4);
        QCOMPARE(back.activeView, 2);
        QCOMPARE(back.views.size(), 4);
        for (int i = 0; i < 4; ++i) QCOMPARE(back.views[i].type, types[i]);
        QCOMPARE(back.views[2].cameraScale, 2.5);
        QVERIFY((back.views[1].cameraPosition - QVector3D(0, 1000000, 0)).length() < 1.0f);
        QVERIFY(std::abs(back.views[1].cameraRotation.x() + 1.5707963f) < 1e-5f);
        QVERIFY(!back.views[0].id.isEmpty());
    }
    // Each Viewer view's camera menu (the reference's camera-menu button):
    // one button a view, Active Camera on the first and Top, Front and Right
    // on the others of four; choosing Perspective or Reset Current View is
    // reported, and a view with a camera of its own draws its own picture.
    void viewerCameraMenusChooseTheView() {
        ui::ViewerWidget viewer;
        viewer.resize(820, 460); viewer.show(); QTest::qWait(20);
        viewer.setProjectSize(QSize(160, 90));
        const auto button = [&](int view) { return viewer.findChild<QToolButton*>(QStringLiteral("viewerCameraMenu_%1").arg(view)); };
        QVERIFY(button(0) && button(3));
        QVERIFY(button(0)->isVisible() && !button(1)->isVisible());
        QCOMPARE(button(0)->text(), QString("Active Camera"));
        viewer.setLayout(ui::ViewerWidget::ViewerLayout::Four);
        QVERIFY(button(3)->isVisible());
        QCOMPARE(button(1)->text(), QString("Top"));
        QCOMPARE(button(2)->text(), QString("Front"));
        QCOMPARE(button(3)->text(), QString("Right"));
        QCOMPARE(button(1)->menu()->actions().size(), 11);   // 8 views, 2 separators, Reset

        QSignalSpy changed(&viewer, &ui::ViewerWidget::viewTypeChanged);
        QSignalSpy reset(&viewer, &ui::ViewerWidget::viewResetRequested);
        for (QAction* action : button(1)->menu()->actions())
            if (action->text() == "Perspective") action->trigger();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toInt(), 1);
        QCOMPARE(changed.first().at(1).toInt(), int(render::ViewType::Perspective));
        QCOMPARE(viewer.viewType(1), int(render::ViewType::Perspective));
        QCOMPARE(button(1)->text(), QString("Perspective"));
        viewer.findChild<QAction*>("viewerResetView_1")->trigger();
        QCOMPARE(reset.count(), 1);

        // The active camera view shows the shot's frame; the others their own.
        QImage shotFrame(160, 90, QImage::Format_RGBA8888); shotFrame.fill(Qt::red);
        QImage own(160, 90, QImage::Format_RGBA8888); own.fill(Qt::blue);
        viewer.setFrame(shotFrame);
        viewer.setViewFrame(0, own);   // ignored: Active Camera
        viewer.setViewFrame(1, own);
        const QImage grab = viewer.grab().toImage();
        const QPoint first = viewer.mapping(0).toWidget(QPointF(80, 45)).toPoint();
        const QPoint second = viewer.mapping(1).toWidget(QPointF(80, 45)).toPoint();
        const QColor a = grab.pixelColor(first), b = grab.pixelColor(second);
        QVERIFY2(a.red() > 200 && a.blue() < 60, qPrintable(a.name()));
        QVERIFY2(b.blue() > 200 && b.red() < 60, qPrintable(b.name()));
        QVERIFY(viewer.grab().save(testArtifactPath(QStringLiteral("viewer-camera-views.png"))));

        // The wheel over a view of its own camera moves that camera; over the
        // active camera view it zooms the picture as before.
        QSignalSpy zoomed(&viewer, &ui::ViewerWidget::viewZoomRequested);
        const auto wheel = [&](const QPoint& at) {
            QWheelEvent event(QPointF(at), viewer.mapToGlobal(QPointF(at)), QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&viewer, &event);
        };
        wheel(second);
        QCOMPARE(zoomed.count(), 1);
        QCOMPARE(zoomed.first().at(0).toInt(), 1);
        QVERIFY(zoomed.first().at(1).toDouble() > 0.0);
        const int before = viewer.zoomPercent();
        wheel(first);
        QCOMPARE(zoomed.count(), 1);
        QVERIFY(viewer.zoomPercent() != before);
    }
    // The Viewer's views (PanelRenderState::ViewType): a red 3D plane over a
    // green 2D one. The active camera sees both; Front, straight along Z,
    // sees the 3D scene alone at a pixel a unit (twice that at scale 2); Top
    // and Left see the plane edge on, Top with the floor grid under it; the
    // perspective view from (0, 100, 1000) sees the plane too.
    void viewerViewsSeeTheScene() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 100); shot->setDurationSeconds(1);
        {
            auto& plane = shot->addLayer("Red");
            plane.kind = composition::LayerKind::Plane; plane.planeColor = Qt::red;
            plane.dimension = composition::LayerDimension::ThreeD;
            plane.transform.scalePercent = QPointF(20, 20);
            composition::Clip clip; clip.durationSeconds = 1; plane.clips.append(clip);
        }
        {
            auto& ground = shot->addLayer("Green");
            ground.kind = composition::LayerKind::Plane; ground.planeColor = Qt::green;
            composition::Clip clip; clip.durationSeconds = 1; ground.clips.append(clip);
        }
        render::RenderManager manager; manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        const auto frameOf = [&](std::optional<render::ViewType> type, double scale = 1.0, bool floor = false) {
            if (type) {
                render::ViewCameraState state = render::defaultViewCameraState(*type);
                state.scale = scale;
                manager.setViewCamera(render::viewCamera(*type, state, shot.get(), QSize(200, 100), 0), floor);
            } else {
                manager.setViewCamera(std::nullopt);
            }
            frames.clear();
            manager.requestFrame(0, 0.0, QSize(200, 100));
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000)) return QImage();
            const QByteArray bytes = frames.last()[1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 200, 100, 200 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const auto red = [](const QImage& image, int x, int y) {
            const QColor c = image.pixelColor(x, y);
            return c.red() > 200 && c.green() < 60 && c.blue() < 60;
        };
        const auto green = [](const QImage& image, int x, int y) {
            const QColor c = image.pixelColor(x, y);
            return c.green() > 200 && c.red() < 60;
        };
        const auto count = [](const QImage& image, const std::function<bool(const QColor&)>& test) {
            int n = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) n += test(image.pixelColor(x, y)) ? 1 : 0;
            return n;
        };
        const QImage active = frameOf(std::nullopt);
        QVERIFY(!active.isNull());
        QVERIFY(red(active, 100, 50) && green(active, 5, 5));
        const QImage front = frameOf(render::ViewType::Front);
        QVERIFY(!front.isNull());
        QVERIFY(red(front, 100, 50) && red(front, 115, 50) && !red(front, 125, 50));   // 40 wide
        QVERIFY(!green(front, 5, 5));                                                  // no 2D layers
        const QImage closer = frameOf(render::ViewType::Front, 2.0);
        QVERIFY(red(closer, 135, 50) && !red(closer, 145, 50));                         // 80 wide
        const QImage top = frameOf(render::ViewType::Top, 1.0, true);
        QVERIFY(!top.isNull());
        QVERIFY(count(top, [](const QColor& c) { return c.red() > 200 && c.green() < 60; }) <= 200 * 2);
        QVERIFY(count(top, [](const QColor& c) { return c.red() > 60 && c.green() > 60 && c.blue() > 60; }) > 50);
        const QImage left = frameOf(render::ViewType::Left);
        QVERIFY(count(left, [](const QColor& c) { return c.red() > 200 && c.green() < 60; }) <= 100 * 2);
        // From 1000 away and 100 up, through the view's 60-degree lens, the
        // 40 x 20 plane is a few pixels across, below the middle.
        const QImage perspective = frameOf(render::ViewType::Perspective);
        const auto reddish = [](const QColor& c) { return c.red() > 100 && c.green() < 80 && c.blue() < 80; };
        QVERIFY(count(perspective, reddish) >= 1);
        int lowest = -1;
        for (int y = 0; y < perspective.height(); ++y)
            for (int x = 0; x < perspective.width(); ++x)
                if (reddish(perspective.pixelColor(x, y))) lowest = y;
        QVERIFY2(lowest > 52, qPrintable(QString::number(lowest)));
        QVERIFY(!green(perspective, 5, 5));

        // Where the axis views put points: Top has the back of the scene up,
        // Left has its front to the right; Bottom looks up with the back up
        // too, so its +X is on the left.
        const auto screen = [&](render::ViewType type, const QVector3D& point) {
            const model3d::Camera camera = render::viewCamera(type, render::defaultViewCameraState(type),
                                                              shot.get(), QSize(200, 100), 0);
            const QVector4D clip = camera.viewProjection(QSize(200, 100)) * QVector4D(point, 1.0f);
            return QPointF(clip.x() / clip.w(), clip.y() / clip.w());
        };
        QVERIFY(screen(render::ViewType::Top, QVector3D(0, 0, -50)).y() > 0.5);
        QVERIFY(screen(render::ViewType::Top, QVector3D(50, 0, 0)).x() > 0.4);
        QVERIFY(screen(render::ViewType::Left, QVector3D(0, 0, 50)).x() > 0.4);
        QVERIFY(screen(render::ViewType::Right, QVector3D(0, 0, 50)).x() < -0.4);
        QVERIFY(screen(render::ViewType::Back, QVector3D(50, 0, 0)).x() < -0.4);
        QVERIFY(screen(render::ViewType::Bottom, QVector3D(50, 0, 0)).x() < -0.4);
        QVERIFY(screen(render::ViewType::Bottom, QVector3D(0, 0, -50)).y() > 0.5);
        const model3d::Camera bottom = render::viewCamera(render::ViewType::Bottom,
            render::defaultViewCameraState(render::ViewType::Bottom), shot.get(), QSize(200, 100), 0);
        QVERIFY((bottom.target - bottom.position).y() > 0.99f);   // looking up
        QVERIFY(std::abs(screen(render::ViewType::Front, QVector3D(0, 25, 0)).y() - 0.5) < 1e-3);
        QVERIFY(!render::viewHasOwnCamera(render::ViewType::ActiveCamera));
        QVERIFY(render::viewHasOwnCamera(render::ViewType::Perspective));

        // Moving a view's camera: the wheel scales an axis view by 1.25 and
        // dollies the perspective one; a pan follows the pointer (fewer units
        // a pixel when zoomed in); an orbit keeps the perspective camera on
        // the origin at its distance.
        using render::ViewType;
        const render::ViewCameraState frontState = render::defaultViewCameraState(ViewType::Front);
        QVERIFY(std::abs(render::zoomedView(ViewType::Front, frontState, 1).scale - 1.1) < 1e-9);
        QVERIFY(std::abs(render::zoomedView(ViewType::Front, frontState, -1).scale - 0.9) < 1e-9);
        QCOMPARE(render::zoomedView(ViewType::Front, frontState, -20).scale, 0.0002);
        // About the pointer: the point 100 pixels right of the middle stays.
        const render::ViewCameraState anchored =
            render::zoomedView(ViewType::Front, frontState, 10, QPointF(100, 0));   // scale 2
        QVERIFY(std::abs(anchored.scale - 2.0) < 1e-9);
        QVERIFY(std::abs(anchored.position.x() - 50.0f) < 1e-3f);   // 100 at scale 1 = 50 + 100 / 2
        QVERIFY(std::abs(render::pannedView(ViewType::Front, frontState, QPointF(10, 0)).position.x() + 10.0f) < 1e-3f);
        render::ViewCameraState zoomedIn = frontState; zoomedIn.scale = 2.0;
        QVERIFY(std::abs(render::pannedView(ViewType::Front, zoomedIn, QPointF(10, 0)).position.x() + 5.0f) < 1e-3f);
        const render::ViewCameraState topPanned =
            render::pannedView(ViewType::Top, render::defaultViewCameraState(ViewType::Top), QPointF(0, 10));
        QVERIFY(std::abs(topPanned.position.z() + 10.0f) < 1e-2f);
        render::ViewCameraState eye; eye.position = QVector3D(0, 0, 1000);
        QVERIFY(std::abs(render::zoomedView(ViewType::Perspective, eye, 1).position.z() - 880.0f) < 1e-2f);
        QCOMPARE(render::viewCamera(ViewType::Perspective, eye, nullptr, QSize(200, 100), 0).fieldOfViewDegrees, 60.0);
        // Orbit: 0.3 degrees a pixel about the world's up and the level side
        // axis, the camera still on the origin at its distance, upright.
        const render::ViewCameraState orbited = render::orbitedView(ViewType::Perspective, eye, QPointF(90, 30));
        QVERIFY(std::abs(orbited.position.length() - 1000.0f) < 1e-2f);
        const QVector3D looking = render::viewCameraAim(orbited).map(QVector3D(0, 0, -1));
        QVERIFY(QVector3D::dotProduct(looking, -orbited.position.normalized()) > 0.9999f);
        QVERIFY(std::abs(render::viewCameraAim(orbited).map(QVector3D(1, 0, 0)).y()) < 1e-4f);   // no roll
        QVERIFY(orbited.position.y() > 0.0f && orbited.position.x() < 0.0f);   // dragged down and right
        const float across = std::atan2(-orbited.position.x(), orbited.position.z());
        QVERIFY(std::abs(across - 90 * 0.0052359878f) < 1e-4f);
        QCOMPARE(render::orbitedView(ViewType::Top, frontState, QPointF(50, 0)).rotation, frontState.rotation);
        // About another pivot: its distance to it, and how it looks past it, kept.
        const QVector3D pivot(100, 0, 0);
        const render::ViewCameraState aside = render::orbitedView(ViewType::Perspective, eye, QPointF(40, -20), pivot);
        QVERIFY(std::abs((aside.position - pivot).length() - (eye.position - pivot).length()) < 1e-2f);
        const auto offAxis = [&](const render::ViewCameraState& state) {
            return QVector3D::dotProduct(render::viewCameraAim(state).map(QVector3D(0, 0, -1)),
                                         (pivot - state.position).normalized());
        };
        QVERIFY(std::abs(offAxis(aside) - offAxis(eye)) < 1e-4f);
        // Never over the top.
        render::ViewCameraState high = eye;
        for (int i = 0; i < 20; ++i) high = render::orbitedView(ViewType::Perspective, high, QPointF(0, 30));
        QVERIFY(high.position.z() > 0.0f && high.position.y() > 950.0f);
        // Dolly: a unit a pixel of an upward drag forward; an axis view zooms.
        QVERIFY(std::abs(render::dolliedView(ViewType::Perspective, eye, QPointF(5, -10)).position.z() - 990.0f) < 1e-3f);
        QVERIFY(std::abs(render::dolliedView(ViewType::Front, frontState, QPointF(0, -100)).scale - 1.1) < 1e-9);
        // Rotate Camera: 0.1 degrees a pixel where it stands.
        const render::ViewCameraState turned = render::turnedView(ViewType::Perspective, eye, QPointF(10, 0));
        QCOMPARE(turned.position, eye.position);
        QVERIFY(std::abs(turned.rotation.y() - qDegreesToRadians(1.0f)) < 1e-5f);
        QCOMPARE(render::turnedView(ViewType::Front, frontState, QPointF(10, 0)).rotation, frontState.rotation);
        // Zoom Camera: the perspective view's scale narrows its field of view.
        const render::ViewCameraState nearer = render::dragZoomedView(ViewType::Perspective, eye, QPointF(0, 100));
        QVERIFY(std::abs(nearer.scale - 1.1) < 1e-9);
        QVERIFY(std::abs(render::viewCamera(ViewType::Perspective, nearer, nullptr, QSize(200, 100), 0).fieldOfViewDegrees
                         - qRadiansToDegrees(2.0 * std::atan(std::tan(qDegreesToRadians(30.0)) / 1.1))) < 1e-9);
        QVERIFY(std::abs(render::dragZoomedView(ViewType::Front, frontState, QPointF(0, 100)).scale - 0.9) < 1e-9);
        QCOMPARE(render::dragZoomedView(ViewType::Front, frontState, QPointF(0, 5000)).scale, 0.0002);
        // The buttons and keys, as the reference's OpenGLView picks its modes.
        using ui::ViewDrag;
        QCOMPARE(ui::viewDragFor(Qt::RightButton, Qt::NoModifier), ViewDrag::Pan);
        QCOMPARE(ui::viewDragFor(Qt::MiddleButton, Qt::ShiftModifier), ViewDrag::Rotate);
        QCOMPARE(ui::viewDragFor(Qt::LeftButton, Qt::NoModifier), ViewDrag::None);
        QCOMPARE(ui::viewDragFor(Qt::LeftButton, Qt::ControlModifier), ViewDrag::Pan);
        QCOMPARE(ui::viewDragFor(Qt::RightButton, Qt::ControlModifier), ViewDrag::Zoom);
        QCOMPARE(ui::viewDragFor(Qt::LeftButton, Qt::AltModifier), ViewDrag::Orbit);
        QCOMPARE(ui::viewDragFor(Qt::MiddleButton, Qt::AltModifier), ViewDrag::Pan);
        QCOMPARE(ui::viewDragFor(Qt::RightButton, Qt::AltModifier), ViewDrag::Dolly);
        // In the Viewer: a middle drag over a perspective view turns its
        // camera; over the shot's own view nothing moves.
        ui::ViewerWidget navigated; navigated.resize(400, 260); navigated.show();
        QVERIFY(QTest::qWaitForWindowExposed(&navigated));
        navigated.setProjectSize(QSize(320, 180));
        navigated.setViewType(0, int(ViewType::Perspective));
        QImage picture(320, 180, QImage::Format_ARGB32_Premultiplied); picture.fill(Qt::black);
        navigated.setFrame(picture);
        navigated.setViewFrame(0, picture);
        QSignalSpy dragged(&navigated, &ui::ViewerWidget::viewCameraDragged);
        QSignalSpy panned(&navigated, &ui::ViewerWidget::viewPanned);
        const QPoint middle = navigated.rect().center();
        QTest::mousePress(&navigated, Qt::MiddleButton, Qt::NoModifier, middle);
        QMouseEvent turn(QEvent::MouseMove, QPointF(middle + QPoint(12, 0)), navigated.mapToGlobal(QPointF(middle + QPoint(12, 0))),
                         Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(&navigated, &turn);
        QTest::mouseRelease(&navigated, Qt::MiddleButton, Qt::NoModifier, middle + QPoint(12, 0));
        QCOMPARE(dragged.count(), 1);
        QCOMPARE(dragged[0][1].toInt(), int(ViewDrag::Rotate));
        QCOMPARE(dragged[0][2].toPointF(), QPointF(12, 0));
        QTest::mousePress(&navigated, Qt::RightButton, Qt::NoModifier, middle);
        QMouseEvent pan(QEvent::MouseMove, QPointF(middle + QPoint(0, 8)), navigated.mapToGlobal(QPointF(middle + QPoint(0, 8))),
                        Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&navigated, &pan);
        QTest::mouseRelease(&navigated, Qt::RightButton, Qt::NoModifier, middle + QPoint(0, 8));
        QCOMPARE(panned.count(), 1);
        navigated.setViewType(0, int(ViewType::ActiveCamera));
        dragged.clear();
        QTest::mousePress(&navigated, Qt::MiddleButton, Qt::NoModifier, middle);
        QApplication::sendEvent(&navigated, &turn);
        QTest::mouseRelease(&navigated, Qt::MiddleButton, Qt::NoModifier, middle + QPoint(12, 0));
        QCOMPARE(dragged.count(), 0);
    }
    // Orbit Around Clicked Point: the point of the scene under a ray - the
    // nearest visible 3D sheet where its content is, or a model's triangle,
    // a hidden layer passed through, nothing beside them - and the Orbit
    // button's list of the reference's two modes, the Viewer telling at the
    // press which one turns the view (Alt with the left button always the
    // selected layers) and where the press was; through a perspective
    // view's own camera, the middle of the picture meets the shot's plane.
    void orbitTurnsAboutTheClickedPoint() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString cube = temp.filePath("cube.obj");
        {
            QFile file(cube); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream obj(&file);
            for (int i = 0; i < 8; ++i)
                obj << "v " << (i & 1 ? 40 : -40) << ' ' << (i & 2 ? 40 : -40) << ' ' << (i & 4 ? 40 : -40) << '\n';
            const int quads[6][4] = {{1, 2, 4, 3}, {5, 7, 8, 6}, {1, 5, 6, 2}, {3, 4, 8, 7}, {1, 3, 7, 5}, {2, 6, 8, 4}};
            for (const auto& q : quads)
                obj << "f " << q[0] << ' ' << q[1] << ' ' << q[2] << "\nf " << q[0] << ' ' << q[2] << ' ' << q[3] << '\n';
        }
        media::MediaManager media;
        model3d::ImportSettings settings;
        settings.unit = model3d::ImportSettings::Unit::Pixel;
        settings.autoNormalize = false;
        settings.centerAnchorPoint = false;
        QVERIFY(media.importModel(cube, settings).isSuccess());
        composition::Composition shot;
        shot.setSize(320, 180); shot.setFrameRate(24, 1); shot.setDurationSeconds(1);
        composition::Clip clip; clip.durationSeconds = 1;
        {
            auto& floor = shot.addLayer("Back");
            floor.kind = composition::LayerKind::Plane; floor.dimension = composition::LayerDimension::ThreeD;
            floor.clips << clip;
            floor.transform.scalePercent = QPointF(300, 300);   // 960 x 540
            auto& card = shot.addLayer("Card");
            card.kind = composition::LayerKind::Plane; card.dimension = composition::LayerDimension::ThreeD;
            card.clips << clip;
            card.transform.position = QPointF(100, 0); card.transform.positionZ = 50;
            card.transform.scalePercent = QPointF(10, 10);
            auto& hidden = shot.addLayer("Hidden");
            hidden.kind = composition::LayerKind::Plane; hidden.dimension = composition::LayerDimension::ThreeD;
            hidden.clips << clip; hidden.transform.positionZ = 100; hidden.visible = false;
            auto& model = shot.addLayer("Cube");
            model.kind = composition::LayerKind::Model3D; model.dimension = composition::LayerDimension::ThreeD;
            model.modelAssetId = media.assetByFilePath(QFileInfo(cube).absoluteFilePath()).id();
            model.transform.position = QPointF(-100, 0);
        }
        const QVector3D down(0, 0, -1);
        const auto hit = [&](const QVector3D& from) { return render::sceneRayHit(shot, &media, 0, from, down); };
        const auto near = [](const std::optional<QVector3D>& a, const QVector3D& b) {
            return a && (*a - b).length() < 0.01f;
        };
        QVERIFY(near(hit(QVector3D(0, 0, 1000)), QVector3D(0, 0, 0)));
        QVERIFY(near(hit(QVector3D(100, 0, 1000)), QVector3D(100, 0, 50)));
        QVERIFY(near(hit(QVector3D(-100, 0, 1000)), QVector3D(-100, 0, 40)));
        QVERIFY(near(hit(QVector3D(130, 0, 1000)), QVector3D(130, 0, 0)));   // past the card: the back
        QVERIFY(!hit(QVector3D(500, 0, 1000)));
        QVERIFY(!render::sceneRayHit(shot, &media, 0, QVector3D(0, 280, 1000), down));
        QVERIFY(!render::sceneRayHit(shot, &media, 0, QVector3D(0, 0, 1000), QVector3D(0, 0, 1)));

        // Through a perspective view's own camera, the middle of its picture.
        using render::ViewType;
        const render::ViewCameraState state = render::defaultViewCameraState(ViewType::Perspective);
        const model3d::Camera camera = render::viewCamera(ViewType::Perspective, state, &shot, QSize(320, 180), 0);
        bool invertible = false;
        const QMatrix4x4 unproject = camera.viewProjection(QSize(320, 180)).inverted(&invertible);
        QVERIFY(invertible);
        const QVector3D start = unproject.map(QVector3D(0, 0, -1)), end = unproject.map(QVector3D(0, 0, 1));
        const std::optional<QVector3D> centre = render::sceneRayHit(shot, &media, 0, start, end - start);
        QVERIFY(near(centre, QVector3D(0, 100, 0)));   // the view stands 100 up, looking along -Z

        // The Orbit button's list.
        ui::ViewerWidget viewer; viewer.resize(400, 260); viewer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewer));
        viewer.setProjectSize(QSize(320, 180));
        viewer.setViewType(0, int(ViewType::Perspective));
        QImage picture(320, 180, QImage::Format_ARGB32_Premultiplied); picture.fill(Qt::black);
        viewer.setFrame(picture);
        viewer.setViewFrame(0, picture);
        auto* button = viewer.findChild<QToolButton*>("toolButtonCameraOrbit"); QVERIFY(button && button->menu());
        QCOMPARE(button->menu()->actions().size(), 2);
        QCOMPARE(button->menu()->actions().at(0)->text(), QString("Orbit Around Selected Layers"));
        QCOMPARE(viewer.orbitPivot(), ui::OrbitPivot::SelectedLayers);
        auto* clicked = viewer.findChild<QAction*>("actionOrbitAroundClickedPoint"); QVERIFY(clicked);
        clicked->trigger();
        QCOMPARE(viewer.orbitPivot(), ui::OrbitPivot::ClickedPoint);
        QCOMPARE(viewer.tool(), ui::ViewerWidget::ViewerTool::Orbit);
        QVERIFY(clicked->isChecked() && !viewer.findChild<QAction*>("actionOrbitAroundSelectedLayers")->isChecked());
        QSignalSpy started(&viewer, &ui::ViewerWidget::viewOrbitStarted);
        QSignalSpy orbited(&viewer, &ui::ViewerWidget::viewOrbited);
        const QPoint middle = viewer.mapping(0).toWidget(QPointF(160, 90)).toPoint();
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::NoModifier, middle);
        QMouseEvent move(QEvent::MouseMove, QPointF(middle + QPoint(10, 0)), viewer.mapToGlobal(QPointF(middle + QPoint(10, 0))),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&viewer, &move);
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::NoModifier, middle + QPoint(10, 0));
        QCOMPARE(started.count(), 1);
        QCOMPARE(started[0][0].toInt(), 0);
        QCOMPARE(started[0][2].value<ui::OrbitPivot>(), ui::OrbitPivot::ClickedPoint);
        const QPointF where = started[0][1].toPointF();
        QVERIFY2(std::abs(where.x() - 0.5) < 0.02 && std::abs(where.y() - 0.5) < 0.02,
                 qPrintable(QStringLiteral("%1 %2").arg(where.x()).arg(where.y())));
        QCOMPARE(orbited.count(), 1);
        // Alt with the left button: the selected layers, whatever the list.
        started.clear();
        QTest::mousePress(&viewer, Qt::LeftButton, Qt::AltModifier, middle);
        QTest::mouseRelease(&viewer, Qt::LeftButton, Qt::AltModifier, middle);
        QCOMPARE(started.count(), 1);
        QCOMPARE(started[0][2].value<ui::OrbitPivot>(), ui::OrbitPivot::SelectedLayers);
    }
    // Towards Layer's second turn (AlignToTarget): once a layer faces the
    // other one, it turns about the way to it until its up, seen across the
    // way, is the other layer's - a camera looking at a point turned a
    // quarter about Z takes that point's up, half a turn when the point is
    // upside down; a sheet does the same about its centre, and a light
    // above it; a light whose way lies along the point's up takes the
    // point's -Z instead.
    void towardsLayerTakesTheOtherLayersUp() {
        using composition::AutoOrient;
        composition::Composition shot;
        shot.setSize(320, 180); shot.setFrameRate(24, 1); shot.setDurationSeconds(1);
        {
            auto& point = shot.addLayer("Point");
            point.kind = composition::LayerKind::Point; point.dimension = composition::LayerDimension::ThreeD;
            point.transform.rotationDegrees = 90;
            auto& lens = shot.addLayer("Camera");
            lens.kind = composition::LayerKind::Camera; lens.dimension = composition::LayerDimension::ThreeD;
            lens.transform.positionZ = 500;
            lens.autoOrient = AutoOrient::TowardsLayer; lens.autoOrientLayer = shot.layers()[0].id;
            auto& sheet = shot.addLayer("Sheet");
            sheet.kind = composition::LayerKind::Plane; sheet.dimension = composition::LayerDimension::ThreeD;
            sheet.transform.positionZ = -400;
            sheet.autoOrient = AutoOrient::TowardsLayer; sheet.autoOrientLayer = shot.layers()[0].id;
            auto& lamp = shot.addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.transform.position = QPointF(0, 300);
            lamp.autoOrient = AutoOrient::TowardsLayer; lamp.autoOrientLayer = shot.layers()[0].id;
        }
        const auto near = [](const QVector3D& a, const QVector3D& b) { return (a - b).length() < 1e-3f; };
        const auto text = [](const QVector3D& v) { return QStringLiteral("%1 %2 %3").arg(v.x()).arg(v.y()).arg(v.z()); };
        const QVector3D pointUp = render::layerRotationAt(shot.layers()[0].transform, 0).mapVector(QVector3D(0, 1, 0));
        QVERIFY(std::abs(pointUp.y()) < 1e-3f);   // turned a quarter
        const QMatrix4x4 camera = render::layerTurnAt(&shot, shot.layers()[1], 0);
        QVERIFY(near(camera.mapVector(QVector3D(0, 0, -1)), QVector3D(0, 0, -1)));
        QVERIFY2(near(camera.mapVector(QVector3D(0, 1, 0)), pointUp), qPrintable(text(camera.mapVector(QVector3D(0, 1, 0)))));
        const QMatrix4x4 sheet = render::layerSheetMatrixAt(&shot, shot.layers()[2], 0);
        QVERIFY(near(sheet.mapVector(QVector3D(0, 0, 1)).normalized(), QVector3D(0, 0, 1)));
        QVERIFY2(near(sheet.mapVector(QVector3D(0, 1, 0)).normalized(), pointUp),
                 qPrintable(text(sheet.mapVector(QVector3D(0, 1, 0)).normalized())));
        // The light above looks straight down: the point's Y lies across that
        // way, so the light's up is the point's (its -Z for the light).
        const QMatrix4x4 lamp = render::layerTurnAt(&shot, shot.layers()[3], 0);
        QVERIFY(near(lamp.mapVector(QVector3D(0, 0, -1)), QVector3D(0, -1, 0)));
        QVERIFY2(near(lamp.mapVector(QVector3D(0, 1, 0)), pointUp), qPrintable(text(lamp.mapVector(QVector3D(0, 1, 0)))));
        // Upside down: half a turn.
        shot.layerRef(0).transform.rotationDegrees = 180;
        QVERIFY(near(render::layerTurnAt(&shot, shot.layers()[1], 0).mapVector(QVector3D(0, 1, 0)), QVector3D(0, -1, 0)));
        // With the point's Y along the light's way, its -Z: the point turned
        // a quarter about Z, the light at its side looking along -X.
        shot.layerRef(0).transform.rotationDegrees = 90;
        shot.layerRef(3).transform.position = QPointF(300, 0);
        const QVector3D way(-1, 0, 0);
        QVERIFY(QVector3D::crossProduct(way, pointUp).length() < 1e-3f);
        const QVector3D pointBack = render::layerRotationAt(shot.layers()[0].transform, 0).mapVector(QVector3D(0, 0, -1));
        const QMatrix4x4 side = render::layerTurnAt(&shot, shot.layers()[3], 0);
        QVERIFY(near(side.mapVector(QVector3D(0, 0, -1)), way));
        const QVector3D expected = QVector3D::crossProduct(way, QVector3D::crossProduct(way, pointBack)).normalized();
        const QVector3D got = QVector3D::crossProduct(way, QVector3D::crossProduct(way, side.mapVector(QVector3D(0, 1, 0)))).normalized();
        QVERIFY2(!expected.isNull() && near(got, expected), qPrintable(text(got) + " / " + text(expected)));
    }
    // Parents (AbstractLayer::ParentTransformationAtTime): a 2D layer under
    // a 2D parent is placed, turned and scaled by it - in the scene's
    // numbers, on the canvas, in the Layout frame and in the render, the
    // parent hidden or not; a media parent's anchor point is taken off; a
    // camera stands where its parent puts it; a 2D layer under a 3D parent
    // only follows where that parent shows on the frame; a loop of parents
    // is no parent.
    void childrenFollowTheirParents() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(24, 1); shot->setDurationSeconds(1);
        composition::Clip clip; clip.durationSeconds = 1;
        {
            auto& parent = shot->addLayer("Parent");
            parent.kind = composition::LayerKind::Plane; parent.planeColor = Qt::blue; parent.clips << clip;
            parent.transform.position = QPointF(100, 0);
            parent.transform.rotationDegrees = 90;
            parent.transform.scalePercent = QPointF(50, 50);
            parent.visible = false;
            auto& child = shot->addLayer("Child");
            child.kind = composition::LayerKind::Plane; child.planeColor = Qt::red; child.clips << clip;
            child.transform.position = QPointF(40, 0);
            child.transform.scalePercent = QPointF(10, 10);
            child.parentLayerId = shot->layers()[0].id;
        }
        const auto near = [](const QVector3D& a, const QVector3D& b) { return (a - b).length() < 1e-3f; };
        const auto text = [](const QVector3D& v) { return QStringLiteral("%1 %2 %3").arg(v.x()).arg(v.y()).arg(v.z()); };
        // 40 to the right, halved and turned a quarter clockwise: 20 down.
        const QVector3D at = render::layerPositionAt(shot.get(), shot->layers()[1], 0);
        QVERIFY2(near(at, QVector3D(100, -20, 0)), qPrintable(text(at)));
        const QTransform canvasParent = render::layerCanvasParentAt(shot.get(), shot->layers()[1], 0, QSizeF(320, 180));
        const QPointF mapped = canvasParent.map(QPointF(200, 90));
        QVERIFY2(QLineF(mapped, QPointF(260, 110)).length() < 1e-3, qPrintable(QStringLiteral("%1 %2").arg(mapped.x()).arg(mapped.y())));
        const render::LayerGeometry box = render::layerGeometry(*shot, shot->layers()[1], nullptr, 0);
        QVERIFY(QLineF(box.bounds().center(), QPointF(260, 110)).length() < 0.5);
        const auto frameOf = [&] {
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 20000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const QImage drawn = frameOf();
        QVERIFY(drawn.save(testArtifactPath(QStringLiteral("parented-2d.png"))));
        QVERIFY(drawn.pixelColor(260, 110).red() > 250 && drawn.pixelColor(260, 110).green() < 5);
        QVERIFY(drawn.pixelColor(200, 90).red() < 100);   // not where it would be alone
        // Child turned with its parent: the red plane (32 x 18, then halved
        // and turned) stands 9 wide and 16 tall.
        QVERIFY(drawn.pixelColor(260, 103).red() > 250 && drawn.pixelColor(260, 116).red() > 250);
        QVERIFY(drawn.pixelColor(253, 110).red() < 100 && drawn.pixelColor(267, 110).red() < 100);

        // A media parent's anchor point is taken off: a plane parent too.
        shot->layerRef(0).transform.rotationDegrees = 0;
        shot->layerRef(0).transform.scalePercent = QPointF(100, 100);
        shot->layerRef(0).transform.anchorPoint = QPointF(30, 0);
        QVERIFY(near(render::layerPositionAt(shot.get(), shot->layers()[1], 0), QVector3D(110, 0, 0)));
        shot->layerRef(0).transform.anchorPoint = QPointF(0, 0);
        // A loop is no parent.
        shot->layerRef(0).parentLayerId = shot->layers()[1].id;
        QVERIFY(render::layerParentMatrixAt(shot.get(), shot->layers()[1], 0).map(QVector3D()).length() < 1e6f);
        shot->layerRef(0).parentLayerId = core::Identifier();

        // A camera under a point stands where the point puts it.
        {
            auto& point = shot->addLayer("Point");
            point.kind = composition::LayerKind::Point; point.dimension = composition::LayerDimension::ThreeD;
            point.transform.position = QPointF(50, 0);
            auto& lens = shot->addLayer("Camera");
            lens.kind = composition::LayerKind::Camera; lens.dimension = composition::LayerDimension::ThreeD;
            lens.transform.positionZ = 800;
            lens.parentLayerId = shot->layers()[2].id;
        }
        const model3d::Camera camera = render::sceneCameraOf(shot.get(), QSize(320, 180), 0);
        QVERIFY2(near(camera.position, QVector3D(50, 0, 800)), qPrintable(text(camera.position)));
        shot->layerRef(3).visible = false;   // back to the default camera

        // A 2D layer under a 3D parent follows where the parent shows.
        shot->layerRef(0).dimension = composition::LayerDimension::ThreeD;
        shot->layerRef(0).transform.position = QPointF(60, 0);
        shot->layerRef(1).transform.position = QPointF(0, 0);
        const QVector3D under3D = render::layerPositionAt(shot.get(), shot->layers()[1], 0);
        QVERIFY2(near(under3D, QVector3D(60, 0, 0)), qPrintable(text(under3D)));
        shot->layerRef(0).transform.rotationDegrees = 90;   // its turn does not reach the 2D child
        QVERIFY(render::turnOf(render::layerParentMatrixAt(shot.get(), shot->layers()[1], 0)).isIdentity());
    }
    // A file's 3D model animations as a Media asset (the reference's
    // Model3DAnimationAsset under "3D Model Animations"): apart from the same
    // file as a model, refused for a file without animations, listed in the
    // Media panel, behind its Import menu, and kept by a project.
    void modelAnimationAssetsLiveInMedia() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString file = writeAnimatedArm(dir.path());
        media::MediaManager media;
        QString path;
        QVERIFY(media.importModelAnimation(file, &path).isSuccess());
        QVERIFY(path.endsWith(QLatin1String(media::kModelAnimationMarker)));
        const media::MediaAsset asset = media.assetByFilePath(path);
        QCOMPARE(asset.kind(), media::MediaKind::Model3DAnimation);
        QCOMPARE(asset.sourcePath(), QFileInfo(file).absoluteFilePath());
        QCOMPARE(asset.fileName(), QFileInfo(file).fileName());
        QVERIFY(asset.durationSeconds() > 0.0);
        QCOMPARE(asset.fileFrameRate(), 24.0);
        QVERIFY(media.importModelAnimation(file).isSuccess());          // once only
        QCOMPARE(media.assets().size(), 1);
        model3d::ImportSettings settings;
        QVERIFY(media.importModel(file, settings).isSuccess());         // the model is its own asset
        QCOMPARE(media.assets().size(), 2);
        QCOMPARE(media.assetByFilePath(QFileInfo(file).absoluteFilePath()).kind(), media::MediaKind::Model3D);
        const QString still = dir.filePath("still.obj");
        {
            QFile obj(still); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            obj.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        }
        QVERIFY(media.importModelAnimation(still).isFailure());
        QCOMPARE(media.assets().size(), 2);
        // A marked path, as a drag carries it, imports the same asset.
        media::MediaManager other;
        QVERIFY(other.importFile(path).isSuccess());
        QCOMPARE(other.assetByFilePath(path).kind(), media::MediaKind::Model3DAnimation);

        // Listed in Media as a 3D model animation; Import offers it.
        ui::MediaPanel panel; panel.setMediaManager(&media); panel.refresh();
        auto* list = panel.findChild<QListWidget*>();
        QVERIFY(list);
        bool listed = false;
        for (int row = 0; row < list->count(); ++row)
            listed = listed || list->item(row)->text().contains("3D Model Animation");
        QVERIFY(listed);
        auto* importAnimation = panel.findChild<QAction*>("mediaImportModelAnimation");
        QVERIFY(importAnimation);
        QSignalSpy asked(&panel, &ui::MediaPanel::importModelAnimationRequested);
        importAnimation->trigger();
        QCOMPARE(asked.count(), 1);

        // A project keeps it as a Model3DAnimationAsset, the model beside it.
        composition::Composition shot; shot.setSize(64, 64); shot.setDurationSeconds(1);
        const QString project = dir.filePath("animations.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(project, shot, media).isSuccess());
        QFile saved(project); QVERIFY(saved.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(saved.readAll());
        QVERIFY(xml.contains("<Model3DAnimationAsset Version=\"3\">"));
        QVERIFY(xml.contains("<AnimationIndex>0</AnimationIndex>"));
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(project, &loaded, &reloaded).isSuccess());
        const media::MediaAsset back = reloaded.assetByFilePath(path);
        QCOMPARE(back.kind(), media::MediaKind::Model3DAnimation);
        QCOMPARE(back.sourcePath(), QFileInfo(file).absoluteFilePath());
    }
    // The Timeline's Animations under a model layer: "+" inserts one of the
    // model's animations (or a file's), its rows set Strength, Loop, Custom
    // Playback and Playback, "3D Model Animation Properties" its file,
    // animation and frame rate, and it can be removed - each one undo step.
    void modelAnimationRowsEditTheInstance() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString file = writeAnimatedArm(dir.path());
        QVERIFY(!file.isEmpty());
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importModel(file, settings).isSuccess());
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 200); shot->setFrameRate(24, 1); shot->setDurationSeconds(2);
        {
            auto& model = shot->addLayer("Arm");
            model.kind = composition::LayerKind::Model3D;
            model.modelAssetId = media->assetByFilePath(QFileInfo(file).absoluteFilePath()).id();
        }
        panel->setMediaManager(media); panel->setComposition(shot); QTest::qWait(20);
        QVERIFY(panel->findChild<QToolButton*>("timelineAddModelAnimation_0"));

        const int steps = history.count();
        QVERIFY(panel->insertModelAnimation(0, QString(), 1));
        QCOMPARE(history.count(), steps + 1);
        QCOMPARE(history.undoText(), QString("Insert 3D Model Animation Instance"));
        QCOMPARE(shot->layers()[0].modelAnimations.size(), 1);
        QCOMPARE(shot->layers()[0].modelAnimations[0].name, QString("Lift"));
        QCOMPARE(shot->layers()[0].modelAnimations[0].animationIndex, 1);
        QVERIFY(shot->layers()[0].modelAnimations[0].filePath.isEmpty());
        QVERIFY(!panel->insertModelAnimation(0, QString(), 5));
        QVERIFY(!panel->insertModelAnimation(0, dir.filePath("missing.gltf"), 0));
        QCOMPARE(history.count(), steps + 1);

        const auto spin = [&](const QString& name) { return panel->findChild<QDoubleSpinBox*>(name); };
        const auto check = [&](const QString& name) { return panel->findChild<QCheckBox*>(name); };
        QTRY_VERIFY(spin("timelineModelAnimationStrength_0_0"));
        QCOMPARE(spin("timelineModelAnimationStrength_0_0")->value(), 100.0);
        QVERIFY(!spin("timelineModelAnimationPlayback_0_0")->isEnabled());
        spin("timelineModelAnimationStrength_0_0")->setValue(40.0);
        emit spin("timelineModelAnimationStrength_0_0")->editingFinished();
        QCOMPARE(shot->layers()[0].modelAnimations[0].strength, 40.0);
        QCOMPARE(history.undoText(), QString("Set Property"));
        check("timelineModelAnimationLoop_0_0")->setChecked(true);
        QVERIFY(shot->layers()[0].modelAnimations[0].loop);
        check("timelineModelAnimationCustomPlayback_0_0")->setChecked(true);
        QVERIFY(shot->layers()[0].modelAnimations[0].customPlayback);
        QTRY_VERIFY(spin("timelineModelAnimationPlayback_0_0")
                    && spin("timelineModelAnimationPlayback_0_0")->isEnabled());
        spin("timelineModelAnimationPlayback_0_0")->setValue(6.0);
        emit spin("timelineModelAnimationPlayback_0_0")->editingFinished();
        QCOMPARE(shot->layers()[0].modelAnimations[0].playback, 6.0);
        if (auto* tree = panel->findChild<QTreeWidget*>()) {
            for (QTreeWidgetItemIterator it(tree); *it; ++it)
                if ((*it)->text(0) == "Lift") tree->scrollToItem(*it, QAbstractItemView::PositionAtTop);
        }
        panel->grab().save(testArtifactPath("timeline-model-animations.png"));
        // With keys, the value is set at the playhead.
        shot->layerRef(0).modelAnimations[0].strengthCurve.set(0, 100.0);
        shot->layerRef(0).modelAnimations[0].strengthCurve.set(48, 0.0);
        spin("timelineModelAnimationStrength_0_0")->setValue(70.0);
        emit spin("timelineModelAnimationStrength_0_0")->editingFinished();
        QCOMPARE(shot->layers()[0].modelAnimations[0].strengthAt(0), 70.0);
        QCOMPARE(shot->layers()[0].modelAnimations[0].strength, 40.0);
        // Their keys: the row's key button, its lane, a key dragged along it.
        panel->setComposition(shot); QTest::qWait(20);
        QTreeWidgetItem* strengthRow = nullptr;
        auto* tree = panel->findChild<QTreeWidget*>();
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0, ui::kModelAnimationPropertyRole).isValid()
                && (*it)->data(0, ui::kModelAnimationPropertyRole).toInt() == int(ui::ModelAnimationProperty::Strength))
                strengthRow = *it;
        QVERIFY(strengthRow);
        QCOMPARE(strengthRow->data(0, ui::kModelAnimationRole).toString(),
                 shot->layers()[0].modelAnimations[0].id.value());
        tree->scrollToItem(strengthRow, QAbstractItemView::PositionAtCenter);
        tree->setCurrentItem(strengthRow);
        panel->setPlayheadPosition(1.0);                             // frame 24, between 0 and 48
        auto* toggle = panel->findChild<QToolButton*>("timelineToggleKeyframe");
        QVERIFY(toggle && toggle->isEnabled());
        toggle->click();
        QCOMPARE(history.undoText(), QString("Toggle Property Animation"));
        QVERIFY(shot->layers()[0].modelAnimations[0].strengthCurve.contains(24));
        QCOMPARE(shot->layers()[0].modelAnimations[0].strengthAt(24), 35.0);   // halfway from 70 to 0
        ui::TimelineLane lane;
        lane.layerIndex = 0; lane.modelAnimation = shot->layers()[0].modelAnimations[0].id.value();
        lane.animationProperty = int(ui::ModelAnimationProperty::Strength);
        QCOMPARE(ui::laneCurves(shot.get(), lane).size(), 1);
        QCOMPARE(ui::laneCurves(shot.get(), lane).first()->count(), 3);
        // Dragged from frame 24 to 36 on the canvas.
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); QVERIFY(canvas);
        QTRY_VERIFY(!tree->visualItemRect(strengthRow).isEmpty());
        const int laneY = ui::kTimelineRulerHeight + tree->visualItemRect(strengthRow).center().y();
        const auto xFor = [&](int frame) {
            return int(std::lround(frame / 24.0 * canvas->pixelsPerSecond())) - canvas->hScrollOffset();
        };
        const int dragSteps = history.count();
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(xFor(24), laneY));
        QMouseEvent drag(QEvent::MouseMove, QPoint(xFor(36), laneY), canvas->mapToGlobal(QPoint(xFor(36), laneY)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &drag);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(xFor(36), laneY));
        QVERIFY(!shot->layers()[0].modelAnimations[0].strengthCurve.contains(24));
        QVERIFY(shot->layers()[0].modelAnimations[0].strengthCurve.contains(36));
        QCOMPARE(history.count(), dragSteps + 1);
        panel->grab().save(testArtifactPath("timeline-model-animation-keys.png"));
        // Toggled again where a key is: gone.
        panel->setPlayheadPosition(1.5);
        tree->setCurrentItem(strengthRow);
        toggle->click();
        QVERIFY(!shot->layers()[0].modelAnimations[0].strengthCurve.contains(36));

        // 3D Model Animation Properties.
        QCOMPARE(ui::ModelAnimationDialog::timecode(22.5, 24.0), QString("00:00:22:12"));
        {
            ui::ModelAnimationDialog dialog(shot->layers()[0].modelAnimations[0], file);
            auto* animation = dialog.findChild<QComboBox*>("comboBoxAnimation");
            auto* rate = dialog.findChild<QComboBox*>("comboBoxAnimationFrameRate");
            auto* duration = dialog.findChild<QLabel*>("labelAnimationDuration");
            QVERIFY(animation && rate && duration);
            QCOMPARE(animation->count(), 2);
            QCOMPARE(animation->currentText(), QString("Lift"));
            QCOMPARE(rate->currentIndex(), 0);              // From File
            QCOMPARE(duration->text(), QString("00:00:01:00"));
            rate->setCurrentIndex(rate->findText("30"));
            QCOMPARE(duration->text(), QString("00:00:00:24"));
            animation->setCurrentIndex(0);
            const composition::ModelAnimationInstance edited = dialog.instance();
            QVERIFY(edited.frameRateOverridden);
            QCOMPARE(edited.frameRate, 30.0);
            QCOMPARE(edited.animationIndex, 0);
            dialog.grab().save(testArtifactPath("model-animation-dialog.png"));
            dialog.setFilePath(dir.filePath("missing.gltf"));
            QVERIFY(!animation->isEnabled());
            QCOMPARE(animation->currentText(), QString("Invalid Animation"));
            QCOMPARE(dialog.findChild<QLabel*>("labelAnimationProblem")->text(),
                     QString("The animation file could not be found."));
        }
        // Through its row: OK makes one undo step.
        QTimer::singleShot(0, this, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* rate = dialog->findChild<QComboBox*>("comboBoxAnimationFrameRate");
            if (rate) rate->setCurrentIndex(rate->findText("50"));
            dialog->accept();
        });
        auto* properties = panel->findChild<QToolButton*>("timelineModelAnimationProperties_0_0");
        QVERIFY(properties);
        properties->click();
        QTRY_VERIFY(shot->layers()[0].modelAnimations[0].frameRateOverridden);
        QCOMPARE(shot->layers()[0].modelAnimations[0].frameRate, 50.0);
        QCOMPARE(history.undoText(), QString("Edit 3D Model Animation Asset"));

        // Removed, and back with Undo.
        QTRY_VERIFY(panel->findChild<QToolButton*>("timelineModelAnimationRemove_0_0"));
        panel->findChild<QToolButton*>("timelineModelAnimationRemove_0_0")->click();
        QVERIFY(shot->layers()[0].modelAnimations.isEmpty());
        QCOMPARE(history.undoText(), QString("Remove 3D Model Animation Instance"));
        history.undo();
        QCOMPARE(shot->layers()[0].modelAnimations.size(), 1);
        panel->setComposition(std::make_shared<composition::Composition>());
    }
    // Project Settings "Model Texture Max Size": a map larger than it is
    // drawn from a smaller copy - fine stripes turn to grey at 2 px.
    void modelTexturesFollowTheProjectLimit() {
        QImage wide(64, 32, QImage::Format_RGBA8888); wide.fill(Qt::white);
        QCOMPARE(model3d::limitTextureSize(wide, 16).size(), QSize(16, 8));
        QCOMPARE(model3d::limitTextureSize(wide, 64).cacheKey(), wide.cacheKey());
        QCOMPARE(model3d::limitTextureSize(wide, 0).cacheKey(), wide.cacheKey());

        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage stripes(64, 64, QImage::Format_RGBA8888);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) stripes.setPixelColor(x, y, (x / 2) % 2 ? Qt::white : Qt::black);
        QVERIFY(stripes.save(dir.filePath("stripes.png")));
        {
            QFile mtl(dir.filePath("quad.mtl")); QVERIFY(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
            mtl.write("newmtl tex\nKd 1 1 1\nKa 1 1 1\nKs 0 0 0\nmap_Kd stripes.png\n");
            QFile obj(dir.filePath("quad.obj")); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            obj.write("mtllib quad.mtl\nv -80 -80 0\nv 80 -80 0\nv 80 80 0\nv -80 80 0\n"
                      "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                      "usemtl tex\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importModel(dir.filePath("quad.obj"), settings).isSuccess());
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 200); shot->setDurationSeconds(1);
        {
            auto& model = shot->addLayer("Quad");
            model.kind = composition::LayerKind::Model3D;
            model.modelAssetId = media->assetByFilePath(QFileInfo(dir.filePath("quad.obj")).absoluteFilePath()).id();
        }
        const auto spread = [&] {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0, QSize(200, 200));
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000)) return -1;
            const QByteArray bytes = frames.last()[1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 200, 200, 200 * 4,
                               QImage::Format_RGBA8888);
            int low = 255, high = 0;
            for (int x = 40; x < 160; ++x) {
                const int v = image.pixelColor(x, 100).green();
                low = qMin(low, v); high = qMax(high, v);
            }
            return high - low;
        };
        const int full = spread();
        QVERIFY2(full > 150, qPrintable(QString::number(full)));   // the stripes show
        shot->projectSettings().modelTextureMaxSize = 2;
        const int limited = spread();
        QVERIFY2(limited < 40, qPrintable(QString::number(limited))); // averaged to grey
    }
    // The other maps a material carries: specular, emissive, occlusion (MTL
    // map_Ka), normal (norm, tangent space) and bump (map_Bump, height, -bm).
    // Lights as Flux's Illuminant shades with them: an ambient light, a point
    // light's Phong terms, Linear and Curve falloff, a spot's feathered cone,
    // a surface that is not illuminated, emissive colour and the shadow mix;
    // the light layers they come from (turned as the reference turns a
    // layer, intensity * 2.5 / 100); and a 3D plane under a spot.
    void lightsShadeTheSceneAsFluxDoes() {
        using model3d::Light;
        const QVector4D grey(0.4f, 0.4f, 0.4f, 1.0f), black(0.0f, 0.0f, 0.0f, 1.0f);
        const QVector3D n(0, 0, 1), eye(0, 0, 1000), at;
        const auto near = [](float a, double b) { return std::abs(double(a) - b) < 1e-4; };
        model3d::Lighting lighting;
        // No light: the surface as it is.
        QCOMPARE(model3d::illuminate(lighting, grey, grey, grey, QVector3D(), at, n, eye), grey);
        // Ambient: colour * intensity * ambient * ADSS.x (1).
        Light ambient; ambient.type = Light::Type::Ambient; ambient.intensity = Light::intensityOf(40);
        QCOMPARE(ambient.intensity, 1.0);
        lighting.lights = {ambient};
        QVERIFY(near(model3d::illuminate(lighting, grey, grey, grey, QVector3D(), at, n, eye).x(), 0.4));
        // A point light in front: NdotL * diffuse * ADSS.y (0.5) * 2.5, and
        // with a specular colour pow(R.V, 1) * ADSS.z (0.5) * 2.5 more.
        Light point; point.position = QVector3D(0, 0, 500);
        lighting.lights = {point};
        QVERIFY(near(model3d::illuminate(lighting, grey, grey, black, QVector3D(), at, n, eye).x(), 0.5));
        const QVector4D dim(0.1f, 0.1f, 0.1f, 1.0f);
        QVERIFY(near(model3d::illuminate(lighting, dim, dim, dim, QVector3D(), at, n, eye).x(), 0.25));
        // Falloff over 500 of a 1000 reach: Linear half, Curve a quarter;
        // beyond the reach nothing.
        lighting.lights[0].falloff = Light::Falloff::Linear;
        QVERIFY(near(model3d::illuminate(lighting, grey, grey, black, QVector3D(), at, n, eye).x(), 0.25));
        lighting.lights[0].falloff = Light::Falloff::Curve;
        QVERIFY(near(model3d::illuminate(lighting, grey, grey, black, QVector3D(), at, n, eye).x(), 0.125));
        lighting.lights[0].reach = 400;
        QVERIFY(near(model3d::illuminate(lighting, grey, grey, black, QVector3D(), at, n, eye).x(), 0.0));
        // A spot of 30 degrees, half feathered: whole within 7.5 degrees of
        // its axis, nothing beyond 15, (cos - cos 15) / (cos 7.5 - cos 15) of
        // it between.
        Light spot = point; spot.type = Light::Type::Spot; spot.setCone(30, 50);
        QVERIFY(std::abs(spot.spotOuterCosine - std::cos(qDegreesToRadians(15.0))) < 1e-6);
        QVERIFY(std::abs(spot.spotInnerCosine - std::cos(qDegreesToRadians(7.5))) < 1e-6);
        lighting.lights = {spot};
        const auto spotAt = [&](double degrees) {
            const QVector3D where(float(500 * std::tan(qDegreesToRadians(degrees))), 0, 0);
            return double(model3d::illuminate(lighting, grey, grey, black, QVector3D(), where, n, eye).x());
        };
        const double cos5 = std::cos(qDegreesToRadians(5.0)), cos11 = std::cos(qDegreesToRadians(11.25));
        QVERIFY(std::abs(spotAt(5) - 0.5 * cos5) < 1e-4);
        QVERIFY(spotAt(20) < 1e-6);
        const double share = (cos11 - spot.spotOuterCosine) * spot.spotScale;
        QVERIFY2(std::abs(spotAt(11.25) - 0.5 * cos11 * share) < 1e-4, qPrintable(QString::number(spotAt(11.25))));
        // Emissive adds to a lit surface; one that is not illuminated keeps
        // its colour under the lights.
        lighting.lights = {point};
        QVERIFY(near(model3d::illuminate(lighting, grey, grey, black, QVector3D(0.2f, 0, 0), at, n, eye).x(), 0.7));
        lighting.surface.illuminated = false;
        QCOMPARE(model3d::illuminate(lighting, grey, grey, grey, QVector3D(), at, n, eye), grey);
        // Wholly in a shadow of half opacity: half the shadow's colour, half
        // the light (or, not illuminated, half the surface's own colour).
        struct Covered : model3d::ShadowMap { double amountAt(const QVector3D&) const override { return 1.0; } };
        lighting.lights[0].shadowColor = Qt::blue;
        lighting.shadows = {std::make_shared<Covered>()};
        QVector4D shaded = model3d::illuminate(lighting, grey, grey, black, QVector3D(), at, n, eye);
        QVERIFY(near(shaded.x(), 0.2) && near(shaded.z(), 0.7));
        lighting.surface.illuminated = true;
        shaded = model3d::illuminate(lighting, grey, grey, black, QVector3D(), at, n, eye);
        QVERIFY(near(shaded.x(), 0.25) && near(shaded.z(), 0.75));
        lighting.surface.receiveShadows = false;
        QVERIFY(near(model3d::illuminate(lighting, grey, grey, black, QVector3D(), at, n, eye).z(), 0.5));

        // A layer turns by minus its angles (TransformationHelper::Rotation):
        // Z clockwise on screen, Y and X the same way about their axes.
        composition::LayerTransform turned;
        turned.rotationDegrees = 90;
        QVERIFY((render::layerRotationAt(turned, 0).map(QVector3D(1, 0, 0)) - QVector3D(0, -1, 0)).length() < 1e-5f);
        turned = composition::LayerTransform(); turned.rotationYDegrees = 90;
        QVERIFY((render::layerRotationAt(turned, 0).map(QVector3D(0, 0, 1)) - QVector3D(-1, 0, 0)).length() < 1e-5f);
        turned = composition::LayerTransform(); turned.orientationX = 90;
        QVERIFY((render::layerRotationAt(turned, 0).map(QVector3D(0, 0, -1)) - QVector3D(0, -1, 0)).length() < 1e-5f);

        // The light layers: where they stand and point; a hidden one is out.
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setDurationSeconds(1);
        {
            auto& lamp = shot->addLayer("Spot");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.light.type = composition::LightSettings::Type::Spot;
            lamp.transform.position = QPointF(0, 400); lamp.transform.orientationX = 90;
            auto& hidden = shot->addLayer("Hidden");
            hidden.kind = composition::LayerKind::Light; hidden.visible = false;
        }
        QVector<Light> lights = render::sceneLightsOf(shot.get(), 0);
        QCOMPARE(lights.size(), 1);
        QCOMPARE(lights[0].type, Light::Type::Spot);
        QCOMPARE(lights[0].position, QVector3D(0, 400, 0));
        QVERIFY((lights[0].direction - QVector3D(0, -1, 0)).length() < 1e-5f);
        QCOMPARE(lights[0].intensity, 2.5);
        QVERIFY(std::abs(lights[0].spotOuterCosine - std::cos(qDegreesToRadians(7.5))) < 1e-6);

        // A red 3D plane at z = 0 (frame size there) under a spot 800 out
        // with a hard 20 degree cone: lit within 141 of the middle, black
        // outside; drawn as it is without lights or when not illuminated.
        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(320, 180); scene->setDurationSeconds(1);
        {
            auto& plane = scene->addLayer("Plane");
            plane.kind = composition::LayerKind::Plane; plane.planeColor = Qt::red;
            plane.dimension = composition::LayerDimension::ThreeD;
            composition::Clip clip; clip.durationSeconds = 1; plane.clips << clip;
        }
        const auto frame = [&] {
            render::RenderManager manager; manager.setComposition(scene);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 5000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        QImage image = frame();
        QCOMPARE(image.pixelColor(5, 5), QColor(Qt::red));
        {
            auto& lamp = scene->addLayer("Spot");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.light.type = composition::LightSettings::Type::Spot;
            lamp.light.coneAngle = 20; lamp.light.feather = 0;
            lamp.transform.positionZ = 800;
        }
        image = frame();
        QVERIFY(image.save(testArtifactPath(QStringLiteral("light-spot-plane.png"))));
        QCOMPARE(image.pixelColor(160, 90), QColor(Qt::red));      // 1.25 + 1.25, clamped
        QCOMPARE(image.pixelColor(160, 10), QColor(Qt::red));      // 80 from the middle
        QCOMPARE(image.pixelColor(5, 5), QColor(Qt::black));       // 176 from it
        // At 30 % (0.75): 0.375 diffuse and as much specular in the middle;
        // 130 out NdotL and R.V fall off.
        scene->layerRef(1).light.intensity = 30;
        image = frame();
        const QColor middle = image.pixelColor(160, 90), rim = image.pixelColor(160 + 130, 90);
        QVERIFY2(qAbs(middle.red() - 191) <= 3 && middle.green() == 0, qPrintable(middle.name()));
        QVERIFY2(rim.red() < middle.red() - 10 && rim.red() > 150, qPrintable(rim.name()));
        scene->layerRef(1).light.falloff = composition::LightSettings::Falloff::Linear;
        scene->layerRef(1).light.reach = 700;
        QCOMPARE(frame().pixelColor(160, 90), QColor(Qt::black));   // 800 away, reach 700
        scene->layerRef(0).surface.illuminated = false;
        QCOMPARE(frame().pixelColor(5, 5), QColor(Qt::red));
    }
    // The Light group of a light layer and the Material group of a 3D layer
    // in the timeline edit them as single undoable changes, and both are
    // saved under the reference's property names.
    void lightAndMaterialPropertiesAreEditedAndSaved() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setDurationSeconds(1);
        {
            auto& lamp = shot->addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            auto& sheet = shot->addLayer("Sheet");
            sheet.kind = composition::LayerKind::Plane; sheet.dimension = composition::LayerDimension::ThreeD;
            composition::Clip clip; clip.durationSeconds = 1; sheet.clips << clip;
            auto& flat = shot->addLayer("Flat");
            flat.kind = composition::LayerKind::Plane; flat.clips << clip;
        }
        QUndoStack stack;
        ui::TimelineWidget timeline; timeline.resize(900, 700);
        timeline.setUndoStack(&stack); timeline.setComposition(shot);
        timeline.show(); QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        const auto editor = [&](const QString& name) { return timeline.findChild<QWidget*>(name); };
        QVERIFY(editor("timelineLight_0_type") && editor("timelineLight_0_shadowColor"));
        QVERIFY(editor("timelineSurface_1_illuminated") && editor("timelineSurface_1_emissive"));
        QVERIFY(!editor("timelineSurface_2_illuminated"));   // a 2D layer is not in the scene
        QVERIFY(!editor("timelineSurface_0_illuminated"));

        auto* type = editor("timelineLight_0_type")->findChild<QComboBox*>(); QVERIFY(type);
        QCOMPARE(type->currentData().toString(), QString("2"));   // Point
        type->setCurrentIndex(type->findData(QStringLiteral("3")));
        QCOMPARE(shot->layers()[0].light.type, composition::LightSettings::Type::Spot);
        QCOMPARE(stack.count(), 1);
        auto* reach = editor("timelineLight_0_reach")->findChild<QDoubleSpinBox*>(); QVERIFY(reach);
        QCOMPARE(reach->value(), 1000.0);
        reach->setValue(640);
        QCOMPARE(shot->layers()[0].light.reach, 640.0);
        auto* shadows = editor("timelineLight_0_castShadows")->findChild<QCheckBox*>(); QVERIFY(shadows);
        shadows->setChecked(true);
        QVERIFY(shot->layers()[0].light.castShadows);
        auto* illuminated = editor("timelineSurface_1_illuminated")->findChild<QCheckBox*>(); QVERIFY(illuminated);
        QVERIFY(illuminated->isChecked());
        illuminated->setChecked(false);
        QVERIFY(!shot->layers()[1].surface.illuminated);
        auto* diffuse = editor("timelineSurface_1_diffuse")->findChild<QDoubleSpinBox*>(); QVERIFY(diffuse);
        QCOMPARE(diffuse->value(), 50.0);
        diffuse->setValue(80);
        QCOMPARE(shot->layers()[1].surface.diffuse, 80.0);
        QCOMPARE(stack.count(), 5);
        stack.undo();
        QCOMPARE(shot->layers()[1].surface.diffuse, 50.0);
        stack.redo();
        QVERIFY(timeline.grab().save(testArtifactPath(QStringLiteral("timeline-light-rows.png"))));

        // Saved by the reference's names and read back.
        shot->layerRef(0).light.color = QColor(255, 128, 0);
        shot->layerRef(0).light.intensityCurve.set(0, 150.0);
        shot->layerRef(0).light.falloff = composition::LightSettings::Falloff::Curve;
        shot->layerRef(0).light.shadowColor = QColor(0, 0, 255);
        shot->layerRef(1).surface.emissive = QColor(0, 255, 0);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("lights.vegfx");
        media::MediaManager media;
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, media).isSuccess());
        {
            QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
            const QString xml = QString::fromUtf8(saved.readAll());
            QVERIFY(xml.contains("<LightType>3</LightType>"));
            for (const char* name : {"spotlightAngle", "spotlightFeather", "shadowOpacity", "shadowDiffusion",
                                     "receiveShadows", "matDiffuse", "matShininess", "matEmissiveColor"})
                QVERIFY2(xml.contains(QStringLiteral("<Name>%1</Name>").arg(QLatin1String(name))), name);
        }
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &reloaded).isSuccess());
        QCOMPARE(loaded.layers().size(), 3);
        const composition::LightSettings& light = loaded.layers()[0].light;
        QCOMPARE(light.type, composition::LightSettings::Type::Spot);
        QCOMPARE(light.color, QColor(255, 128, 0));
        QCOMPARE(light.intensityAt(0), 150.0);
        QVERIFY(light.castShadows);
        QCOMPARE(light.falloff, composition::LightSettings::Falloff::Curve);
        QCOMPARE(light.reach, 640.0);
        QCOMPARE(light.shadowColor, QColor(0, 0, 255));
        const composition::SurfaceShading& surface = loaded.layers()[1].surface;
        QVERIFY(!surface.illuminated);
        QVERIFY(surface.receiveShadows);
        QCOMPARE(surface.diffuse, 80.0);
        QCOMPARE(surface.shininess, 100.0);
        QCOMPARE(surface.emissive, QColor(0, 255, 0));
    }
    // What animates as in the reference (CreateProperty's static flag off):
    // a light's colours and numbers, a camera's Lens and a layer's Material.
    // Their rows carry a key button and their lanes the keys; between keys a
    // number is blended and a colour channel by channel - in the scene's
    // lights, the Material and the render - and the keys are saved by the
    // reference's names, a colour's as whole <cl>s. The Material group has
    // the reference's rows for the layer's kind: Cast Shadows If Visibility
    // Is Off for all, Billboarded Lights and Shadows for a media or plane
    // layer, Self Shadow Bias and no Ambient..Emissive for a model.
    void lightAndMaterialPropertiesAnimate() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(24, 1); shot->setDurationSeconds(2);
        composition::Clip clip; clip.durationSeconds = 2;
        {
            auto& lamp = shot->addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.light.type = composition::LightSettings::Type::Ambient;
            lamp.light.intensity = 40;   // 1.0 in the shader
            lamp.light.color = Qt::red;
            auto& sheet = shot->addLayer("Sheet");
            sheet.kind = composition::LayerKind::Plane; sheet.planeColor = Qt::white;
            sheet.dimension = composition::LayerDimension::ThreeD; sheet.clips << clip;
            auto& lens = shot->addLayer("Camera");
            lens.kind = composition::LayerKind::Camera; lens.dimension = composition::LayerDimension::ThreeD;
            lens.visible = false;
            auto& model = shot->addLayer("Model");
            model.kind = composition::LayerKind::Model3D; model.dimension = composition::LayerDimension::ThreeD;
            model.visible = false;
        }
        QUndoStack history;
        ui::TimelineWidget timeline; timeline.resize(900, 900);
        timeline.setUndoStack(&history); timeline.setComposition(shot);
        timeline.show(); QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        auto* tree = timeline.findChild<QTreeWidget*>(); QVERIFY(tree);
        const auto row = [&](int layer, const char* name) -> QTreeWidgetItem* {
            for (QTreeWidgetItemIterator it(tree); *it; ++it)
                if ((*it)->data(0, Qt::UserRole).toInt() == layer
                    && (*it)->data(0, ui::kLayerPropertyRole).toString() == QLatin1String(name))
                    return *it;
            return nullptr;
        };
        for (const char* name : {"color", "intensity", "reach", "spotlightAngle", "spotlightFeather", "shadowOpacity",
                                 "shadowDiffusion", "shadowColor"})
            QVERIFY2(row(0, name), name);
        for (const char* name : {"matAmbient", "matDiffuse", "matSpecular", "matShininess", "matEmissiveColor"})
            QVERIFY2(row(1, name), name);
        for (const char* name : {"zoom", "focusDistance", "aperture", "blurDoF"}) QVERIFY2(row(2, name), name);
        QVERIFY(!row(0, "castShadows") && !row(3, "matDiffuse"));
        const auto editor = [&](const QString& name) { return timeline.findChild<QWidget*>(name); };
        QVERIFY(editor("timelineSurface_1_castShadowsIfInvisible") && editor("timelineSurface_1_billboardedLights")
                && editor("timelineSurface_1_billboardedShadows") && !editor("timelineSurface_1_selfShadowBias"));
        QVERIFY(editor("timelineSurface_3_castShadowsIfInvisible") && editor("timelineSurface_3_selfShadowBias")
                && !editor("timelineSurface_3_billboardedLights") && !editor("timelineSurface_3_diffuse"));

        // The light's colour keyed red at 0 and, at 1 s, blue.
        auto* toggle = timeline.findChild<QToolButton*>("timelineToggleKeyframe"); QVERIFY(toggle);
        tree->setCurrentItem(row(0, "color"));
        timeline.setPlayheadPosition(0.0);
        QVERIFY(toggle->isEnabled());
        toggle->click();
        QCOMPARE(history.undoText(), QString("Toggle Property Animation"));
        QVERIFY(shot->layers()[0].light.colorCurve.contains(0));
        timeline.setPlayheadPosition(1.0);
        tree->setCurrentItem(row(0, "color"));
        toggle->click();
        QVERIFY(shot->layers()[0].light.colorCurve.contains(24));
        composition::setKeyedLayerProperty(shot->layerRef(0), "color", QColor(Qt::blue), 24);
        const QColor between = shot->layers()[0].light.colorAt(12);
        QVERIFY2(qAbs(between.red() - 128) <= 1 && between.green() == 0 && qAbs(between.blue() - 128) <= 1,
                 qPrintable(between.name()));
        QCOMPARE(render::sceneLightsOf(shot.get(), 12).first().color, between);
        QCOMPARE(shot->layers()[0].light.color, QColor(Qt::red));   // its own value stays
        // Reach keyed at 0, then edited at 1 s: a key there.
        tree->setCurrentItem(row(0, "reach"));
        timeline.setPlayheadPosition(0.0);
        toggle->click();
        timeline.setPlayheadPosition(1.0);
        auto* reach = editor("timelineLight_0_reach")->findChild<QDoubleSpinBox*>(); QVERIFY(reach);
        reach->setValue(500);
        QCOMPARE(shot->layers()[0].light.reachCurve.count(), 2);
        QCOMPARE(shot->layers()[0].light.reachAt(12), 750.0);
        QCOMPARE(shot->layers()[0].light.reach, 1000.0);
        ui::TimelineLane lane; lane.layerIndex = 0; lane.layerProperty = "reach";
        QVERIFY(lane.isAnimationRow());
        QCOMPARE(ui::laneCurves(shot.get(), lane).size(), 1);
        QCOMPARE(ui::laneCurves(shot.get(), lane).first()->count(), 2);
        // Toggled where a key is: gone.
        tree->setCurrentItem(row(0, "reach"));
        toggle->click();
        QCOMPARE(shot->layers()[0].light.reachCurve.count(), 1);
        history.undo();
        QCOMPARE(shot->layers()[0].light.reachCurve.count(), 2);
        // A Material and a lens keyed the same way.
        composition::toggleKeyedLayerProperty(shot->layerRef(1), "matDiffuse", 0);
        composition::setKeyedLayerProperty(shot->layerRef(1), "matDiffuse", 100.0, 24);
        QCOMPARE(render::surfaceLightingOf(shot->layers()[1], 12).diffuse, 0.75);
        composition::toggleKeyedLayerProperty(shot->layerRef(1), "matEmissiveColor", 0);
        composition::setKeyedLayerProperty(shot->layerRef(1), "matEmissiveColor", QColor(0, 200, 0), 24);
        QCOMPARE(render::surfaceLightingOf(shot->layers()[1], 24).emissive, QColor(0, 200, 0));
        composition::toggleKeyedLayerProperty(shot->layerRef(2), "blurDoF", 0);
        composition::setKeyedLayerProperty(shot->layerRef(2), "blurDoF", 50.0, 24);
        QCOMPARE(shot->layers()[2].camera.blurAt(12), 75.0);
        // A model's Material is its materials' own: ADSS whole, no Emissive.
        const model3d::SurfaceLighting model = render::surfaceLightingOf(shot->layers()[3], 0);
        QCOMPARE(model.diffuse, 1.0); QCOMPARE(model.ambient, 1.0); QCOMPARE(model.shadowBias, 1.0);
        shot->layerRef(3).surface.selfShadowBias = 1000;
        QCOMPARE(qRound(render::surfaceLightingOf(shot->layers()[3], 0).shadowBias), 1000);

        // In the render: the lit sheet red at 0, between red and blue at 0.5 s.
        const auto frameAt = [&](double seconds) {
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, seconds, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 10000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const QColor start = frameAt(0.0).pixelColor(160, 90);
        QVERIFY2(start.red() > 250 && start.green() < 4 && start.blue() < 4, qPrintable(start.name()));
        const QColor middle = frameAt(0.5).pixelColor(160, 90);
        QVERIFY2(qAbs(middle.red() - 128) <= 3 && qAbs(middle.blue() - 128) <= 3, qPrintable(middle.name()));
        timeline.setPlayheadPosition(0.5);
        QVERIFY(timeline.grab().save(testArtifactPath(QStringLiteral("timeline-light-keys.png"))));

        // Saved and read back.
        shot->layerRef(1).surface.billboardedLights = true;
        shot->layerRef(1).surface.castShadowsIfInvisible = true;
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("keys.vegfx");
        media::MediaManager media;
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, media).isSuccess());
        {
            QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
            QDomDocument doc; QVERIFY(doc.setContent(&saved));
            bool colourKeys = false;
            const QDomNodeList props = doc.elementsByTagName("Prop");
            for (int i = 0; i < props.size(); ++i) {
                const QDomElement prop = props.at(i).toElement();
                if (prop.firstChildElement("Name").text() != "color") continue;
                QCOMPARE(prop.attribute("Type"), QString("0"));
                const QDomElement key = prop.firstChildElement("Animation").firstChildElement("Key");
                colourKeys = !key.firstChildElement("Vl").firstChildElement("cl").isNull();
            }
            QVERIFY(colourKeys);
        }
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &reloaded).isSuccess());
        const composition::LightSettings& light = loaded.layers()[0].light;
        QCOMPARE(light.colorCurve.count(), 2);
        QCOMPARE(light.colorAt(0), QColor(Qt::red));
        QCOMPARE(light.colorAt(24), QColor(Qt::blue));
        QCOMPARE(light.reachAt(12), 750.0);
        const composition::SurfaceShading& surface = loaded.layers()[1].surface;
        QCOMPARE(surface.diffuseAt(12), 75.0);
        QCOMPARE(surface.emissiveAt(24), QColor(0, 200, 0));
        QVERIFY(surface.billboardedLights && surface.castShadowsIfInvisible && !surface.billboardedShadows);
        QCOMPARE(loaded.layers()[2].camera.blurAt(12), 75.0);
        QCOMPARE(loaded.layers()[3].surface.selfShadowBias, 1000.0);
    }
    // Billboarded Lights: a sheet lit as if it faced each light, from it -
    // a light at a grazing angle lights it whole. Cast Shadows If Visibility
    // Is Off: a hidden caster still shadows the floor, and is not drawn.
    void billboardedLightsAndHiddenCasters() {
        model3d::Lighting lighting;
        model3d::Light grazing; grazing.type = model3d::Light::Type::Point; grazing.intensity = 1.0;
        grazing.position = QVector3D(1000, 0, 10);
        lighting.lights = {grazing};
        lighting.surface.diffuse = 0.5; lighting.surface.specular = 0.0;
        const QVector4D white(1, 1, 1, 1);
        const QVector3D n(0, 0, 1), eye(0, 0, 1000), at;
        const double dim = model3d::illuminate(lighting, white, white, white, QVector3D(), at, n, eye).x();
        QVERIFY2(dim < 0.01, qPrintable(QString::number(dim)));
        lighting.surface.billboardedLights = true;
        QVERIFY(qAbs(model3d::illuminate(lighting, white, white, white, QVector3D(), at, n, eye).x() - 0.5) < 1e-4);
        // Seen from the light, its specular is whole too.
        lighting.surface.specular = 0.25;
        QVERIFY(qAbs(model3d::illuminate(lighting, white, white, white, QVector3D(), at, n, eye).x() - 0.75) < 1e-4);

        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(320, 180); scene->setDurationSeconds(1);
        composition::Clip clip; clip.durationSeconds = 1;
        {
            auto& floor = scene->addLayer("Floor");
            floor.kind = composition::LayerKind::Plane; floor.planeColor = Qt::red;
            floor.dimension = composition::LayerDimension::ThreeD; floor.clips << clip;
            auto& card = scene->addLayer("Card");
            card.kind = composition::LayerKind::Plane; card.planeColor = Qt::white;
            card.dimension = composition::LayerDimension::ThreeD; card.clips << clip;
            card.transform.position = QPointF(-100, 0); card.transform.positionZ = 100;
            card.transform.scalePercent = QPointF(12.5, 100.0 * 40 / 180);
            card.surface.castShadows = true;
            card.visible = false;
            auto& lamp = scene->addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.transform.position = QPointF(-300, 0); lamp.transform.positionZ = 400;
            lamp.light.shadowOpacity = 100; lamp.light.castShadows = true;
        }
        const auto frame = [&] {
            render::RenderManager manager; manager.setComposition(scene);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 10000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const QImage hidden = frame();
        QVERIFY(hidden.pixelColor(127, 90).red() > 200);   // hidden: no shadow
        scene->layerRef(1).surface.castShadowsIfInvisible = true;
        const QImage casting = frame();
        QVERIFY(casting.save(testArtifactPath(QStringLiteral("shadow-hidden-caster.png"))));
        QCOMPARE(casting.pixelColor(127, 90), QColor(Qt::black));
        // The card itself is still not drawn: where it stands, the floor.
        const QImage shown = [&] { scene->layerRef(1).visible = true; QImage image = frame(); scene->layerRef(1).visible = false; return image; }();
        bool cardSeen = false;
        for (int x = 0; x < 320 && !cardSeen; ++x)
            for (int y = 0; y < 180 && !cardSeen; ++y)
                if (shown.pixelColor(x, y).green() > 200 && casting.pixelColor(x, y).green() < 50) cardSeen = true;
        QVERIFY(cardSeen);
    }
    // Ambient occlusion as Flux draws it (AmbientOcclusionRenderer): over a
    // G-buffer a flat surface stays open, a floor beside a raised block is
    // darkened along it and open further off, the block's top open, and a
    // pixel weighing too little stays open and occludes nothing. renderMesh
    // notes what each pixel shows, the nearer of two meshes, its normal
    // turned to the eye. In a shot, a floor that receives occlusion darkens
    // round a card in front of it that casts it - not when the card does
    // not cast it, nor when the floor does not receive it - and the card
    // keeps its colour. The Material group has Receives and Casts Ambient
    // Occlusion and the Ambient Occlusion group (a model's with Include Semi
    // Transparent Triangles), and all of it is saved as the reference keeps
    // it: static, the counts as <i>, the rest as <fl>.
    void ambientOcclusionDarkensWhereSurfacesMeet() {
        const QSize size(200, 120);
        const int count = size.width() * size.height();
        QVector<QVector3D> positions(count), normals(count, QVector3D(0, 0, 1));
        QVector<float> weights(count, 1.0f);
        const QRect block(80, 40, 40, 40);
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x)
                positions[y * size.width() + x] =
                    QVector3D(x - 100.0f, 60.0f - y, block.contains(x, y) ? -450.0f : -500.0f);
        const model3d::AmbientOcclusion settings;   // the reference's defaults
        QVector<QVector3D> flat = positions;
        for (QVector3D& p : flat) p.setZ(-500.0f);
        const auto allOpen = [](const QVector<float>& map) {
            return std::all_of(map.cbegin(), map.cend(), [](float v) { return std::abs(v - 1.0f) < 1e-5f; });
        };
        QVERIFY(allOpen(model3d::ambientOcclusionMap(size, flat, normals, weights, settings)));
        const QVector<float> map = model3d::ambientOcclusionMap(size, positions, normals, weights, settings);
        const auto at = [&](int x, int y) { return map.at(y * size.width() + x); };
        const QString values = QStringLiteral("beside %1 %2, far %3, top %4")
                                   .arg(at(77, 60)).arg(at(100, 37)).arg(at(20, 60)).arg(at(100, 60));
        QVERIFY2(at(77, 60) < 0.9f && at(100, 37) < 0.99f, qPrintable(values));
        QVERIFY2(at(20, 60) > 0.999f && at(100, 60) > 0.999f, qPrintable(values));
        model3d::AmbientOcclusion none = settings;
        none.intensity = 0.0;
        QVERIFY(allOpen(model3d::ambientOcclusionMap(size, positions, normals, weights, none)));
        QVector<float> faint = weights;
        faint.fill(0.5f);
        QVERIFY(allOpen(model3d::ambientOcclusionMap(size, positions, normals, faint, settings)));

        // A triangle on the z = 0 plane wound away from the camera, then a
        // nearer one over part of it.
        model3d::Mesh mesh;
        mesh.positions = {QVector3D(-70, -60, 0), QVector3D(-20, 75, 0), QVector3D(80, -45, 0)};
        model3d::Triangle triangle;
        triangle.position[0] = 0; triangle.position[1] = 1; triangle.position[2] = 2;
        mesh.triangles = {triangle};
        const QSize canvas(200, 200);
        const model3d::Camera camera = model3d::defaultCameraForCanvas(canvas);
        model3d::SurfaceBuffer shown;
        model3d::renderMesh(mesh, QMatrix4x4(), camera, canvas, model3d::Lighting(), model3d::ShadingMode::Shaded,
                            1.0, model3d::Fog(), 4, &shown);
        QCOMPARE(shown.size, canvas);
        const int inside = 110 * canvas.width() + 96, outside = 5 * canvas.width() + 5;
        QVERIFY(shown.has(inside) && !shown.has(outside));
        QVERIFY(std::abs(shown.positions.at(inside).z()) < 1e-3f);
        QVERIFY2((shown.normals.at(inside) - QVector3D(0, 0, 1)).length() < 1e-4f,
                 qPrintable(QStringLiteral("%1 %2 %3").arg(shown.normals.at(inside).x())
                                .arg(shown.normals.at(inside).y()).arg(shown.normals.at(inside).z())));
        QCOMPARE(shown.alphas.at(inside), 1.0f);
        QMatrix4x4 nearer; nearer.translate(0, 0, 50);
        model3d::renderMesh(mesh, nearer, camera, canvas, model3d::Lighting(), model3d::ShadingMode::Shaded,
                            1.0, model3d::Fog(), 1, &shown);
        QVERIFY(std::abs(shown.positions.at(inside).z() - 50.0f) < 1e-2f);

        // A shot: a card 30 px in front of a floor.
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(24, 1); shot->setDurationSeconds(1);
        composition::Clip clip; clip.durationSeconds = 1;
        {
            auto& card = shot->addLayer("Card");
            card.kind = composition::LayerKind::Plane; card.planeColor = Qt::red;
            card.dimension = composition::LayerDimension::ThreeD; card.clips << clip;
            card.transform.positionZ = 30;
            card.transform.scalePercent = QPointF(25, 25);
            auto& floor = shot->addLayer("Floor");
            floor.kind = composition::LayerKind::Plane; floor.planeColor = Qt::white;
            floor.dimension = composition::LayerDimension::ThreeD; floor.clips << clip;
            auto& model = shot->addLayer("Model");
            model.kind = composition::LayerKind::Model3D; model.dimension = composition::LayerDimension::ThreeD;
            model.visible = false;
        }
        const auto frame = [&] {
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 20000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const QImage plain = frame();
        QVERIFY(!plain.isNull());
        // The card's left edge on the middle row.
        int edge = -1;
        for (int x = 0; x < 320 && edge < 0; ++x)
            if (plain.pixelColor(x, 90).green() < 50) edge = x;
        QVERIFY2(edge > 100 && edge < 160, qPrintable(QString::number(edge)));
        QCOMPARE(plain.pixelColor(edge - 4, 90), QColor(Qt::white));
        shot->layerRef(1).surface.receiveAmbientOcclusion = true;
        QCOMPARE(frame().pixelColor(edge - 4, 90), QColor(Qt::white));   // the card casts none
        shot->layerRef(0).surface.castAmbientOcclusion = true;
        const QImage occluded = frame();
        QVERIFY(occluded.save(testArtifactPath(QStringLiteral("ambient-occlusion-card.png"))));
        const QColor beside = occluded.pixelColor(edge - 4, 90);
        QVERIFY2(beside.red() < 225 && beside.red() == beside.green() && beside.green() == beside.blue(),
                 qPrintable(beside.name()));
        QCOMPARE(occluded.pixelColor(10, 10), QColor(Qt::white));
        QCOMPARE(occluded.pixelColor(160, 90), plain.pixelColor(160, 90));   // the card itself
        shot->layerRef(1).surface.receiveAmbientOcclusion = false;
        QCOMPARE(frame().pixelColor(edge - 4, 90), QColor(Qt::white));

        // The Material group.
        QUndoStack history;
        ui::TimelineWidget timeline; timeline.resize(900, 1400);
        timeline.setUndoStack(&history); timeline.setComposition(shot);
        timeline.show(); QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        const auto editor = [&](const QString& name) { return timeline.findChild<QWidget*>(name); };
        for (const char* key : {"receiveAmbientOcclusion", "castAmbientOcclusion", "aoSamples", "aoSampleRadius",
                                "aoIntensity", "aoDepthScale", "aoBias", "aoBlurRadius", "aoDepthFalloff"}) {
            QVERIFY2(editor(QStringLiteral("timelineSurface_1_%1").arg(key)), key);
            QVERIFY2(editor(QStringLiteral("timelineSurface_2_%1").arg(key)), key);
        }
        QVERIFY(!editor("timelineSurface_1_includeSemiTransparentTriangles")
                && editor("timelineSurface_2_includeSemiTransparentTriangles"));
        auto* tree = timeline.findChild<QTreeWidget*>(); QVERIFY(tree);
        QTreeWidgetItem* group = nullptr;
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->text(0) == QLatin1String("Ambient Occlusion") && (*it)->data(0, Qt::UserRole).toInt() == 1)
                group = *it;
        QVERIFY(group);
        QCOMPARE(group->childCount(), 7);
        QCOMPARE(group->parent()->text(0), QString("Material"));
        auto* samples = editor("timelineSurface_1_aoSamples")->findChild<QDoubleSpinBox*>(); QVERIFY(samples);
        QCOMPARE(samples->value(), 4.0);
        QCOMPARE(samples->decimals(), 0);
        samples->setValue(8);
        QCOMPARE(shot->layers()[1].surface.aoSamples, 8);
        history.undo();
        QCOMPARE(shot->layers()[1].surface.aoSamples, 4);

        // Saved and read back.
        composition::SurfaceShading& surface = shot->layerRef(1).surface;
        surface.receiveAmbientOcclusion = true;
        surface.aoSamples = 9; surface.aoSampleRadius = 3; surface.aoIntensity = 75; surface.aoDepthScale = 20;
        surface.aoBias = 0.5; surface.aoBlurRadius = 2; surface.aoDepthFalloff = 30;
        shot->layerRef(2).surface.includeSemiTransparentTriangles = false;
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("occlusion.vegfx");
        media::MediaManager media;
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, media).isSuccess());
        {
            QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
            QDomDocument doc; QVERIFY(doc.setContent(&saved));
            QStringList kinds;
            const QDomNodeList props = doc.elementsByTagName("Prop");
            for (int i = 0; i < props.size(); ++i) {
                const QDomElement prop = props.at(i).toElement();
                const QString name = prop.firstChildElement("Name").text();
                if (name != "aoSamples" && name != "aoBias" && name != "receiveAmbientOcclusion") continue;
                QCOMPARE(prop.attribute("Type"), QString("1"));
                kinds << name + ":" + prop.firstChildElement("Static").firstChildElement().tagName();
            }
            QVERIFY2(kinds.contains("aoSamples:i") && kinds.contains("aoBias:fl")
                         && kinds.contains("receiveAmbientOcclusion:b"),
                     qPrintable(kinds.join(' ')));
        }
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &reloaded).isSuccess());
        const composition::SurfaceShading& back = loaded.layers()[1].surface;
        QVERIFY(back.receiveAmbientOcclusion && !back.castAmbientOcclusion);
        QVERIFY(loaded.layers()[0].surface.castAmbientOcclusion);
        QCOMPARE(back.aoSamples, 9); QCOMPARE(back.aoSampleRadius, 3); QCOMPARE(back.aoIntensity, 75.0);
        QCOMPARE(back.aoDepthScale, 20.0); QCOMPARE(back.aoBias, 0.5); QCOMPARE(back.aoBlurRadius, 2);
        QCOMPARE(back.aoDepthFalloff, 30.0);
        QVERIFY(!loaded.layers()[2].surface.includeSemiTransparentTriangles);
        QVERIFY(loaded.layers()[1].surface.includeSemiTransparentTriangles);
    }
    // Reflections as Flux draws them. Lat-long lookups go as
    // ConvertVec2LatLong does (+Y the top row, a direction with x or z 0 at
    // the middle column), and a white environment lights every way by pi.
    // A black mirror sheet that receives reflections shows a green card in
    // front of it where its mirror image falls - added at full strength,
    // at a quarter with Specular Reflectivity 50 - not without Receives or
    // Casts Reflections, still from a hidden card with Cast Reflections If
    // Visibility Is Off, and never from a card behind the mirror. A 3D
    // text with an Environment Map takes the colour of the layer it uses,
    // or of the shot beneath with Use Composite Shot. The Material group
    // has the reference's rows for each kind (Environment Map's Layer once
    // asked for), and all of it is saved under the reference's names.
    void reflectionsMirrorTheSceneInSheets() {
        QVERIFY((model3d::latLongOf(QVector3D(0, 0, 1)) - QVector2D(0.5f, 0.5f)).length() < 1e-5f);
        QVERIFY(std::abs(model3d::latLongOf(QVector3D(0, 1, 0)).y() - 1.0f) < 1e-5f);
        QVERIFY(std::abs(model3d::latLongOf(QVector3D(1, 0, 0.001f)).x() - 0.75f) < 1e-3f);
        QVERIFY(std::abs(model3d::latLongOf(QVector3D(1, 0, 0)).x() - 0.5f) < 1e-5f);   // the shader's quirk
        QImage sky(64, 32, QImage::Format_RGBA8888);
        sky.fill(Qt::blue);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 64; ++x) sky.setPixelColor(x, y, Qt::red);
        QVERIFY(model3d::environmentColor(sky, QVector3D(0, 1, 0.2f)).x() > 0.99f);
        QVERIFY(model3d::environmentColor(sky, QVector3D(0, -1, 0.2f)).z() > 0.99f);
        // Straight up, the map repeats: half the bottom row.
        QVERIFY(std::abs(model3d::environmentColor(sky, QVector3D(0, 1, 0)).x() - 0.5f) < 0.01f);
        QImage white(64, 32, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        const model3d::Irradiance even = model3d::irradianceOf(white);
        for (const QVector3D& n : {QVector3D(0, 1, 0), QVector3D(1, 0, 0), QVector3D(0, 0, -1)})
            QVERIFY2(std::abs(even.at(n).x() - 3.14159f) < 0.05f, qPrintable(QString::number(even.at(n).x())));
        const model3d::Irradiance skyLight = model3d::irradianceOf(sky);
        QVERIFY(skyLight.at(QVector3D(0, 1, 0)).x() > skyLight.at(QVector3D(0, -1, 0)).x() + 1.0f);

        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(24, 1); shot->setDurationSeconds(1);
        composition::Clip clip; clip.durationSeconds = 1;
        {
            auto& card = shot->addLayer("Card");
            card.kind = composition::LayerKind::Plane; card.planeColor = Qt::green;
            card.dimension = composition::LayerDimension::ThreeD; card.clips << clip;
            card.transform.position = QPointF(60, 0); card.transform.positionZ = 100;
            card.transform.scalePercent = QPointF(10, 10);
            auto& mirror = shot->addLayer("Mirror");
            mirror.kind = composition::LayerKind::Plane; mirror.planeColor = Qt::black;
            mirror.dimension = composition::LayerDimension::ThreeD; mirror.clips << clip;
        }
        const auto frameOf = [&](const std::shared_ptr<composition::Composition>& composition) {
            render::RenderManager manager; manager.setComposition(composition);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 20000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        // The card's mirror image is 100 behind the mirror: its middle at
        // 160 + 60 f / (d + 100) with f = d = 90 / tan(19.8 degrees).
        const double d = 90.0 / std::tan(qDegreesToRadians(39.6 / 2.0));
        const int reflected = qRound(160 + 60 * d / (d + 100)), direct = qRound(160 + 60 * d / (d - 100));
        const QImage plain = frameOf(shot);
        QVERIFY(!plain.isNull());
        QCOMPARE(plain.pixelColor(reflected, 90), QColor(Qt::black));
        QVERIFY(plain.pixelColor(direct, 90).green() > 250);
        shot->layerRef(1).surface.receiveReflections = true;
        QCOMPARE(frameOf(shot).pixelColor(reflected, 90), QColor(Qt::black));   // the card casts none
        shot->layerRef(0).surface.castReflections = true;
        const QImage mirrored = frameOf(shot);
        QVERIFY(mirrored.save(testArtifactPath(QStringLiteral("planar-reflection.png"))));
        QCOMPARE(mirrored.pixelColor(reflected, 90), QColor(Qt::green));
        QCOMPARE(mirrored.pixelColor(160, 90), QColor(Qt::black));
        QVERIFY(mirrored.pixelColor(direct, 90).green() > 250);
        shot->layerRef(1).surface.specularReflectivity = 50;
        const QColor quarter = frameOf(shot).pixelColor(reflected, 90);
        QVERIFY2(std::abs(quarter.green() - 64) <= 2 && quarter.red() == 0, qPrintable(quarter.name()));
        shot->layerRef(1).surface.specularReflectivity = 100;
        // Hidden: still reflected with Cast Reflections If Visibility Is Off.
        shot->layerRef(0).visible = false;
        QCOMPARE(frameOf(shot).pixelColor(reflected, 90), QColor(Qt::black));
        shot->layerRef(0).surface.castReflectionsIfInvisible = true;
        const QImage hidden = frameOf(shot);
        QCOMPARE(hidden.pixelColor(reflected, 90), QColor(Qt::green));
        QCOMPARE(hidden.pixelColor(direct, 90), QColor(Qt::black));
        shot->layerRef(0).visible = true;
        // Behind the mirror: cut away, no reflection where it would fall.
        shot->layerRef(0).transform.positionZ = -100;
        const QImage behind = frameOf(shot);
        for (int x = 150; x < 300; ++x) QVERIFY2(behind.pixelColor(x, 90).green() < 5, qPrintable(QString::number(x)));
        shot->layerRef(0).transform.positionZ = 100;

        // A 3D text with an environment map.
        auto titled = std::make_shared<composition::Composition>();
        titled->setSize(320, 180); titled->setFrameRate(24, 1); titled->setDurationSeconds(1);
        {
            auto& title = titled->addLayer("Title");
            title.kind = composition::LayerKind::Text; title.dimension = composition::LayerDimension::ThreeD;
            composition::TextStyle style;
            style.text = "MMMM"; style.fontSize = 90; style.fontColor = QColor(0, 128, 0);
            style.alignH = composition::TextStyle::AlignH::Center;
            style.alignV = composition::TextStyle::AlignV::Middle;
            composition::Clip textClip; textClip.durationSeconds = 1;
            composition::Effect text; text.pluginId = core::Identifier("text"); text.name = "Text";
            text.parameterValues = composition::textStyleToParameters(style);
            textClip.effects << text;
            title.clips << textClip;
            auto& red = titled->addLayer("Red");
            red.kind = composition::LayerKind::Plane; red.planeColor = Qt::red; red.clips << clip;
            red.visible = false;
            auto& floor = titled->addLayer("Blue");
            floor.kind = composition::LayerKind::Plane; floor.planeColor = Qt::blue; floor.clips << clip;
        }
        const auto count = [](const QImage& image, const std::function<bool(const QColor&)>& wanted) {
            int found = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) found += wanted(image.pixelColor(x, y)) ? 1 : 0;
            return found;
        };
        const auto inked = [](const QColor& c) { return c.green() > 100 && c.red() < 40 && c.blue() < 40; };
        const QImage bare = frameOf(titled);
        QVERIFY2(count(bare, inked) > 300, qPrintable(QString::number(count(bare, inked))));
        using Environment = composition::SurfaceShading::EnvironmentMap;
        titled->layerRef(0).surface.environmentMap = Environment::Layer;
        titled->layerRef(0).surface.environmentMapLayer = titled->layers()[1].id;
        const QImage redder = frameOf(titled);
        QVERIFY(redder.save(testArtifactPath(QStringLiteral("environment-map-layer.png"))));
        const auto reddened = [](const QColor& c) { return c.red() > 200 && c.green() > 100 && c.blue() < 40; };
        QVERIFY2(count(redder, reddened) > 300 && count(redder, inked) < 20,
                 qPrintable(QStringLiteral("%1 %2").arg(count(redder, reddened)).arg(count(redder, inked))));
        titled->layerRef(0).surface.environmentMap = Environment::CompositeShot;
        const QImage bluer = frameOf(titled);
        const auto blued = [](const QColor& c) { return c.blue() > 200 && c.green() > 100 && c.red() < 40; };
        QVERIFY2(count(bluer, blued) > 300, qPrintable(QString::number(count(bluer, blued))));
        titled->layerRef(0).surface.specularReflectivity = 0;
        QCOMPARE(count(frameOf(titled), blued), 0);
        titled->layerRef(0).surface.specularReflectivity = 100;

        // The Material group.
        {
            auto& model = shot->addLayer("Model");
            model.kind = composition::LayerKind::Model3D; model.dimension = composition::LayerDimension::ThreeD;
            model.visible = false;
            auto& title = shot->addLayer("Title");
            title.kind = composition::LayerKind::Text; title.dimension = composition::LayerDimension::ThreeD;
            title.clips << clip;
        }
        QUndoStack history;
        ui::TimelineWidget timeline; timeline.resize(900, 1600);
        timeline.setUndoStack(&history); timeline.setComposition(shot);
        timeline.show(); QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        const auto editor = [&](const QString& name) { return timeline.findChild<QWidget*>(name); };
        for (int layer : {1, 2, 3})
            for (const char* key : {"receiveReflections", "castReflections", "castReflectionsIfInvisible"})
                QVERIFY2(editor(QStringLiteral("timelineSurface_%1_%2").arg(layer).arg(key)), key);
        QVERIFY(!editor("timelineSurface_1_environmentMap") && editor("timelineSurface_2_environmentMap")
                && editor("timelineSurface_3_environmentMap"));
        QVERIFY(editor("timelineSurface_1_specularReflectivity") && !editor("timelineSurface_2_specularReflectivity")
                && editor("timelineSurface_3_specularReflectivity"));
        QVERIFY(!editor("timelineSurface_1_diffuseReflectivity") && editor("timelineSurface_3_diffuseReflectivity"));
        QVERIFY(!editor("timelineSurface_3_environmentMapLayer"));
        auto* environment = editor("timelineSurface_3_environmentMap")->findChild<QComboBox*>(); QVERIFY(environment);
        QCOMPARE(environment->count(), 3);
        environment->setCurrentIndex(2);
        QCOMPARE(shot->layers()[3].surface.environmentMap, Environment::Layer);
        QTRY_VERIFY(editor("timelineSurface_3_environmentMapLayer"));
        auto* source = editor("timelineSurface_3_environmentMapLayer")->findChild<QComboBox*>(); QVERIFY(source);
        source->setCurrentIndex(source->findText("Mirror"));
        QCOMPARE(shot->layers()[3].surface.environmentMapLayer, shot->layers()[1].id);
        auto* tree = timeline.findChild<QTreeWidget*>(); QVERIFY(tree);
        bool keyed = false;
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            keyed = keyed || ((*it)->data(0, Qt::UserRole).toInt() == 3
                              && (*it)->data(0, ui::kLayerPropertyRole).toString() == "diffuseReflectivity");
        QVERIFY(keyed);

        // Saved and read back.
        composition::toggleKeyedLayerProperty(shot->layerRef(1), "specularReflectivity", 0);
        composition::setKeyedLayerProperty(shot->layerRef(1), "specularReflectivity", 20.0, 24);
        shot->layerRef(3).surface.diffuseReflectivity = 40;
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("reflections.vegfx");
        media::MediaManager media;
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, media).isSuccess());
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &reloaded).isSuccess());
        QVERIFY(loaded.layers()[0].surface.castReflections && loaded.layers()[0].surface.castReflectionsIfInvisible);
        QVERIFY(loaded.layers()[1].surface.receiveReflections && !loaded.layers()[1].surface.castReflections);
        QCOMPARE(loaded.layers()[1].surface.specularReflectivityAt(12), 60.0);
        QCOMPARE(loaded.layers()[3].surface.environmentMap, Environment::Layer);
        QCOMPARE(loaded.layers()[3].surface.environmentMapLayer, shot->layers()[1].id);
        QCOMPARE(loaded.layers()[3].surface.diffuseReflectivity, 40.0);
    }
    // A model's reflections as Flux's model shader takes them. A reflection
    // map has its mip levels. A cube's front face, unlit grey, turns red with
    // an Environment Map of a red layer (its Specular Reflectivity adding
    // the map), stays grey without; receiving reflections, it shows a green
    // card behind the camera that casts them, seen in the scene round the
    // cube. A flat model whose material's Reflection Mode is Planar shows a
    // card mirrored in its plane where the mirror image falls, a Spherical
    // one does not. Reflection Mode is saved as the reference's
    // ReflectionType and read back.
    void modelsReflectTheirSurroundings() {
        QImage white(64, 32, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        const model3d::Reflections map = model3d::Reflections::fromLatLong(white);
        QCOMPARE(map.levels.size(), 5);
        QCOMPARE(map.levels.last().size(), QSize(4, 2));
        QVERIFY(std::abs(map.at(QVector3D(0, 0.3f, 1), 2.5).x() - 1.0f) < 0.01f);

        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto writeObj = [&](const QString& name, const std::function<void(QTextStream&)>& body) {
            const QString path = temp.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
            QTextStream obj(&file);
            body(obj);
            return path;
        };
        const QString cube = writeObj("cube.obj", [](QTextStream& obj) {
            for (int i = 0; i < 8; ++i)
                obj << "v " << (i & 1 ? 40 : -40) << ' ' << (i & 2 ? 40 : -40) << ' ' << (i & 4 ? 40 : -40) << '\n';
            const int quads[6][4] = {{1, 2, 4, 3}, {5, 7, 8, 6}, {1, 5, 6, 2}, {3, 4, 8, 7}, {1, 3, 7, 5}, {2, 6, 8, 4}};
            for (const auto& q : quads)
                obj << "f " << q[0] << ' ' << q[1] << ' ' << q[2] << "\nf " << q[0] << ' ' << q[2] << ' ' << q[3] << '\n';
        });
        const QString flat = writeObj("flat.obj", [](QTextStream& obj) {
            obj << "v -160 -90 0\nv 160 -90 0\nv 160 90 0\nv -160 90 0\nf 1 2 3\nf 1 3 4\n";
        });
        QVERIFY(!cube.isEmpty() && !flat.isEmpty());
        auto media = std::make_shared<media::MediaManager>();
        model3d::ImportSettings settings;
        settings.unit = model3d::ImportSettings::Unit::Pixel;
        settings.autoNormalize = false;
        settings.centerAnchorPoint = false;
        QVERIFY(media->importModel(cube, settings).isSuccess());
        QVERIFY(media->importModel(flat, settings).isSuccess());
        const core::Identifier cubeId = media->assetByFilePath(QFileInfo(cube).absoluteFilePath()).id();
        const core::Identifier flatId = media->assetByFilePath(QFileInfo(flat).absoluteFilePath()).id();
        QVERIFY(!media->modelMesh(cubeId).isEmpty() && !media->modelMesh(flatId).isEmpty());
        // White specular, no diffuse reflectivity: the reflection alone.
        const auto shiny = [&](const core::Identifier& id, model3d::ReflectionMode mode) {
            QVector<model3d::Material> materials = media->modelMesh(id).materials;
            for (model3d::Material& material : materials) {
                material.specularColor = Qt::white;
                material.diffuseReflectivity = 0.0;
                material.reflectionMode = mode;
            }
            media->setModelMaterials(id, materials);
        };
        shiny(cubeId, model3d::ReflectionMode::Spherical);
        shiny(flatId, model3d::ReflectionMode::Planar);

        composition::Clip clip; clip.durationSeconds = 1;
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(24, 1); shot->setDurationSeconds(1);
        {
            auto& model = shot->addLayer("Cube");
            model.kind = composition::LayerKind::Model3D; model.dimension = composition::LayerDimension::ThreeD;
            model.modelAssetId = cubeId;
            auto& red = shot->addLayer("Red");
            red.kind = composition::LayerKind::Plane; red.planeColor = Qt::red; red.clips << clip;
            red.visible = false;
            auto& card = shot->addLayer("Card");
            card.kind = composition::LayerKind::Plane; card.planeColor = Qt::green; card.clips << clip;
            card.dimension = composition::LayerDimension::ThreeD;
            card.transform.positionZ = 400;
            card.visible = false;
        }
        const auto frameOf = [&](const std::shared_ptr<composition::Composition>& composition) {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(composition);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 30000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const QColor grey = frameOf(shot).pixelColor(160, 90);
        QVERIFY2(grey.red() > 100 && grey.red() < 230 && grey.red() == grey.green() && grey.green() == grey.blue(),
                 qPrintable(grey.name()));
        using Environment = composition::SurfaceShading::EnvironmentMap;
        shot->layerRef(0).surface.environmentMap = Environment::Layer;
        shot->layerRef(0).surface.environmentMapLayer = shot->layers()[1].id;
        const QColor reddened = frameOf(shot).pixelColor(160, 90);
        QVERIFY2(reddened.red() == 255 && std::abs(reddened.green() - grey.green()) <= 2, qPrintable(reddened.name()));
        // Type 1003 (not in the Material rows): a project asset's picture,
        // no layer of the shot.
        {
            QImage redPicture(64, 32, QImage::Format_RGBA8888);
            redPicture.fill(Qt::red);
            const QString redPath = temp.filePath("red.png");
            QVERIFY(redPicture.save(redPath));
            QVERIFY(media->importFile(redPath).isSuccess());
            shot->layerRef(0).surface.environmentMap = Environment::Asset;
            shot->layerRef(0).surface.environmentMapLayer =
                media->assetByFilePath(QFileInfo(redPath).absoluteFilePath()).id();
            const QColor fromAsset = frameOf(shot).pixelColor(160, 90);
            QVERIFY2(fromAsset.red() == 255 && std::abs(fromAsset.green() - grey.green()) <= 2,
                     qPrintable(fromAsset.name()));
        }
        shot->layerRef(0).surface.environmentMap = Environment::None;
        // Receiving reflections: the card behind the camera, in the cube.
        shot->layerRef(0).surface.receiveReflections = true;
        QCOMPARE(frameOf(shot).pixelColor(160, 90), grey);   // the card casts none
        shot->layerRef(2).surface.castReflections = true;
        shot->layerRef(2).surface.castReflectionsIfInvisible = true;
        const QImage surrounded = frameOf(shot);
        QVERIFY(surrounded.save(testArtifactPath(QStringLiteral("model-reflection-cube.png"))));
        const QColor greened = surrounded.pixelColor(160, 90);
        QVERIFY2(greened.green() == 255 && greened.red() == grey.red(), qPrintable(greened.name()));

        // A flat model, Planar, over a card in front of it.
        QVector<model3d::Material> materials = media->modelMaterialEdits(flatId);
        QVERIFY(!materials.isEmpty());
        auto mirrored = std::make_shared<composition::Composition>();
        mirrored->setSize(320, 180); mirrored->setFrameRate(24, 1); mirrored->setDurationSeconds(1);
        {
            auto& card = mirrored->addLayer("Card");
            card.kind = composition::LayerKind::Plane; card.planeColor = Qt::green; card.clips << clip;
            card.dimension = composition::LayerDimension::ThreeD;
            card.transform.position = QPointF(60, 0); card.transform.positionZ = 100;
            card.transform.scalePercent = QPointF(10, 10);
            card.surface.castReflections = true;
            auto& model = mirrored->addLayer("Mirror");
            model.kind = composition::LayerKind::Model3D; model.dimension = composition::LayerDimension::ThreeD;
            model.modelAssetId = flatId;
            model.surface.receiveReflections = true;
        }
        const double d = 90.0 / std::tan(qDegreesToRadians(39.6 / 2.0));
        const int reflected = qRound(160 + 60 * d / (d + 100));
        const QImage planar = frameOf(mirrored);
        QVERIFY(planar.save(testArtifactPath(QStringLiteral("model-reflection-planar.png"))));
        const QColor seen = planar.pixelColor(reflected, 90);
        QVERIFY2(seen.green() == 255 && seen.red() < 230, qPrintable(seen.name()));
        const QColor plain = planar.pixelColor(100, 90);
        QVERIFY2(plain.green() == plain.red(), qPrintable(plain.name()));
        materials.first().reflectionMode = model3d::ReflectionMode::Spherical;
        media->setModelMaterials(flatId, materials);
        QVERIFY(frameOf(mirrored).pixelColor(reflected, 90).green() < 240);

        // Saved as ReflectionType and read back.
        materials.first().reflectionMode = model3d::ReflectionMode::Planar;
        media->setModelMaterials(flatId, materials);
        const QString path = temp.filePath("planar.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *mirrored, *media).isSuccess());
        {
            QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
            QVERIFY(saved.readAll().contains("<ReflectionType>0</ReflectionType>"));
        }
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &reloaded).isSuccess());
        const QVector<model3d::Material> back = reloaded.modelMaterialEdits(flatId);
        QVERIFY(!back.isEmpty());
        QCOMPARE(back.first().reflectionMode, model3d::ReflectionMode::Planar);
    }
    // Alignment (AutoOrient) as the reference turns layers by it: a light and
    // a camera Towards Target Position aim their -Z at their Target - one
    // straight below keeps (-1, 0, 0) as its up - and a point layer its +Z;
    // Towards Layer aims at that layer; a 3D plane Towards Active Camera turns
    // as the shot's camera does, whatever its own rotation, and is drawn
    // whole. The Transform group shows Target after Position for those
    // layers, Alignment with the reference's choices for the kind and
    // Alignment Layer once it is asked for; all of it is saved as the
    // reference keeps it (<AutoOrient>, <AutoOrientLayer>, "target").
    void layersTurnByTheirAlignment() {
        using composition::AutoOrient;
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(24, 1); shot->setDurationSeconds(1);
        composition::Clip clip; clip.durationSeconds = 1;
        {
            auto& lamp = shot->addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.transform.position = QPointF(0, 400);
            lamp.autoOrient = AutoOrient::TowardsTargetPosition;
            auto& lens = shot->addLayer("Lens");
            lens.kind = composition::LayerKind::Camera; lens.dimension = composition::LayerDimension::ThreeD;
            lens.transform.positionZ = 1000;
            lens.autoOrient = AutoOrient::TowardsTargetPosition;
            lens.transform.targetX = 100;
            auto& mark = shot->addLayer("Mark");
            mark.kind = composition::LayerKind::Point; mark.dimension = composition::LayerDimension::ThreeD;
            mark.autoOrient = AutoOrient::TowardsTargetPosition;
            mark.transform.targetX = 500;
            auto& card = shot->addLayer("Card");
            card.kind = composition::LayerKind::Plane; card.planeColor = Qt::white;
            card.dimension = composition::LayerDimension::ThreeD; card.clips << clip;
            card.transform.rotationYDegrees = 80;
            auto& flat = shot->addLayer("Flat");
            flat.kind = composition::LayerKind::Plane; flat.clips << clip; flat.visible = false;
        }
        const auto near = [](const QVector3D& a, const QVector3D& b) { return (a - b).length() < 1e-3f; };
        // A light straight above its target looks down.
        QVERIFY(near(render::sceneLightsOf(shot.get(), 0).first().direction, QVector3D(0, -1, 0)));
        shot->layerRef(0).transform.targetX = 300; shot->layerRef(0).transform.targetY = 400;
        QVERIFY(near(render::sceneLightsOf(shot.get(), 0).first().direction, QVector3D(1, 0, 0)));
        // Straight down, its up is (-1, 0, 0): its X is (0, 0, -1).
        shot->layerRef(0).transform.targetX = 0; shot->layerRef(0).transform.targetY = 0;
        const QMatrix4x4 down = render::layerTurnAt(shot.get(), shot->layers()[0], 0);
        QVERIFY(near(down.mapVector(QVector3D(0, 1, 0)), QVector3D(-1, 0, 0)));
        QVERIFY(near(down.mapVector(QVector3D(1, 0, 0)), QVector3D(0, 0, -1)));
        // Its own rotation turns it further, after the aim.
        shot->layerRef(0).transform.orientationX = 90;
        QVERIFY(near(render::layerTurnAt(shot.get(), shot->layers()[0], 0).mapVector(QVector3D(0, 0, -1)),
                     down.mapVector(QMatrix4x4(render::layerRotationAt(shot->layers()[0].transform, 0))
                                        .mapVector(QVector3D(0, 0, -1)))));
        shot->layerRef(0).transform.orientationX = 0;
        // The camera at (0, 0, 1000) looks at (100, 0, 0), upright.
        const model3d::Camera camera = render::sceneCameraOf(shot.get(), QSize(320, 180), 0);
        QVERIFY(near((camera.target - camera.position).normalized(), QVector3D(100, 0, -1000).normalized()));
        QVERIFY(camera.up.y() > 0.99f);
        // The point layer's +Z faces its target.
        QVERIFY(near(render::layerTurnAt(shot.get(), shot->layers()[2], 0).mapVector(QVector3D(0, 0, 1)),
                     QVector3D(1, 0, 0)));
        // Towards Layer: the light aims at the point layer.
        shot->layerRef(0).autoOrient = AutoOrient::TowardsLayer;
        shot->layerRef(0).autoOrientLayer = shot->layers()[2].id;
        shot->layerRef(2).transform.position = QPointF(400, 400);
        QVERIFY(near(render::sceneLightsOf(shot.get(), 0).first().direction, QVector3D(1, 0, 0)));
        // Towards Active Camera: the card turns as the camera does.
        shot->layerRef(3).autoOrient = AutoOrient::TowardsActiveCamera;
        const QMatrix4x4 cameraTurn = render::layerTurnAt(shot.get(), shot->layers()[1], 0);
        const QMatrix4x4 cardTurn = render::layerTurnAt(shot.get(), shot->layers()[3], 0);
        for (const QVector3D& axis : {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)})
            QVERIFY(near(cardTurn.mapVector(axis), cameraTurn.mapVector(axis)));

        // Drawn: edge-on at 80 degrees it is a sliver; facing the default
        // camera (the camera layer hidden) it fills the frame across; unlit,
        // the light hidden too.
        shot->layerRef(1).visible = false;
        shot->layerRef(0).visible = false;
        const auto whiteAcross = [&] {
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 10000)) return -1;
            const QByteArray bytes = frames[0][1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                               QImage::Format_RGBA8888);
            int count = 0;
            for (int x = 0; x < 320; ++x) count += image.pixelColor(x, 90).lightness() > 200 ? 1 : 0;
            return count;
        };
        const int facing = whiteAcross();
        shot->layerRef(3).autoOrient = AutoOrient::None;
        const int edgeOn = whiteAcross();
        QVERIFY2(facing > 300 && edgeOn < 120, qPrintable(QStringLiteral("%1 %2").arg(facing).arg(edgeOn)));
        shot->layerRef(3).autoOrient = AutoOrient::TowardsActiveCamera;
        shot->layerRef(1).visible = true;
        shot->layerRef(0).visible = true;

        // The rows.
        QUndoStack history;
        ui::TimelineWidget timeline; timeline.resize(900, 900);
        timeline.setUndoStack(&history); timeline.setComposition(shot);
        timeline.show(); QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        const auto editor = [&](const QString& name) { return timeline.findChild<QWidget*>(name); };
        const int target = int(composition::TransformProperty::Target);
        QVERIFY(editor(QStringLiteral("timelineTransform_0_%1_2").arg(target)));
        QVERIFY(editor(QStringLiteral("timelineTransform_2_%1_0").arg(target)));
        QVERIFY(!editor(QStringLiteral("timelineTransform_3_%1_0").arg(target)));
        const auto choices = [&](const QString& name) {
            QStringList values;
            if (auto* combo = editor(name) ? editor(name)->findChild<QComboBox*>() : nullptr)
                for (int i = 0; i < combo->count(); ++i) values << combo->itemData(i).toString();
            return values.join(',');
        };
        QCOMPARE(choices("timelineAlignment_0_alignment"), QString("0,1,4,5"));
        QCOMPARE(choices("timelineAlignment_2_alignment"), QString("0,4,5"));
        QCOMPARE(choices("timelineAlignment_3_alignment"), QString("0,1,2,5"));
        QCOMPARE(choices("timelineAlignment_4_alignment"), QString("0,1"));
        QVERIFY(editor("timelineAlignment_0_alignmentLayer"));
        QVERIFY(!editor("timelineAlignment_1_alignmentLayer"));
        auto* mode = editor("timelineAlignment_1_alignment")->findChild<QComboBox*>();
        mode->setCurrentIndex(mode->findData(QStringLiteral("1")));
        QCOMPARE(shot->layers()[1].autoOrient, AutoOrient::AlongMotionPath);
        QCOMPARE(history.undoText(), QString("Set Layer Alignment"));
        QTRY_VERIFY(editor("timelineAlignment_1_alignmentAxis"));
        QCOMPARE(choices("timelineAlignment_1_alignmentAxis"), QString("1,2,3"));
        history.undo();
        QCOMPARE(shot->layers()[1].autoOrient, AutoOrient::TowardsTargetPosition);
        if (auto* tree = timeline.findChild<QTreeWidget*>()) {
            for (QTreeWidgetItemIterator it(tree); *it; ++it) {
                const QString group = (*it)->text(0);
                if (group == "Transform" || group == "Light" || group == "Lens") (*it)->setExpanded(group == "Transform");
            }
        }
        QVERIFY(timeline.grab().save(testArtifactPath(QStringLiteral("timeline-alignment.png"))));

        // Saved by the reference's names and read back.
        shot->layerRef(1).transform.targetXCurve.set(0, 100.0);
        shot->layerRef(1).transform.targetXCurve.set(24, -100.0);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("aim.vegfx");
        media::MediaManager media;
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, media).isSuccess());
        {
            QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
            const QString xml = QString::fromUtf8(saved.readAll());
            QVERIFY(xml.contains("<AutoOrient>4</AutoOrient>") && xml.contains("<AutoOrient>5</AutoOrient>")
                    && xml.contains("<AutoOrient>2</AutoOrient>"));
            QVERIFY(xml.contains(QStringLiteral("<AutoOrientLayer>%1</AutoOrientLayer>").arg(shot->layers()[2].id.value())));
            QVERIFY(xml.contains("<Name>target</Name>"));
        }
        composition::Composition loaded; media::MediaManager reloaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &reloaded).isSuccess());
        QCOMPARE(loaded.layers()[0].autoOrient, AutoOrient::TowardsLayer);
        QCOMPARE(loaded.layers()[0].autoOrientLayer.value(), shot->layers()[2].id.value());
        QCOMPARE(loaded.layers()[1].autoOrient, AutoOrient::TowardsTargetPosition);
        QCOMPARE(loaded.layers()[1].transform.targetAt(12, 0), 0.0);
        QCOMPARE(loaded.layers()[2].transform.targetX, 500.0);
        QCOMPARE(loaded.layers()[3].autoOrient, AutoOrient::TowardsActiveCamera);
        QVERIFY(near(render::sceneLightsOf(&loaded, 0).first().direction, QVector3D(1, 0, 0)));
    }
    // Along Motion Path and Towards Layer, as TransformationHelper::
    // AlignToMotion and AlignToTarget turn layers: a 2D bar laid along its
    // diagonal motion (its X, or its Y), whatever its own Rotation, and back
    // to that Rotation once it stops - drawn so and framed so in Layout; a 3D
    // sheet and a light turned the shortest way along their motion; a sheet's
    // front (+Z) and a camera's -Z turned to the layer they follow, the sheet
    // about its own centre.
    void layersFollowTheirMotionAndOtherLayers() {
        using composition::AutoOrient;
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(24, 1); shot->setDurationSeconds(1);
        composition::Clip clip; clip.durationSeconds = 1;
        {
            auto& bar = shot->addLayer("Bar");
            bar.kind = composition::LayerKind::Plane; bar.planeColor = Qt::white; bar.clips << clip;
            bar.transform.scalePercent = QPointF(50, 5);   // 160 x 9
            bar.transform.rotationDegrees = 30;
            bar.transform.positionXCurve.set(0, -50.0); bar.transform.positionXCurve.set(10, 50.0);
            bar.transform.positionYCurve.set(0, -50.0); bar.transform.positionYCurve.set(10, 50.0);
            bar.autoOrient = AutoOrient::AlongMotionPath;
        }
        const composition::Layer& bar = shot->layers()[0];
        QCOMPARE(qRound(render::layerAngleAt(bar, 2)), -45);
        shot->layerRef(0).autoOrientAxis = 2;
        QCOMPARE(qRound(render::layerAngleAt(bar, 2)), 45);
        shot->layerRef(0).autoOrientAxis = 1;
        QCOMPARE(render::layerAngleAt(bar, 20), 30.0);   // still: its own Rotation
        const render::LayerGeometry frame = render::layerGeometry(*shot, bar, nullptr, 2);
        QVERIFY(frame.valid);
        QCOMPARE(qRound(QLineF(frame.quad.at(0), frame.quad.at(1)).angle()), 45);   // up and right
        // Drawn along the diagonal: from its centre (130, 120) up-right, not across.
        {
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 2.0 / 24.0, QSize(320, 180));
            QVERIFY(QTest::qWaitFor([&] { return frames.count() > 0; }, 10000));
            const QByteArray bytes = frames[0][1].toByteArray();
            const QImage image(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                               QImage::Format_RGBA8888);
            QVERIFY(image.copy().save(testArtifactPath(QStringLiteral("align-motion-2d.png"))));
            QVERIFY(image.pixelColor(130, 120).lightness() > 200);
            QVERIFY(image.pixelColor(172, 78).lightness() > 200);
            QVERIFY(image.pixelColor(190, 120).lightness() < 50);
        }

        // In the scene.
        {
            auto& sheet = shot->addLayer("Sheet");
            sheet.kind = composition::LayerKind::Plane; sheet.dimension = composition::LayerDimension::ThreeD;
            sheet.clips << clip;
            sheet.transform.positionYCurve.set(0, 0.0); sheet.transform.positionYCurve.set(10, 100.0);
            sheet.autoOrient = AutoOrient::AlongMotionPath;
            auto& lamp = shot->addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.light.type = composition::LightSettings::Type::Spot;
            lamp.transform.positionXCurve.set(0, 0.0); lamp.transform.positionXCurve.set(10, 100.0);
            lamp.autoOrient = AutoOrient::AlongMotionPath; lamp.autoOrientAxis = 3;
            auto& mark = shot->addLayer("Mark");
            mark.kind = composition::LayerKind::Point; mark.dimension = composition::LayerDimension::ThreeD;
            mark.transform.position = QPointF(500, 0);
            auto& lens = shot->addLayer("Lens");
            lens.kind = composition::LayerKind::Camera; lens.dimension = composition::LayerDimension::ThreeD;
            lens.transform.positionZ = 1000; lens.visible = false;
        }
        const auto near = [](const QVector3D& a, const QVector3D& b) { return (a - b).length() < 1e-3f; };
        // The sheet's X turned up along its rise; the spot's -Z along its run.
        QVERIFY(near(render::layerTurnAt(shot.get(), shot->layers()[1], 2).mapVector(QVector3D(1, 0, 0)),
                     QVector3D(0, 1, 0)));
        QVERIFY(near(render::sceneLightsOf(shot.get(), 2).first().direction, QVector3D(1, 0, 0)));
        // Towards Layer: the sheet's front and the camera's -Z to the point.
        shot->layerRef(1).autoOrient = AutoOrient::TowardsLayer;
        shot->layerRef(1).autoOrientLayer = shot->layers()[3].id;
        shot->layerRef(1).transform.positionYCurve.clear();
        QVERIFY(near(render::layerTurnAt(shot.get(), shot->layers()[1], 0).mapVector(QVector3D(0, 0, 1)),
                     QVector3D(1, 0, 0)));
        const QMatrix4x4 placed = render::layerSheetMatrixAt(shot.get(), shot->layers()[1], 0);
        QVERIFY(near(placed.map(QVector3D()), QVector3D()));
        QVERIFY(near(placed.mapVector(QVector3D(0, 0, 1)).normalized(), QVector3D(1, 0, 0)));
        shot->layerRef(4).autoOrient = AutoOrient::TowardsLayer;
        shot->layerRef(4).autoOrientLayer = shot->layers()[3].id;
        shot->layerRef(4).visible = true;
        const model3d::Camera camera = render::sceneCameraOf(shot.get(), QSize(320, 180), 0);
        QVERIFY(near((camera.target - camera.position).normalized(), QVector3D(500, 0, -1000).normalized()));

        // The Target in the Viewer: a line and a handle of its own, dragged
        // parallel to the view - the light stays where it is.
        auto aimed = std::make_shared<composition::Composition>();
        aimed->setSize(320, 180); aimed->setDurationSeconds(1);
        aimed->addLayer("Camera") = ui::newCameraLayer(*aimed);
        {
            auto& lamp = aimed->addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.light.type = composition::LightSettings::Type::Spot;
            lamp.transform.position = QPointF(100, 0);
            lamp.autoOrient = AutoOrient::TowardsTargetPosition;
            lamp.transform.targetX = -100;
        }
        bool targetFinished = false;
        ui::SceneObjectOverlay* overlayPtr = nullptr;
        ui::SceneObjectOverlay overlay(
            [&] { return ui::SceneObjectOverlay::Scene{aimed.get(), 0, -1}; },
            [&](int) -> std::optional<ui::SceneObjectOverlay::ViewCamera> {
                return ui::SceneObjectOverlay::ViewCamera{render::sceneCameraOf(aimed.get(), aimed->displaySize(), 0), true};
            },
            [](int) {}, [] {}, [] {}, [&] { targetFinished = overlayPtr && overlayPtr->dragsTarget(); });
        overlayPtr = &overlay;
        const ui::SceneObjectOverlay::Drawing shown = overlay.drawing(0);
        QCOMPARE(shown.handles.size(), 2);
        QVERIFY(shown.handles[1].target && shown.handles[1].layer == 1);
        QVERIFY((shown.handles[1].position - QPointF(60, 90)).manhattanLength() < 0.5);
        ui::ViewerPointerEvent press;
        press.button = Qt::LeftButton; press.buttons = Qt::LeftButton;
        press.mapping.view = 0; press.mapping.canvasSize = QSizeF(320, 180);
        press.mapping.imageRect = QRectF(0, 0, 320, 180); press.mapping.viewRect = press.mapping.imageRect;
        press.canvasPos = QPointF(60, 90);
        QVERIFY(overlay.mousePress(press));
        ui::ViewerPointerEvent drag = press; drag.canvasPos = QPointF(90, 90);
        overlay.mouseMove(drag);
        overlay.mouseRelease(drag);
        QVERIFY(targetFinished);
        QVERIFY2(std::abs(aimed->layers()[1].transform.targetX + 70.0) < 0.5,
                 qPrintable(QString::number(aimed->layers()[1].transform.targetX)));
        QCOMPARE(aimed->layers()[1].transform.position, QPointF(100, 0));
    }
    // Shadows: a small sheet that casts them between a light and a red floor
    // that receives them leaves its shadow there - all of the light's
    // Shadow Opacity in its Shadow Color - from a point light (six maps) and
    // a narrow spot (one), and nowhere else; the caster does not shadow
    // itself, and a floor that does not receive shadows gets none.
    void shadowsFallFromLights() {
        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(320, 180); scene->setDurationSeconds(1);
        composition::Clip clip; clip.durationSeconds = 1;
        {
            auto& floor = scene->addLayer("Floor");
            floor.kind = composition::LayerKind::Plane; floor.planeColor = Qt::red;
            floor.dimension = composition::LayerDimension::ThreeD; floor.clips << clip;
            // 40 x 40 at (-100, 0, 100): left of the frame, nearer the camera.
            auto& card = scene->addLayer("Card");
            card.kind = composition::LayerKind::Plane; card.planeColor = Qt::white;
            card.dimension = composition::LayerDimension::ThreeD; card.clips << clip;
            card.transform.position = QPointF(-100, 0); card.transform.positionZ = 100;
            card.transform.scalePercent = QPointF(12.5, 100.0 * 40 / 180);
            card.surface.castShadows = true;
            // From (-300, 0, 400) the card's shadow lands 4/3 its size about
            // (-33, 0, 0): pixels 100..153 across, 63..117 down.
            auto& lamp = scene->addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.transform.position = QPointF(-300, 0); lamp.transform.positionZ = 400;
            lamp.light.shadowOpacity = 100;
        }
        composition::LightSettings& light = scene->layerRef(2).light;
        const auto frame = [&] {
            render::RenderManager manager; manager.setComposition(scene);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 10000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const auto close = [](const QColor& a, const QColor& b) {
            return qAbs(a.red() - b.red()) <= 3 && qAbs(a.green() - b.green()) <= 3 && qAbs(a.blue() - b.blue()) <= 3;
        };
        const QImage plain = frame();
        QVERIFY(plain.pixelColor(127, 90).red() > 200);
        light.castShadows = true;
        QImage shaded = frame();
        QVERIFY(shaded.save(testArtifactPath(QStringLiteral("shadow-point.png"))));
        QCOMPARE(shaded.pixelColor(127, 90), QColor(Qt::black));
        QVERIFY2(close(shaded.pixelColor(200, 90), plain.pixelColor(200, 90)), qPrintable(shaded.pixelColor(200, 90).name()));
        QVERIFY2(close(shaded.pixelColor(127, 30), plain.pixelColor(127, 30)), qPrintable(shaded.pixelColor(127, 30).name()));
        QVERIFY2(close(shaded.pixelColor(10, 90), plain.pixelColor(10, 90)), qPrintable(shaded.pixelColor(10, 90).name()));
        // 80 % opacity: a fifth of the light, taken before the sum is clamped -
        // there 1.04 diffuse and 1.12 specular, so 0.43; a blue shadow.
        light.shadowOpacity = 80;
        const QColor fifth = frame().pixelColor(127, 90);
        QVERIFY2(qAbs(fifth.red() - 110) <= 4 && fifth.green() == 0, qPrintable(fifth.name()));
        light.shadowOpacity = 100; light.shadowColor = Qt::blue;
        QCOMPARE(frame().pixelColor(127, 90), QColor(Qt::blue));
        light.shadowColor = Qt::black;
        // A narrow spot aimed at it: one map, the same shadow.
        light.type = composition::LightSettings::Type::Spot; light.coneAngle = 40;
        scene->layerRef(2).transform.orientationY = qRadiansToDegrees(std::atan2(267.0, 400.0));
        shaded = frame();
        QVERIFY(shaded.save(testArtifactPath(QStringLiteral("shadow-spot.png"))));
        QCOMPARE(shaded.pixelColor(127, 90), QColor(Qt::black));
        QVERIFY(shaded.pixelColor(127, 30).red() > 200);
        // Diffused: the edges soften - more pixels part lit, part dark.
        const auto partial = [](const QImage& image) {
            int count = 0;
            for (int x = 90; x < 165; ++x) {
                const int red = image.pixelColor(x, 90).red();
                if (red > 10 && red < 200) ++count;
            }
            return count;
        };
        const int hard = partial(shaded);
        light.shadowDiffusion = 100;
        const int soft = partial(frame());
        QVERIFY2(soft > hard, qPrintable(QStringLiteral("%1 %2").arg(hard).arg(soft)));
        light.shadowDiffusion = 0;
        // Not received, or not cast: no shadow.
        scene->layerRef(0).surface.receiveShadows = false;
        QVERIFY(frame().pixelColor(127, 90).red() > 200);
        scene->layerRef(0).surface.receiveShadows = true;
        scene->layerRef(1).surface.castShadows = false;
        QVERIFY(frame().pixelColor(127, 90).red() > 200);
    }
    // A model's material lit by its own illumination model, as Flux's shader
    // does: Phong with the material's Shininess and its specular colour whole,
    // Cook-Torrance, Metallic-Roughness and Specular-Glossiness; and glTF
    // materials read as the reference's importer makes them.
    void modelMaterialsFollowTheirIlluminationModel() {
        using model3d::IlluminationModel;
        model3d::Lighting lighting;
        model3d::Light ahead; ahead.type = model3d::Light::Type::Directional;   // 2.5, down -Z
        lighting.lights = {ahead};
        const QVector3D n(0, 0, 1), eye(0, 0, 1000), at;
        const QVector4D white(1, 1, 1, 1), black(0, 0, 0, 1), dim(0.1f, 0.1f, 0.1f, 1);
        const auto lit = [&](const model3d::Reflectance& r, const QVector4D& diffuse, const QVector4D& specular) {
            return double(model3d::illuminate(lighting, black, diffuse, specular, QVector3D(), at, n, eye, &r).x());
        };
        // Phong: 0.5 of the diffuse colour, the specular colour whole.
        model3d::Reflectance phong; phong.shininess = 4;
        QVERIFY(std::abs(lit(phong, dim, black) - 0.1 * 0.5 * 2.5) < 1e-4);
        QVERIFY(std::abs(lit(phong, black, dim) - 0.1 * 2.5) < 1e-4);
        // Metallic-Roughness head on, roughness 1: a dielectric white gives
        // (1 - 0.04) * 0.96 / pi + 0.04 * 0.25 / pi; a metal only its F * vis * D.
        model3d::Reflectance metal; metal.model = IlluminationModel::MetallicRoughness; metal.roughness = 1;
        const double dielectric = (0.96 * 0.96 / M_PI + 0.04 * 0.25 / M_PI) * 2.5;
        QVERIFY2(std::abs(lit(metal, white, white) - dielectric) < 1e-4, qPrintable(QString::number(lit(metal, white, white))));
        metal.metallic = 1;
        QVERIFY(std::abs(lit(metal, white, white) - 0.25 / M_PI * 2.5) < 1e-4);
        // Specular-Glossiness: the diffuse colour times 1 - the specular's
        // largest channel, the specular colour as F0.
        model3d::Reflectance gloss; gloss.model = IlluminationModel::SpecularGlossiness; gloss.roughness = 1;
        const QVector4D half(0.5f, 0.5f, 0.5f, 1);
        const double fresnel = 0.5;
        const double sg = ((1 - fresnel) * 0.5 / M_PI + fresnel * 0.25 / M_PI) * 2.5;
        QVERIFY(std::abs(lit(gloss, white, half) - sg) < 1e-4);
        // Cook-Torrance head on: Fresnel F0 * Beckmann 1 / (4 m^2) * the
        // specular colour * specularReflectance, at a tenth of the intensity.
        lighting.lights[0].intensity = 0.1;
        model3d::Reflectance cook; cook.model = IlluminationModel::CookTorrance;
        const double expected = 0.91318 * (1.0 / (4 * 0.05 * 0.05)) * 0.1 * 0.05 * 0.1;
        QVERIFY2(std::abs(lit(cook, black, dim) - expected) < 1e-3, qPrintable(QString::number(lit(cook, black, dim))));
        // An ambient light lights the split colour of the physically based
        // models, the ambient colour of the others.
        model3d::Light fill; fill.type = model3d::Light::Type::Ambient; fill.intensity = 1.0;
        lighting.lights = {fill};
        metal.metallic = 0;
        QVERIFY(std::abs(lit(metal, white, white) - 0.96) < 1e-4);
        QVERIFY(std::abs(lit(phong, white, white)) < 1e-6);   // ambient colour black

        // glTF: Metallic-Roughness with an alpha mask, Specular-Glossiness by
        // its extension; colours in sRGB as the reference stores them.
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QByteArray buffer;
        {
            QDataStream out(&buffer, QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);
            out.setFloatingPointPrecision(QDataStream::SinglePrecision);
            const float positions[] = {-80, -80, 0, 80, -80, 0, 80, 80, 0, -80, 80, 0};
            for (float v : positions) out << v;
            for (int i = 0; i < 4; ++i) out << 0.0f << 0.0f << 1.0f;
            const float uvs[] = {0, 1, 1, 1, 1, 0, 0, 0};
            for (float v : uvs) out << v;
            for (quint16 i : {0, 1, 2, 0, 2, 3}) out << i;
        }
        // Left texels nearly clear, right ones nearly solid.
        QImage texture(2, 1, QImage::Format_RGBA8888);
        texture.setPixelColor(0, 0, QColor(255, 0, 0, 40)); texture.setPixelColor(1, 0, QColor(255, 0, 0, 230));
        QByteArray png;
        { QBuffer device(&png); device.open(QIODevice::WriteOnly); texture.save(&device, "PNG"); }
        const auto view = [](int offset, int length) {
            return QJsonObject{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
        };
        const auto accessor = [](int view, int type, int count, const char* kind) {
            return QJsonObject{{"bufferView", view}, {"componentType", type}, {"count", count}, {"type", kind}};
        };
        const QJsonObject attributes{{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}};
        const QJsonObject root{
            {"asset", QJsonObject{{"version", "2.0"}}},
            {"scene", 0},
            {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}},
            {"nodes", QJsonArray{QJsonObject{{"name", "Quad"}, {"mesh", 0}}}},
            {"meshes", QJsonArray{QJsonObject{{"primitives", QJsonArray{
                QJsonObject{{"attributes", attributes}, {"indices", 3}, {"material", 0}}}}}}},
            {"materials", QJsonArray{
                QJsonObject{{"name", "Cut"}, {"alphaMode", "MASK"}, {"alphaCutoff", 0.5},
                            {"occlusionTexture", QJsonObject{{"index", 0}, {"strength", 0.4}}},
                            {"pbrMetallicRoughness", QJsonObject{
                                {"baseColorFactor", QJsonArray{0.5, 1, 1, 1}}, {"metallicFactor", 0.25},
                                {"roughnessFactor", 0.75}, {"baseColorTexture", QJsonObject{{"index", 0}}}}}},
                QJsonObject{{"name", "Gloss"}, {"extensions", QJsonObject{{"KHR_materials_pbrSpecularGlossiness",
                    QJsonObject{{"diffuseFactor", QJsonArray{1, 1, 1, 0.5}}, {"specularFactor", QJsonArray{0.2, 0.2, 0.2}},
                                {"glossinessFactor", 0.3}}}}}}}},
            {"textures", QJsonArray{QJsonObject{{"source", 0}}}},
            {"images", QJsonArray{QJsonObject{{"uri", QString("data:image/png;base64," + png.toBase64())}}}},
            {"buffers", QJsonArray{QJsonObject{{"byteLength", int(buffer.size())},
                                               {"uri", QString("data:application/octet-stream;base64," + buffer.toBase64())}}}},
            {"bufferViews", QJsonArray{view(0, 48), view(48, 48), view(96, 32), view(128, 12)}},
            {"accessors", QJsonArray{accessor(0, 5126, 4, "VEC3"), accessor(1, 5126, 4, "VEC3"),
                                     accessor(2, 5126, 4, "VEC2"), accessor(3, 5123, 6, "SCALAR")}},
        };
        {
            QFile file(dir.filePath("materials.gltf")); QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        }
        model3d::ImportSettings settings; settings.centerAnchorPoint = false; settings.autoNormalize = false;
        const auto loaded = model3d::loadModel(dir.filePath("materials.gltf"), settings);
        QVERIFY2(loaded.ok, qPrintable(loaded.message));
        QVERIFY(loaded.mesh.materials.size() >= 2);
        const model3d::Material& cut = loaded.mesh.materials[0];
        QCOMPARE(cut.illuminationModel, IlluminationModel::MetallicRoughness);
        QCOMPARE(cut.alphaMode, model3d::AlphaMode::Mask);
        QCOMPARE(cut.alphaCutOff, 0.5);
        QCOMPARE(cut.occlusionStrength, 0.4);
        QCOMPARE(cut.metallic, 0.25);
        QCOMPARE(cut.roughness, 0.75);
        QVERIFY(qAbs(cut.diffuseColor.redF() - 0.7354) < 0.002);   // 0.5 in sRGB
        QCOMPARE(cut.specularColor, QColor(Qt::white));
        const model3d::Material& gloss2 = loaded.mesh.materials[1];
        QCOMPARE(gloss2.illuminationModel, IlluminationModel::SpecularGlossiness);
        QCOMPARE(gloss2.glossiness, 0.3);
        QCOMPARE(gloss2.opacity, 0.5);
        QVERIFY(qAbs(gloss2.specularColor.redF() - 0.4845) < 0.002);   // 0.2 in sRGB
        // Mask: the clear half is cut away, the other drawn wholly.
        const QSize size(200, 200);
        const QImage image = model3d::renderMesh(loaded.mesh, QMatrix4x4(), model3d::defaultCameraForCanvas(size), size,
                                                 model3d::Lighting(), model3d::ShadingMode::Shaded, 1.0);
        QCOMPARE(image.pixelColor(40, 100).alpha(), 0);
        QCOMPARE(image.pixelColor(160, 100).alpha(), 255);
    }
    // The Materials tab of a model's properties edits its materials - rows by
    // Illumination Model - the asset keeps the edits over the file's own
    // (through another import too), and a project saves them as the
    // reference's <Material Version="7"> and reads them back.
    void modelMaterialsAreEditedAndSaved() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        {
            QFile mtl(temp.filePath("quad.mtl")); QVERIFY(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
            mtl.write("newmtl Paint\nKd 1 0 0\nKa 0 0 0\nKs 0.5 0.5 0.5\nNs 10\n");
            QFile obj(temp.filePath("quad.obj")); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            obj.write("mtllib quad.mtl\nv -80 -80 0\nv 80 -80 0\nv 80 80 0\nv -80 80 0\nvn 0 0 1\n"
                      "usemtl Paint\nf 1//1 2//1 3//1\nf 1//1 3//1 4//1\n");
        }
        const QString file = QFileInfo(temp.filePath("quad.obj")).absoluteFilePath();
        model3d::ImportSettings settings; settings.centerAnchorPoint = false; settings.autoNormalize = false;
        QVector<model3d::Material> edited;
        {
            ui::Model3DSettingsDialog dialog(file, settings);
            const auto editor = [&](const QString& key) {
                return dialog.findChild<QWidget*>(QStringLiteral("materialRow_0_") + key);
            };
            QVERIFY(editor("illuminationModel") && editor("shininess") && editor("ambientColor"));
            QVERIFY(!editor("fresnel"));
            QVERIFY(dialog.editedMaterials().isEmpty());
            auto* model = editor("illuminationModel")->findChild<QComboBox*>(); QVERIFY(model);
            QCOMPARE(model->count(), 4);
            model->setCurrentIndex(model->findData(QStringLiteral("1")));   // Cook-Torrance
            QTRY_VERIFY(editor("fresnel") != nullptr);
            QVERIFY(!editor("shininess"));
            auto* reflectance = editor("specularReflectance")->findChild<QDoubleSpinBox*>(); QVERIFY(reflectance);
            QCOMPARE(reflectance->value(), 5.0);
            reflectance->setValue(20);
            edited = dialog.editedMaterials();
            QCOMPARE(edited.size(), 1);
            QCOMPARE(edited[0].illuminationModel, model3d::IlluminationModel::CookTorrance);
            QCOMPARE(edited[0].cookTorranceSpecularReflectance, 0.2);
            // Read again with other settings, the edits stay.
            auto* flip = dialog.findChildren<QCheckBox*>().value(0); QVERIFY(flip);
            flip->setChecked(!flip->isChecked());
            QVERIFY(QMetaObject::invokeMethod(&dialog, "updatePreview"));
            QCOMPARE(dialog.editedMaterials().size(), 1);
            QTRY_VERIFY(editor("fresnel") != nullptr);
            QVERIFY(dialog.grab().save(testArtifactPath(QStringLiteral("model-materials-tab.png"))));
        }
        // The asset lays them over the file's material, through re-imports;
        // none brings the file's back.
        media::MediaManager media;
        QVERIFY(media.importModel(file, settings).isSuccess());
        const core::Identifier id = media.assetByFilePath(file).id();
        QCOMPARE(media.modelMesh(id).materials.value(0).illuminationModel, model3d::IlluminationModel::Phong);
        media.setModelMaterials(id, edited);
        QCOMPARE(media.modelMesh(id).materials.value(0).illuminationModel, model3d::IlluminationModel::CookTorrance);
        QVERIFY(media.importModel(file, settings).isSuccess());
        QCOMPARE(media.modelMesh(id).materials.value(0).cookTorranceSpecularReflectance, 0.2);
        QCOMPARE(media.modelMesh(id).materials.value(0).diffuseColor, QColor(Qt::red));
        // Saved under the model layer and read back.
        composition::Composition shot; shot.setSize(320, 180);
        auto& layer = shot.addLayer("Quad");
        layer.kind = composition::LayerKind::Model3D; layer.dimension = composition::LayerDimension::ThreeD;
        layer.modelAssetId = id;
        const QString path = temp.filePath("materials.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, shot, media).isSuccess());
        {
            QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
            const QString xml = QString::fromUtf8(saved.readAll());
            QVERIFY(xml.contains("<Material Version=\"7\">"));
            QVERIFY(xml.contains("<IlluminationModel>1</IlluminationModel>"));
            QVERIFY(xml.contains("<CK_SpecularReflectance>20</CK_SpecularReflectance>"));
            QVERIFY(xml.contains("<Opacity>100</Opacity>"));
            QDomDocument document; QVERIFY(bool(document.setContent(xml)));
            const QDomElement diffuse = document.elementsByTagName("Material").item(0).toElement()
                                            .firstChildElement("DiffuseColor");
            QCOMPARE(diffuse.attribute("R"), QString("1"));
            QCOMPARE(diffuse.attribute("G"), QString("0"));
            QCOMPARE(diffuse.attribute("A"), QString("1"));
        }
        composition::Composition loaded; media::MediaManager loadedMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &loadedMedia).isSuccess());
        const model3d::Material back = loadedMedia.modelMesh(loaded.layers().value(0).modelAssetId).materials.value(0);
        QCOMPARE(back.illuminationModel, model3d::IlluminationModel::CookTorrance);
        QVERIFY(std::abs(back.cookTorranceSpecularReflectance - 0.2) < 1e-9);
        QCOMPARE(back.shininess, 10.0);
        media.setModelMaterials(id, {});
        QCOMPARE(media.modelMesh(id).materials.value(0).illuminationModel, model3d::IlluminationModel::Phong);
    }
    // The shot's cameras, lights and points in the Viewer (the reference's
    // CameraLayerControl, LightLayerControl, PointLayerControl): a frustum
    // for a camera other than the one a view looks through, a light's icon
    // with the circle of its reach or its cone, in every view through its
    // camera; a handle picks its layer and drags it parallel to the view.
    void sceneObjectsShowInTheViewer() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setDurationSeconds(1);
        {
            auto camera = ui::newCameraLayer(*shot);
            shot->addLayer("Camera") = camera;
            shot->layerRef(0).name = "Camera";
            auto& lamp = shot->addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.transform.position = QPointF(100, 0);
            lamp.light.falloff = composition::LightSettings::Falloff::Linear; lamp.light.reach = 60;
            auto& spot = shot->addLayer("Spot");
            spot.kind = composition::LayerKind::Light; spot.dimension = composition::LayerDimension::ThreeD;
            spot.light.type = composition::LightSettings::Type::Spot;
            spot.transform.position = QPointF(-100, 0);
            auto& point = shot->addLayer("Point");
            point.kind = composition::LayerKind::Point; point.dimension = composition::LayerDimension::ThreeD;
            point.transform.position = QPointF(0, -50);
        }
        int picked = -1, began = 0, changed = 0, finished = 0;
        ui::SceneObjectOverlay overlay(
            [&] { return ui::SceneObjectOverlay::Scene{shot.get(), 0, picked}; },
            [&](int view) -> std::optional<ui::SceneObjectOverlay::ViewCamera> {
                const QSize canvas = shot->displaySize();
                if (view == 1)
                    return ui::SceneObjectOverlay::ViewCamera{
                        render::viewCamera(render::ViewType::Top, render::defaultViewCameraState(render::ViewType::Top),
                                           shot.get(), canvas, 0), false};
                return ui::SceneObjectOverlay::ViewCamera{render::sceneCameraOf(shot.get(), canvas, 0), true};
            },
            [&](int layer) { picked = layer; }, [&] { ++began; }, [&] { ++changed; }, [&] { ++finished; });
        QVERIFY(overlay.isActive());
        // Through the shot's camera (1:1 at z = 0): no frustum of its own;
        // the light at (260, 90) with its reach as a 60 pixel circle, the
        // spot and the point at theirs.
        const ui::SceneObjectOverlay::Drawing through = overlay.drawing(0);
        QCOMPARE(through.handles.size(), 3);
        QCOMPARE(through.handles[0].layer, 1);
        QVERIFY((through.handles[0].position - QPointF(260, 90)).manhattanLength() < 0.5);
        QVERIFY(through.handles[0].light && !through.handles[2].light);
        double farthest = 0.0;
        for (const QLineF& line : through.lines)
            if (line.p1().x() > 160) farthest = qMax(farthest, QLineF(line.p1(), QPointF(260, 90)).length());
        QVERIFY2(std::abs(farthest - 60.0) < 0.5, qPrintable(QString::number(farthest)));
        QCOMPARE(overlay.layerAt(0, QPointF(263, 92)), 1);
        QCOMPARE(overlay.layerAt(0, QPointF(160, 20)), -1);
        // From the top: the camera's frustum and its handle too.
        const ui::SceneObjectOverlay::Drawing top = overlay.drawing(1);
        QCOMPARE(top.handles.size(), 4);
        QVERIFY(top.lines.size() > through.lines.size());
        QCOMPARE(top.handles[0].layer, 0);

        // A drag of the light 30 right and 20 down moves it in the plane of
        // the view, as one change.
        ui::ViewerPointerEvent press;
        press.button = Qt::LeftButton; press.buttons = Qt::LeftButton;
        press.mapping.view = 0; press.mapping.canvasSize = QSizeF(320, 180);
        press.mapping.imageRect = QRectF(0, 0, 320, 180); press.mapping.viewRect = press.mapping.imageRect;
        press.canvasPos = QPointF(260, 90);
        QVERIFY(overlay.mousePress(press));
        QCOMPARE(picked, 1);
        ui::ViewerPointerEvent move = press; move.canvasPos = QPointF(290, 110);
        overlay.mouseMove(move);
        overlay.mouseRelease(move);
        QCOMPARE(began, 1); QVERIFY(changed >= 1); QCOMPARE(finished, 1);
        const QPointF moved = shot->layers()[1].transform.position;
        QVERIFY2((moved - QPointF(130, -20)).manhattanLength() < 0.5,
                 qPrintable(QStringLiteral("%1 %2").arg(moved.x()).arg(moved.y())));
        QVERIFY(std::abs(shot->layers()[1].transform.positionZ) < 1e-3);
        // From the top, a drag down the view moves it towards the front (+Z).
        ui::ViewerPointerEvent fromTop = press;
        fromTop.mapping.view = 1;
        const ui::SceneObjectOverlay::Drawing seen = overlay.drawing(1);
        fromTop.canvasPos = seen.handles[2].position;   // the spot
        QVERIFY(overlay.mousePress(fromTop));
        QCOMPARE(picked, 2);
        ui::ViewerPointerEvent down = fromTop; down.canvasPos += QPointF(0, 25);
        overlay.mouseMove(down);
        overlay.mouseRelease(down);
        QVERIFY(shot->layers()[2].transform.positionZ > 1.0);
        QVERIFY(std::abs(shot->layers()[2].transform.position.y()) < 1e-3);

        // In the Viewer: drawn in the reference's blue.
        ui::ViewerWidget viewer;
        viewer.resize(344, 180); viewer.show(); QTest::qWait(20);
        viewer.setProjectSize(QSize(320, 180));
        QImage blank(320, 180, QImage::Format_ARGB32_Premultiplied); blank.fill(Qt::black);
        viewer.setFrame(blank);
        viewer.addOverlay(&overlay);
        QTest::qWait(20);
        const QImage grab = viewer.grab().toImage();
        QVERIFY(grab.save(testArtifactPath(QStringLiteral("viewer-scene-objects.png"))));
        int blue = 0;
        for (int y = 0; y < grab.height(); ++y)
            for (int x = 0; x < grab.width(); ++x) {
                const QColor c = grab.pixelColor(x, y);
                if (c.blue() > 200 && c.red() < 110 && c.green() > 90 && c.green() < 170) ++blue;
            }
        QVERIFY2(blue > 100, qPrintable(QString::number(blue)));
        viewer.removeOverlay(&overlay);
    }
    void modelMapsShadeTheSurface() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto solid = [&](const QString& name, const QColor& color) {
            QImage image(4, 4, QImage::Format_RGBA8888); image.fill(color);
            QVERIFY(image.save(dir.filePath(name)));
        };
        solid("green.png", Qt::green); solid("black.png", Qt::black); solid("white.png", Qt::white);
        solid("flat.png", QColor(128, 128, 255)); solid("tilted.png", QColor(218, 128, 218));
        {
            // A height rising along u: a slope the bump map tilts the face by.
            QImage ramp(64, 4, QImage::Format_RGBA8888);
            for (int x = 0; x < 64; ++x) for (int y = 0; y < 4; ++y) ramp.setPixelColor(x, y, QColor(x * 4, x * 4, x * 4));
            QVERIFY(ramp.save(dir.filePath("ramp.png")));
        }
        const auto load = [&](const QByteArray& material) {
            QFile mtl(dir.filePath("quad.mtl")); mtl.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
            mtl.write("newmtl m\n" + material + "\n"); mtl.close();
            QFile obj(dir.filePath("quad.obj")); obj.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
            obj.write("mtllib quad.mtl\nv -80 -80 0\nv 80 -80 0\nv 80 80 0\nv -80 80 0\n"
                      "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                      "usemtl m\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
            obj.close();
            model3d::ImportSettings settings;
            settings.centerAnchorPoint = false; settings.autoNormalize = false;
            return model3d::loadModel(dir.filePath("quad.obj"), settings);
        };
        const QSize size(200, 200);
        const auto draw = [&](const model3d::Mesh& mesh, const model3d::Lighting& light) {
            return model3d::renderMesh(mesh, QMatrix4x4(), model3d::defaultCameraForCanvas(size), size, light,
                                       model3d::ShadingMode::Shaded, 1.0).pixelColor(100, 100);
        };
        // A light that gives nothing: the surface is lit (its emissive colour
        // shows), by nothing.
        model3d::Lighting dark; dark.lights = {model3d::Light()}; dark.lights[0].intensity = 0.0;
        // Straight on from the camera, at twice the reference's 100 %, so a
        // facing surface (Diffuse 50 %) takes exactly its own colour.
        model3d::Lighting front; front.lights = {model3d::Light()};
        front.lights[0].type = model3d::Light::Type::Directional; front.lights[0].intensity = 2.0;

        // Emissive: a black emissive colour with a map glows with the map.
        auto loaded = load("Kd 0 0 0\nKa 0 0 0\nKs 0 0 0\nKe 0 0 0\nmap_Ke green.png");
        QVERIFY(loaded.ok);
        QColor c = draw(loaded.mesh, dark);
        QVERIFY2(c.green() > 240 && c.red() < 10, qPrintable(c.name()));

        // Specular: a black map takes the highlight away.
        loaded = load("Kd 0 0 0\nKa 0 0 0\nKs 1 1 1\nNs 4\nmap_Ks white.png");
        const int highlight = draw(loaded.mesh, front).red();
        loaded = load("Kd 0 0 0\nKa 0 0 0\nKs 1 1 1\nNs 4\nmap_Ks black.png");
        QVERIFY2(highlight > 200 && draw(loaded.mesh, front).red() < 10, qPrintable(QString::number(highlight)));

        // Occlusion: the ambient term times the map.
        model3d::Lighting ambient; ambient.lights = {model3d::Light()};
        ambient.lights[0].type = model3d::Light::Type::Ambient; ambient.lights[0].intensity = 1.0;
        loaded = load("Kd 0 0 0\nKa 1 1 1\nKs 0 0 0\nmap_Ka black.png");
        QVERIFY(draw(loaded.mesh, ambient).red() < 10);
        loaded = load("Kd 0 0 0\nKa 1 1 1\nKs 0 0 0\nmap_Ka white.png");
        QVERIFY(draw(loaded.mesh, ambient).red() > 240);

        // Normal: a flat map changes nothing; one leaning 45 degrees along u
        // turns the face from the light: cos 45 of it.
        loaded = load("Kd 1 1 1\nKa 0 0 0\nKs 0 0 0");
        const int lit = draw(loaded.mesh, front).red();
        loaded = load("Kd 1 1 1\nKa 0 0 0\nKs 0 0 0\nnorm flat.png");
        QVERIFY(!loaded.mesh.materials[0].normalImage.isNull());
        QVERIFY(qAbs(draw(loaded.mesh, front).red() - lit) <= 2);
        loaded = load("Kd 1 1 1\nKa 0 0 0\nKs 0 0 0\nnorm tilted.png");
        const int leaning = draw(loaded.mesh, front).red();
        QVERIFY2(qAbs(leaning - qRound(lit * 0.7071)) <= 8, qPrintable(QStringLiteral("%1 of %2").arg(leaning).arg(lit)));

        // Bump: a slope tilts the face too, more with -bm; the scale is read.
        loaded = load("Kd 1 1 1\nKa 0 0 0\nKs 0 0 0\nbump -bm 2 ramp.png");
        QCOMPARE(loaded.mesh.materials[0].bumpScale, 2.0);
        QVERIFY(loaded.mesh.materials[0].normalMap.isEmpty());
        const int bumped = draw(loaded.mesh, front).red();
        loaded = load("Kd 1 1 1\nKa 0 0 0\nKs 0 0 0\nbump -bm 8 ramp.png");
        const int steeper = draw(loaded.mesh, front).red();
        QVERIFY2(bumped < lit && steeper < bumped,
                 qPrintable(QStringLiteral("%1 %2 %3").arg(lit).arg(bumped).arg(steeper)));

        // Override Materials: the emissive map from a hidden layer.
        loaded = load("Kd 0 0 0\nKa 0 0 0\nKs 0 0 0\nKe 0 0 0");
        auto media = std::make_shared<media::MediaManager>();
        model3d::ImportSettings settings; settings.centerAnchorPoint = false; settings.autoNormalize = false;
        QVERIFY(media->importModel(dir.filePath("quad.obj"), settings).isSuccess());
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 200); shot->setDurationSeconds(1);
        auto& model = shot->addLayer("Quad");
        model.kind = composition::LayerKind::Model3D;
        model.modelAssetId = media->assetByFilePath(QFileInfo(dir.filePath("quad.obj")).absoluteFilePath()).id();
        auto& plane = shot->addLayer("Magenta");
        plane.kind = composition::LayerKind::Plane; plane.planeColor = QColor(255, 0, 255); plane.visible = false;
        composition::Clip clip; clip.durationSeconds = 1; plane.clips.append(clip);
        // Emissive shows on a lit surface: a light that gives nothing.
        auto& lamp = shot->addLayer("Light");
        lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
        lamp.light.intensity = 0.0;
        composition::ModelMaterialOverride entry; entry.material = "m";
        entry.map(composition::ModelMaterialOverride::Map::Emissive) = {
            composition::ModelMaterialOverride::MapSource::UseLayer, shot->layers()[1].id};
        shot->layerRef(0).materialOverrides.append(entry);
        render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 0, size);
        QVERIFY(QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000));
        const QByteArray bytes = frames.last()[1].toByteArray();
        const QImage frame(reinterpret_cast<const uchar*>(bytes.constData()), 200, 200, 800, QImage::Format_RGBA8888);
        c = frame.pixelColor(100, 100);
        QVERIFY2(c.red() > 240 && c.blue() > 240 && c.green() < 10, qPrintable(c.name()));
    }
    // Override Materials: a model material's diffuse map from its file, from
    // nowhere, or from a layer's picture (Model3DMaterialOverride, "Use
    // Layer") - set in the timeline, drawn by the renderer, saved.
    void modelMaterialsTakeLayers() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage texture(2, 2, QImage::Format_RGBA8888);
        texture.setPixelColor(0, 1, Qt::red); texture.setPixelColor(1, 1, Qt::green);
        texture.setPixelColor(0, 0, Qt::blue); texture.setPixelColor(1, 0, Qt::white);
        QVERIFY(texture.save(dir.filePath("checker.png")));
        {
            QFile mtl(dir.filePath("quad.mtl")); QVERIFY(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
            mtl.write("newmtl tex\nKd 1 1 1\nKa 0 0 0\nKs 0 0 0\nmap_Kd checker.png\n");
            QFile obj(dir.filePath("quad.obj")); QVERIFY(obj.open(QIODevice::WriteOnly | QIODevice::Text));
            obj.write("mtllib quad.mtl\nv -80 -80 0\nv 80 -80 0\nv 80 80 0\nv -80 80 0\n"
                      "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                      "usemtl tex\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
        }
        model3d::ImportSettings settings;
        settings.centerAnchorPoint = false; settings.autoNormalize = false;
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importModel(dir.filePath("quad.obj"), settings).isSuccess());
        const core::Identifier modelId =
            media->assetByFilePath(QFileInfo(dir.filePath("quad.obj")).absoluteFilePath()).id();
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(200, 200); shot->setDurationSeconds(1);
        {
            auto& model = shot->addLayer("Quad");
            model.kind = composition::LayerKind::Model3D; model.modelAssetId = modelId;
            auto& plane = shot->addLayer("Magenta");
            plane.kind = composition::LayerKind::Plane; plane.planeColor = QColor(255, 0, 255);
            plane.visible = false;   // only lent to the material
            composition::Clip clip; clip.durationSeconds = 1; plane.clips.append(clip);
        }
        const auto frame = [&] {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0, QSize(200, 200));
            if (!QTest::qWaitFor([&] { return frames.count() >= 1; }, 5000)) return QImage();
            const QByteArray bytes = frames.last()[1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 200, 200, 200 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        const auto hue = [](const QImage& image, int x, int y) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() > 120 && c.blue() > 120 && c.green() < c.red() / 3) return 'm';
            if (c.red() > 120 && c.green() < c.red() / 3 && c.blue() < c.red() / 3) return 'r';
            if (c.green() > 120 && c.red() < c.green() / 3 && c.blue() < c.green() / 3) return 'g';
            if (c.red() > 120 && qAbs(c.red() - c.green()) < 20 && qAbs(c.red() - c.blue()) < 20) return 'w';
            return '?';
        };
        // From the file: the checker.
        QImage image = frame();
        QVERIFY(!image.isNull());
        QCOMPARE(hue(image, 60, 140), 'r');
        QCOMPARE(hue(image, 140, 140), 'g');

        // The timeline: Override Materials > tex > Diffuse Map Source / Layer.
        panel->setMediaManager(media); panel->setComposition(shot); QTest::qWait(20);
        const auto combo = [&](const QString& name) { return panel->findChild<QComboBox*>(name); };
        QComboBox* source = combo("timelineMaterialSource_0_0_diffuse");
        QComboBox* layerPicker = combo("timelineMaterialLayer_0_0_diffuse");
        QVERIFY(source && layerPicker);
        QCOMPARE(source->currentData().toInt(), int(composition::ModelMaterialOverride::MapSource::FromFile));
        QVERIFY(!layerPicker->isEnabled());
        QCOMPARE(layerPicker->count(), 2);   // None and the plane - not the model itself
        const int steps = history.count();
        source->setCurrentIndex(2); emit source->activated(2);
        QTRY_VERIFY((layerPicker = combo("timelineMaterialLayer_0_0_diffuse")) && layerPicker->isEnabled());
        layerPicker->setCurrentIndex(1); emit layerPicker->activated(1);
        QCOMPARE(history.count(), steps + 2);
        const composition::ModelMaterialOverride* entry = shot->layers()[0].materialOverride("tex");
        QVERIFY(entry);
        QCOMPARE(entry->map(composition::ModelMaterialOverride::Map::Diffuse).source,
                 composition::ModelMaterialOverride::MapSource::UseLayer);
        QCOMPARE(entry->map(composition::ModelMaterialOverride::Map::Diffuse).layer, shot->layers()[1].id);

        // Drawn with the plane's picture, though the plane itself is hidden.
        image = frame();
        image.save(testArtifactPath("model-material-layer.png"));
        QCOMPARE(hue(image, 60, 140), 'm');
        QCOMPARE(hue(image, 140, 60), 'm');
        QVERIFY(hue(image, 5, 5) != 'm');   // the background, not the plane

        // Saved and read back.
        const QString path = dir.filePath("materials.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, *shot, *media).isSuccess());
        composition::Composition loaded; media::MediaManager loadedMedia;
        QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &loadedMedia).isSuccess());
        QCOMPARE(loaded.layers().size(), 2);
        const composition::ModelMaterialOverride* read = loaded.layers()[0].materialOverride("tex");
        QVERIFY(read);
        QCOMPARE(read->map(composition::ModelMaterialOverride::Map::Diffuse).source,
                 composition::ModelMaterialOverride::MapSource::UseLayer);
        QCOMPARE(read->map(composition::ModelMaterialOverride::Map::Diffuse).layer, loaded.layers()[1].id);

        // None: the material's colour alone.
        shot->layerRef(0).materialOverride("tex")->map(composition::ModelMaterialOverride::Map::Diffuse).source =
            composition::ModelMaterialOverride::MapSource::None;
        image = frame();
        QCOMPARE(hue(image, 60, 140), 'w');
        QCOMPARE(hue(image, 140, 140), 'w');

        // Undone: back to the file's map.
        history.undo(); history.undo();
        QVERIFY(!shot->layers()[0].materialOverride("tex")
                || shot->layers()[0].materialOverride("tex")->map(composition::ModelMaterialOverride::Map::Diffuse).source
                       == composition::ModelMaterialOverride::MapSource::FromFile);
        panel->setComposition(std::make_shared<composition::Composition>());
    }
    void sceneIsMultisampled() {
        const int expected[] = {4, 8, 16, 32, 4, 8, 4, 8, 8};
        for (int mode = 1; mode <= 9; ++mode)
            QCOMPARE(model3d::antialiasingSamples(mode), expected[mode - 1]);
        for (const int n : {1, 4, 8, 16, 32, 6}) {
            const QVector<QPointF> positions = model3d::samplePositions(n);
            QCOMPARE(positions.size(), n);
            QSet<QPair<int, int>> distinct;
            for (const QPointF& at : positions) {
                QVERIFY(at.x() >= 0.0 && at.x() < 1.0 && at.y() >= 0.0 && at.y() < 1.0);
                distinct.insert({qRound(at.x() * 64), qRound(at.y() * 64)});
            }
            QCOMPARE(distinct.size(), n);
        }

        // A triangle on the z = 0 plane, which the default camera shows at
        // frame size: 9750 px of it.
        model3d::Mesh mesh;
        mesh.positions = {QVector3D(-70, -60, 0), QVector3D(80, -45, 0), QVector3D(-20, 75, 0)};
        model3d::Triangle triangle;
        triangle.position[0] = 0; triangle.position[1] = 1; triangle.position[2] = 2;
        mesh.triangles = {triangle};
        const QSize size(200, 200);
        const model3d::Camera camera = model3d::defaultCameraForCanvas(size);
        const auto draw = [&](int samples) {
            return model3d::renderMesh(mesh, QMatrix4x4(), camera, size, model3d::Lighting(),
                                       model3d::ShadingMode::Shaded, 1.0, model3d::Fog(), samples);
        };
        const auto measure = [&](const QImage& image, double* coverage) {
            int partial = 0;
            *coverage = 0.0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const int alpha = image.pixelColor(x, y).alpha();
                    *coverage += alpha / 255.0;
                    if (alpha > 0 && alpha < 255) ++partial;
                }
            return partial;
        };
        const double area = 9750.0;
        const QImage single = draw(1);
        double coverage = 0.0;
        QCOMPARE(measure(single, &coverage), 0);
        QString errors;
        for (const int n : {4, 8, 16, 32}) {
            const QImage image = draw(n);
            const int partial = measure(image, &coverage);
            errors += QStringLiteral(" %1x: %2 partial, coverage %3").arg(n).arg(partial).arg(coverage);
            QVERIFY2(partial > 300, qPrintable(errors));
            QVERIFY2(std::abs(coverage - area) < 12.0, qPrintable(errors));
            if (n == 4) {
                // Inside, every sample is the face's: the colour of the
                // single-sample render, shaded at the pixel's centre.
                QCOMPARE(image.pixelColor(96, 110), single.pixelColor(96, 110));
                QVERIFY(image.save(testArtifactPath(QStringLiteral("msaa-triangle-4x.png"))));
            }
        }
        // A face behind another stays hidden at every sample.
        mesh.positions << QVector3D(-90, -90, -50) << QVector3D(90, -90, -50) << QVector3D(0, 90, -50);
        model3d::Triangle behind;
        behind.position[0] = 3; behind.position[1] = 4; behind.position[2] = 5;
        model3d::Material blue; blue.diffuseColor = Qt::blue; blue.emissiveColor = Qt::blue;
        mesh.materials = {model3d::Material(), blue};
        behind.material = 1;
        mesh.triangles << behind;
        QCOMPARE(draw(8).pixelColor(96, 110), draw(1).pixelColor(96, 110));

        // A 3D sheet turned on screen has soft edges; the 2D layer it was
        // does not (full-size 2D is not antialiased).
        const auto turned = [](bool threeD, int mode) {
            auto scene = std::make_shared<composition::Composition>();
            scene->setSize(160, 90); scene->setDurationSeconds(1);
            scene->projectSettings().antialiasingMode = mode;
            auto& plane = scene->addLayer("Plane"); plane.kind = composition::LayerKind::Plane;
            plane.planeColor = Qt::red; plane.transform.scalePercent = QPointF(50, 30);
            plane.transform.rotationDegrees = 25;
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
        const auto softEdge = [](const QImage& image) {
            int soft = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const int red = image.pixelColor(x, y).red();
                    if (red > 40 && red < 215) ++soft;
                }
            return soft;
        };
        const QImage flat = turned(false, 1), deep = turned(true, 1);
        QVERIFY(!flat.isNull() && !deep.isNull());
        QVERIFY2(softEdge(deep) > 3 * softEdge(flat) && softEdge(deep) > 60,
                 qPrintable(QStringLiteral("3D %1, 2D %2").arg(softEdge(deep)).arg(softEdge(flat))));
        QVERIFY(deep.save(testArtifactPath(QStringLiteral("msaa-3d-sheet.png"))));

        // The cache key follows the mode for a 3D shot and leaves a 2D one's
        // alone.
        composition::Composition shot;
        shot.setSize(160, 90); shot.setDurationSeconds(1);
        auto& layer = shot.addLayer("Plane"); layer.kind = composition::LayerKind::Plane;
        composition::Clip clip; clip.durationSeconds = 1; layer.clips << clip;
        const auto key = [&] {
            return render::renderStateKey(shot, nullptr, 0.0, QSize(160, 90), true, false, QString());
        };
        const QString flatKey = key();
        shot.projectSettings().antialiasingMode = 3;
        QCOMPARE(key(), flatKey);
        shot.layerRef(0).dimension = composition::LayerDimension::ThreeD;
        const QString sixteen = key();
        shot.projectSettings().antialiasingMode = 1;
        QVERIFY(key() != sixteen);
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

        // The default camera - the reference's "New Camera", a 35 mm lens -
        // stands where the z = 0 plane fills the frame.
        const double cameraDistance = 160.0 / (2.0 * std::tan(54.4322 / 2.0 * M_PI / 180.0));
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
    void compositionSampleRateReachesMovieAndProcessedPcm_data() {
        QTest::addColumn<int>("sampleRate");
        for (int rate : {32000, 44100, 48000, 96000})
            QTest::newRow(qPrintable(QString::number(rate))) << rate;
    }
    void compositionSampleRateReachesMovieAndProcessedPcm() {
        QFETCH(int, sampleRate);
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
        if (ffmpeg.isEmpty() || ffprobe.isEmpty()) QSKIP("FFmpeg is required");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString source = dir.filePath("source.wav");
        QFile wave(source); QVERIFY(wave.open(QIODevice::WriteOnly));
        QDataStream stream(&wave); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4); stream << quint32(36 + 48000 * 4);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
               << quint32(192000) << quint16(4) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(48000 * 4);
        for (int frame = 0; frame < 48000; ++frame) stream << qint16(1000) << qint16(1000);
        wave.close();
        auto manager = std::make_shared<media::MediaManager>();
        QVERIFY(manager->importFile(source).isSuccess());
        auto scene = std::make_shared<composition::Composition>();
        scene->setSize(16, 16); scene->setFrameRate(25, 1); scene->setDurationSeconds(1);
        scene->setAudioSampleRate(sampleRate);
        auto& layer = scene->addLayer("Audio");
        composition::Clip clip; clip.durationSeconds = 1;
        clip.mediaId = manager->assetByFilePath(source).id(); layer.clips.append(clip);
        layer.transform.audioLevel = -6.020599913;
        const QString projectFile = dir.filePath("project.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(projectFile, *scene, *manager).isSuccess());
        scene = std::make_shared<composition::Composition>();
        manager = std::make_shared<media::MediaManager>();
        QVERIFY(project::VegfxSerializer::loadFromFile(projectFile, scene.get(), manager.get()).isSuccess());
        QCOMPARE(scene->audioSampleRate(), sampleRate);
        for (bool processed : {false, true}) {
            if (processed) {
                auto& curve = scene->layerRef(0).transform.audioLevelCurve;
                curve.set(0, -6.020599913); curve.set(25, -6.020599913);
            }
            render::ExportAudioRequest request;
            request.composition = scene.get(); request.media = manager.get();
            request.ffmpeg = ffmpeg; request.ffprobe = ffprobe;
            request.temporaryDirectory = dir.path(); request.frameRate = 25;
            request.exportStart = 0.2; request.exportEnd = 0.6;
            QVector<render::ExportAudioInput> inputs; QString error;
            QVERIFY2(render::buildExportAudioInputs(request, inputs, &error), qPrintable(error));
            QCOMPARE(inputs.size(), 1); QCOMPARE(inputs[0].rawPcm, processed);
            if (processed) {
                QCOMPARE(inputs[0].sampleRate, sampleRate);
                QFile pcm(inputs[0].path); QVERIFY(pcm.open(QIODevice::ReadOnly));
                QCOMPARE(pcm.size(), qRound64(0.4 * sampleRate) * 4);
                QVERIFY(pcm.seek(qRound64(0.1 * sampleRate) * 4));
                const QByteArray bytes = pcm.read(2); QCOMPARE(bytes.size(), 2);
                QVERIFY(qAbs(int(qFromLittleEndian<qint16>(bytes.constData())) - 500) <= 3);
            }
            for (const auto* extension : {"mov", "mp4"}) {
                render::ExportJob::Request movie;
                movie.composition = scene; movie.media = manager;
                movie.firstFrame = 5; movie.lastFrame = 14; movie.hardwareEncoding = false;
                movie.outputPath = dir.filePath(QString("%1.%2").arg(processed ? "processed" : "direct", extension));
                render::ExportJob job(movie); QSignalSpy finished(&job, &render::ExportJob::finished);
                job.start(); QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
                QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
                QProcess probe;
                probe.start(ffprobe, {"-v", "error", "-select_streams", "a:0", "-show_entries",
                                      "stream=sample_rate,channels,duration", "-of", "default=noprint_wrappers=1", movie.outputPath});
                QVERIFY(probe.waitForFinished(10000)); QCOMPARE(probe.exitCode(), 0);
                const QString metadata = QString::fromUtf8(probe.readAllStandardOutput());
                QVERIFY2(metadata.contains(QString("sample_rate=%1").arg(sampleRate)), qPrintable(metadata));
                QVERIFY2(metadata.contains("channels=2"), qPrintable(metadata));
                QVERIFY2(metadata.contains("duration=0.400000"), qPrintable(metadata));
                if (QString::fromLatin1(extension) != "mov") continue;
                QProcess decode;
                decode.start(ffmpeg, {"-v", "error", "-i", movie.outputPath, "-map", "0:a:0", "-f", "s16le", "pipe:1"});
                QVERIFY(decode.waitForFinished(10000)); QCOMPARE(decode.exitCode(), 0);
                const auto bytes = decode.readAllStandardOutput();
                QCOMPARE(bytes.size(), qRound64(0.4 * sampleRate) * 4);
                QVERIFY(qAbs(int(qFromLittleEndian<qint16>(bytes.constData() + qRound64(0.1 * sampleRate) * 4)) - 500) <= 3);
            }
        }
    }

    void nestedAudioOffsetsSpeedGainAndReuse_data() {
        QTest::addColumn<int>("sampleRate"); QTest::addColumn<double>("speed");
        QTest::newRow("32k-slow") << 32000 << 0.25;
        QTest::newRow("44k-fast") << 44100 << 2.0;
        QTest::newRow("96k-slow") << 96000 << 0.25;
    }
    void nestedAudioOffsetsSpeedGainAndReuse() {
        QFETCH(int, sampleRate); QFETCH(double, speed);
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
        if (ffmpeg.isEmpty() || ffprobe.isEmpty()) QSKIP("FFmpeg is required");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString source = dir.filePath("steps.wav");
        QFile wave(source); QVERIFY(wave.open(QIODevice::WriteOnly));
        QDataStream stream(&wave); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4); stream << quint32(36 + 48000 * 3 * 4);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
               << quint32(192000) << quint16(4) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(48000 * 3 * 4);
        for (int frame = 0; frame < 48000 * 3; ++frame) {
            const qint16 value = frame < 24000 ? 1000 : frame < 48000 ? 3000 : 2000;
            stream << value << value;
        }
        wave.close();
        auto manager = std::make_shared<media::MediaManager>(); QVERIFY(manager->importFile(source).isSuccess());
        auto child = std::make_shared<composition::Composition>();
        child->setName("Child"); child->setFrameRate(20, 1); child->setDurationSeconds(3);
        child->setAudioSampleRate(48000);
        composition::Clip leaf; leaf.mediaId = manager->assetByFilePath(source).id();
        leaf.startSeconds = 0.1; leaf.sourceStartSeconds = 0.2; leaf.durationSeconds = 2.5;
        leaf.audioLevel = -6.020599913; child->addLayer("Child audio").clips.append(leaf);
        child->addLayer("Hidden child").clips.append(leaf); child->layerRef(1).visible = false;
        child->addLayer("Muted child").clips.append(leaf); child->layerRef(2).muted = true;
        auto middle = std::make_shared<composition::Composition>();
        middle->setName("Middle"); middle->setFrameRate(30, 1); middle->setDurationSeconds(3);
        composition::Clip mid; mid.nestedComposition = child; mid.nestedCompositionId = child->id();
        mid.startSeconds = 0.2; mid.sourceStartSeconds = 0.1; mid.speed = 1.25; mid.durationSeconds = 2;
        middle->addLayer("Middle audio").clips.append(mid); middle->layerRef(0).transform.audioLevel = -6.020599913;
        auto root = std::make_shared<composition::Composition>();
        root->setName("Root"); root->setSize(16, 16); root->setFrameRate(25, 1);
        root->setDurationSeconds(3); root->setAudioSampleRate(sampleRate);
        root->addCompositeShot(child); root->addCompositeShot(middle);
        composition::Clip first; first.nestedComposition = middle; first.nestedCompositionId = middle->id();
        first.startSeconds = 0.3; first.sourceStartSeconds = 0.2; first.durationSeconds = 1.5; first.speed = speed;
        auto second = first; second.startSeconds = 2.2; second.sourceStartSeconds = 0.5;
        second.durationSeconds = 0.5; second.speed = 1; second.audioLevel = -6.020599913;
        root->addLayer("Root audio").clips = {first, second}; root->layerRef(0).transform.audioLevel = -6.020599913;
        root->addLayer("Muted parent").clips = {first}; root->layerRef(1).muted = true;
        const QString projectFile = dir.filePath("nested.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(projectFile, *root, *manager).isSuccess());
        root = std::make_shared<composition::Composition>(); manager = std::make_shared<media::MediaManager>();
        QVERIFY(project::VegfxSerializer::loadFromFile(projectFile, root.get(), manager.get()).isSuccess());
        QVERIFY(root->layers()[0].clips[0].nestedComposition);
        QCOMPARE(root->layers()[0].clips[0].nestedComposition, root->layers()[0].clips[1].nestedComposition);
        render::ExportAudioRequest request;
        request.composition = root.get(); request.media = manager.get(); request.ffmpeg = ffmpeg;
        request.ffprobe = ffprobe; request.temporaryDirectory = dir.path(); request.frameRate = 25;
        request.exportStart = 0; request.exportEnd = 3;
        const auto valueAt = [](const render::ExportAudioInput& input, double seconds) {
            QFile file(input.path); if (!file.open(QIODevice::ReadOnly)) return INT_MIN;
            if (!file.seek(qRound64((seconds - input.delay) * input.sampleRate) * 4)) return INT_MIN;
            const QByteArray bytes = file.read(2);
            return bytes.size() == 2 ? int(qFromLittleEndian<qint16>(bytes.constData())) : INT_MIN;
        };
        QVector<render::ExportAudioInput> inputs; QString error;
        QVERIFY2(render::buildExportAudioInputs(request, inputs, &error), qPrintable(error));
        QCOMPARE(inputs.size(), 2);
        const auto early = inputs[0].delay < inputs[1].delay ? inputs[0] : inputs[1];
        const auto repeated = inputs[0].delay > inputs[1].delay ? inputs[0] : inputs[1];
        QCOMPARE(early.sampleRate, sampleRate); QCOMPARE(early.delay, 0.3); QCOMPARE(early.sourceDuration, 1.5);
        QVERIFY(qAbs(valueAt(early, 0.3 + (0.35 - 0.2) / (speed * 1.25)) - 125) <= 8);
        QVERIFY2(qAbs(valueAt(early, 0.3 + (0.6 - 0.2) / (speed * 1.25)) - 375) <= 12,
                 qPrintable(QString::number(valueAt(early, 0.3 + 0.4 / (speed * 1.25)))));
        QVERIFY(qAbs(valueAt(repeated, 2.35) - 188) <= 8);
        if (speed == 2) QCOMPARE(valueAt(early, 1.6), 0); // Past the child's valid content.
        // Parent animation is sampled in root frames, not child/middle frames.
        auto& curve = root->layerRef(0).transform.audioLevelCurve;
        curve.setCanInterpolate(false);
        curve.set(0, -6.020599913, composition::TemporalType::Hold);
        curve.set(25, -12.041199826, composition::TemporalType::Hold);
        QVector<render::ExportAudioInput> animated;
        QVERIFY2(render::buildExportAudioInputs(request, animated, &error), qPrintable(error));
        const auto animatedRepeated = animated[0].delay > animated[1].delay ? animated[0] : animated[1];
        QVERIFY(qAbs(valueAt(animatedRepeated, 2.35) - 94) <= 8);
        // New preparation must not overwrite files already returned to an earlier job.
        QVERIFY(qAbs(valueAt(repeated, 2.35) - 188) <= 8);
        render::ExportJob::Request movie; movie.composition = root; movie.media = manager;
        movie.firstFrame = 10; movie.lastFrame = 69; movie.hardwareEncoding = false;
        movie.outputPath = dir.filePath("nested.mov");
        render::ExportJob job(movie); QSignalSpy done(&job, &render::ExportJob::finished);
        job.start(); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
        QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
        QProcess decode; decode.start(ffmpeg, {"-v", "error", "-i", movie.outputPath, "-map", "0:a:0", "-f", "s16le", "pipe:1"});
        QVERIFY(decode.waitForFinished(10000)); QCOMPARE(decode.exitCode(), 0);
        const QByteArray pcm = decode.readAllStandardOutput(); QCOMPARE(pcm.size(), qRound64(2.4 * sampleRate) * 4);
        const auto movieAt = [&](double rootTime) { return int(qFromLittleEndian<qint16>(pcm.constData() + qRound64((rootTime - 0.4) * sampleRate) * 4)); };
        QCOMPARE(movieAt(2.0), 0); QVERIFY(qAbs(movieAt(2.35) - 94) <= 8);
        // Local child fps governs its keys, including under repeated nesting.
        auto loadedChild = root->compositeShot(child->id()); QVERIFY(loadedChild);
        auto& childCurve = loadedChild->layerRef(0).transform.audioLevelCurve;
        childCurve.setCanInterpolate(false); childCurve.set(0, 0, composition::TemporalType::Hold);
        childCurve.set(15, -6.020599913, composition::TemporalType::Hold); // 0.75 s at 20 fps.
        inputs.clear(); QVERIFY2(render::buildExportAudioInputs(request, inputs, &error), qPrintable(error));
        if (speed == 2) {
            const auto changed = inputs[0].delay < inputs[1].delay ? inputs[0] : inputs[1];
            QVERIFY(qAbs(valueAt(changed, 0.66) - 125) <= 8);
        }
    }

    void nestedAudioNativeDspAndTransitions() {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
        const QString balancePath = qEnvironmentVariable("OPENVEGAS_HFPL_BALANCE");
        if (ffmpeg.isEmpty() || ffprobe.isEmpty() || !QFileInfo::exists(balancePath))
            QSKIP("FFmpeg and OPENVEGAS_HFPL_BALANCE are required");
        const QDir modules = QFileInfo(balancePath).absoluteDir();
        const QString dependency = modules.absoluteFilePath("../..");
        const QString reversePath = modules.filePath("AudioReverse.hfpl");
        const QString fadePath = modules.filePath("../AudioTransitions/Fade.hfpl");
        const core::Identifier balance("test.nested.balance"), reverse("test.nested.reverse"), fade("test.nested.fade");
        const auto clear = qScopeGuard([] { plugin::releaseNativeEffectThreadRenderer(); plugin::clearNativeEffectModules(); });
        for (const auto& module : QVector<QPair<core::Identifier, QString>> {{balance, balancePath}, {reverse, reversePath}}) {
            const auto metadata = plugin::loadNativePluginMetadata(module.second, dependency);
            QVERIFY(metadata.lifecycleCompatible);
            plugin::registerNativeAudioEffectModule(module.first, module.second, dependency, true, metadata.parameters);
        }
        const auto fadeMetadata = plugin::loadNativePluginMetadata(fadePath, dependency);
        QVERIFY(fadeMetadata.lifecycleCompatible);
        plugin::registerNativeAudioTransitionModule(fade, fadePath, dependency, true, fadeMetadata.parameters);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString source = dir.filePath("steps.wav");
        QFile wave(source); QVERIFY(wave.open(QIODevice::WriteOnly));
        QDataStream stream(&wave); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4); stream << quint32(36 + 48000 * 2 * 4);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
               << quint32(192000) << quint16(4) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(48000 * 2 * 4);
        for (int frame = 0; frame < 96000; ++frame) {
            const qint16 value = frame < 48000 ? 1000 : 3000; stream << value << value;
        }
        wave.close();
        auto manager = std::make_shared<media::MediaManager>(); QVERIFY(manager->importFile(source).isSuccess());
        auto child = std::make_shared<composition::Composition>(); child->setDurationSeconds(2); child->setFrameRate(50, 1);
        composition::Clip leaf; leaf.mediaId = manager->assetByFilePath(source).id(); leaf.durationSeconds = 2;
        composition::Effect reverseEffect; reverseEffect.pluginId = reverse; reverseEffect.name = "Reverse";
        leaf.effects.append(reverseEffect); child->addLayer("Reversed audio").clips.append(leaf);
        auto root = std::make_shared<composition::Composition>(); root->setSize(16, 16);
        root->setDurationSeconds(2); root->setFrameRate(25, 1); root->setAudioSampleRate(44100);
        composition::Clip first; first.nestedComposition = child; first.durationSeconds = 1; first.speed = 2;
        composition::Effect balanceEffect; balanceEffect.pluginId = balance; balanceEffect.name = "Balance";
        balanceEffect.parameterValues = {QStringLiteral("100")};
        composition::KeyFrameList keys(100); keys.setCanInterpolate(false);
        keys.set(0, 100, composition::TemporalType::Hold); keys.set(12, -100, composition::TemporalType::Hold);
        balanceEffect.animation.insert(0, keys); first.effects.append(balanceEffect);
        composition::Clip second; second.nestedComposition = child;
        second.startSeconds = 1; second.sourceStartSeconds = 0.5; second.durationSeconds = 1;
        composition::Effect fadeEffect; fadeEffect.pluginId = fade; fadeEffect.name = "Fade";
        fadeEffect.transitionEdge = composition::TransitionEdge::In; fadeEffect.transitionSeconds = 0.4;
        second.effects.append(fadeEffect);
        root->addLayer("Nested DSP").clips = {first, second}; root->layerRef(0).transform.audioLevel = -6.020599913;
        render::ExportAudioRequest request; request.composition = root.get(); request.media = manager.get();
        request.ffmpeg = ffmpeg; request.ffprobe = ffprobe; request.temporaryDirectory = dir.path();
        request.exportEnd = 2; request.frameRate = 25;
        QVector<render::ExportAudioInput> inputs; QString error;
        QVERIFY2(render::buildExportAudioInputs(request, inputs, &error), qPrintable(error)); QCOMPARE(inputs.size(), 2);
        const auto stereoAt = [&](double seconds) {
            QPoint sample;
            for (const auto& input : inputs) {
                if (seconds < input.delay || seconds >= input.delay + input.sourceDuration) continue;
                QFile file(input.path); if (!file.open(QIODevice::ReadOnly)) continue;
                file.seek(qRound64((seconds - input.delay) * input.sampleRate) * 4);
                const QByteArray bytes = file.read(4); if (bytes.size() != 4) continue;
                sample += QPoint(qFromLittleEndian<qint16>(bytes.constData()), qFromLittleEndian<qint16>(bytes.constData() + 2));
            }
            return sample;
        };
        const QPoint early = stereoAt(0.2), late = stereoAt(0.65);
        QVERIFY2(qAbs(early.x() - early.y()) > 500, qPrintable(QString("%1,%2").arg(early.x()).arg(early.y())));
        QVERIFY(qAbs(late.x() - late.y()) > 150);
        QVERIFY((early.x() > early.y()) != (late.x() > late.y()));
        QVERIFY(qAbs((qAbs(early.x()) + qAbs(early.y())) - 3 * (qAbs(late.x()) + qAbs(late.y()))) < 80);
        const auto atCut = stereoAt(1.0); QVERIFY(qAbs(atCut.x()) < 30 && qAbs(atCut.y()) < 30);
        const auto after = stereoAt(1.3); QVERIFY(qAbs(after.x() - 1500) < 30 && qAbs(after.y() - 1500) < 30);
        const auto tail = stereoAt(1.7); QVERIFY(qAbs(tail.x() - 500) < 30 && qAbs(tail.y() - 500) < 30);
        render::ExportJob::Request movie; movie.composition = root; movie.media = manager;
        movie.outputPath = dir.filePath("nested-dsp.mov"); movie.lastFrame = 49; movie.hardwareEncoding = false;
        render::ExportJob job(movie); QSignalSpy done(&job, &render::ExportJob::finished);
        job.start(); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
        QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
        QProcess decode; decode.start(ffmpeg, {"-v", "error", "-i", movie.outputPath, "-map", "0:a:0", "-f", "s16le", "pipe:1"});
        QVERIFY(decode.waitForFinished(10000)); QCOMPARE(decode.exitCode(), 0);
        const QByteArray bytes = decode.readAllStandardOutput(); QCOMPARE(bytes.size(), 44100 * 2 * 4);
        for (double time : {0.2, 0.65, 1.0, 1.3, 1.7}) {
            const qint64 offset = qRound64(time * 44100) * 4;
            const QPoint actual(qFromLittleEndian<qint16>(bytes.constData() + offset), qFromLittleEndian<qint16>(bytes.constData() + offset + 2));
            QCOMPARE(actual, stereoAt(time));
        }
    }

    void audioMixUsesSampleDelaysAndPadding() {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty()) QSKIP("FFmpeg is required");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile file(dir.filePath("input.pcm")); QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
        for (int frame = 0; frame < 882; ++frame) stream << qint16(1000) << qint16(1000);
        file.close();
        render::ExportAudioInput input; input.path = file.fileName(); input.sourceDuration = 0.02;
        input.delay = 0.0005; input.rawPcm = true; input.sampleRate = 44100;
        QStringList arguments {"-v", "error"};
        arguments += render::exportAudioInputArguments({input});
        arguments << "-filter_complex" << render::exportAudioMixFilter({input}, 44100, 0)
                  << "-map" << "[aout]" << "-t" << "0.03" << "-ar" << "44100" << "-ac" << "2"
                  << "-f" << "s16le" << "pipe:1";
        QProcess mix; mix.start(ffmpeg, arguments);
        QVERIFY(mix.waitForFinished(10000));
        QVERIFY2(mix.exitCode() == 0, mix.readAllStandardError().constData());
        const QByteArray bytes = mix.readAllStandardOutput(); QCOMPARE(bytes.size(), 1323 * 4);
        const auto sampleAt = [&](int frame) { return int(qFromLittleEndian<qint16>(bytes.constData() + frame * 4)); };
        QCOMPARE(sampleAt(21), 0); QCOMPARE(sampleAt(22), 1000); // 0.5 ms = 22 samples, not 1 ms.
        QCOMPARE(sampleAt(903), 1000); QCOMPARE(sampleAt(904), 0); QCOMPARE(sampleAt(1322), 0);
    }

    void nestedAudioCycleDoesNotPublishPartialInputs() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); media::MediaManager manager;
        auto root = std::make_shared<composition::Composition>(); root->setDurationSeconds(1);
        auto child = std::make_shared<composition::Composition>(); child->setDurationSeconds(1);
        composition::Clip clip; clip.durationSeconds = 1; clip.nestedComposition = child;
        root->addLayer("Child").clips.append(clip);
        clip.nestedComposition = root; child->addLayer("Cycle").clips.append(clip);
        const auto breakCycle = qScopeGuard([&] { child->layerRef(0).clips.clear(); });
        render::ExportAudioRequest request; request.composition = root.get(); request.media = &manager;
        request.temporaryDirectory = dir.path(); request.exportEnd = 1;
        QVector<render::ExportAudioInput> inputs {{"keep-existing-input"}}; QString error;
        QVERIFY(!render::buildExportAudioInputs(request, inputs, &error));
        QVERIFY(error.contains("cycle")); QCOMPARE(inputs.size(), 1); QCOMPARE(inputs[0].path, QString("keep-existing-input"));
        child->layerRef(0).muted = true;
        QVERIFY(render::buildExportAudioInputs(request, inputs, &error)); QVERIFY(error.isEmpty()); QCOMPARE(inputs.size(), 1);
        child->layerRef(0).muted = false; child->layerRef(0).clips.clear();
        QVERIFY(render::buildExportAudioInputs(request, inputs, &error)); QCOMPARE(inputs.size(), 1);
    }

    void audioTransitionsExportAsProcessedPcm_data() {
        compositionSampleRateReachesMovieAndProcessedPcm_data();
    }
    void audioTransitionsExportAsProcessedPcm() {
        QFETCH(int, sampleRate);
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
        scene.setSize(16, 16); scene.setDurationSeconds(2); scene.setAudioSampleRate(sampleRate);
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
            const qint64 frame = qRound64((seconds - input.delay) * input.sampleRate);
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
    // A layer moved in time takes its keys along (the reference keeps key
    // times relative to the layer's start): dragging the clip body moves the
    // layer's keys by as many frames, as one undoable edit.
    void movedLayerTakesItsKeys() {
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); const int y = ui::kTimelineRulerHeight + 11;
        panel->setSnapEnabled(false);
        const auto original = comp->layers()[0];
        QCOMPARE(original.clips.size(), 1);
        const composition::Clip& clip = original.clips[0];
        const int fps = comp->fpsNumerator() / qMax(1, comp->fpsDenominator());
        const int keyFrame = int(std::lround((clip.startSeconds + clip.durationSeconds / 2.0) * fps));
        comp->layerRef(0).transform.positionXCurve.set(keyFrame, 25.0);
        composition::Effect fx; fx.pluginId = core::Identifier("test.keys.follow"); fx.parameterValues = {"1"};
        fx.animation[0].set(keyFrame + 3, 0.5);
        comp->layerRef(0).clips[0].effects.append(fx);
        panel->setComposition(comp);
        const int historyBefore = history.count();
        const auto xFor = [&](double seconds) {
            return int(std::lround(seconds * canvas->pixelsPerSecond())) - canvas->hScrollOffset();
        };
        const double middle = clip.startSeconds + clip.durationSeconds / 2.0;
        const QPoint start(xFor(middle), y);
        const QPoint finish = start + QPoint(int(std::lround(canvas->pixelsPerSecond())), 0);   // a second later
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, finish, canvas->mapToGlobal(finish), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, finish);
        const composition::Layer& moved = comp->layers()[0];
        const int frames = int(std::lround((moved.clips[0].startSeconds - clip.startSeconds) * fps));
        QVERIFY2(frames > 0, qPrintable(QString::number(frames)));
        QCOMPARE(moved.transform.positionXCurve.locations(), (QVector<int> {keyFrame + frames}));
        QCOMPARE(moved.clips[0].effects.last().animation.value(0).locations(), (QVector<int> {keyFrame + 3 + frames}));
        QCOMPARE(history.count(), historyBefore + 1);
        history.undo();
        QCOMPARE(comp->layers()[0].transform.positionXCurve.locations(), (QVector<int> {keyFrame}));
        comp->layerRef(0) = original;
        panel->setComposition(comp);
    }
    // Trimming a layer's head keeps its keys where they were on the shot's
    // timeline, as the reference's trim does (it moves the relative keys back
    // by as much as the start moved, and the media in-point with the start):
    // the keys stay on their frames, also one now before the layer's start,
    // and still do after a save and a load.
    void trimmedLayerKeepsItsKeys() {
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); const int y = ui::kTimelineRulerHeight + 11;
        panel->setSnapEnabled(false);
        const auto original = comp->layers()[0];
        QCOMPARE(original.clips.size(), 1);
        const composition::Clip& clip = original.clips[0];
        const int fps = comp->fpsNumerator() / qMax(1, comp->fpsDenominator());
        const int startFrame = int(std::lround(clip.startSeconds * fps));
        const int keyFrame = int(std::lround((clip.startSeconds + clip.durationSeconds * 0.75) * fps));
        comp->layerRef(0).transform.positionXCurve.set(startFrame + 1, 0.0);
        comp->layerRef(0).transform.positionXCurve.set(keyFrame, 25.0);
        composition::Effect fx; fx.pluginId = core::Identifier("test.keys.trim"); fx.parameterValues = {"1"};
        fx.animation[0].set(keyFrame + 2, 0.5);
        comp->layerRef(0).clips[0].effects.append(fx);
        panel->setComposition(comp);
        const int historyBefore = history.count();
        const auto xFor = [&](double seconds) {
            return int(std::lround(seconds * canvas->pixelsPerSecond())) - canvas->hScrollOffset();
        };
        // The head, grabbed a pixel inside, dragged a quarter of the clip on.
        const QPoint start(xFor(clip.startSeconds) + 1, y);
        const QPoint finish = start + QPoint(int(std::lround(canvas->pixelsPerSecond() * clip.durationSeconds / 4.0)), 0);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, finish, canvas->mapToGlobal(finish), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, finish);
        const composition::Layer& trimmed = comp->layers()[0];
        const double moved = trimmed.clips[0].startSeconds - clip.startSeconds;
        QVERIFY2(moved > 0.0, qPrintable(QString::number(moved)));
        QVERIFY(std::abs(trimmed.clips[0].endSeconds() - clip.endSeconds()) < 1e-9);
        QVERIFY(std::abs(trimmed.clips[0].sourceStartSeconds - (clip.sourceStartSeconds + moved)) < 1e-9);
        QCOMPARE(trimmed.transform.positionXCurve.locations(), (QVector<int> {startFrame + 1, keyFrame}));
        QCOMPARE(trimmed.clips[0].effects.last().animation.value(0).locations(), (QVector<int> {keyFrame + 2}));
        QCOMPARE(history.count(), historyBefore + 1);
        // Out relative to the new start (the first key before it) and back.
        {
            QTemporaryDir temp; QVERIFY(temp.isValid());
            media::MediaManager media;
            composition::Composition shot;
            shot.setSize(320, 180); shot.setFrameRate(fps, 1); shot.setDurationSeconds(comp->durationSeconds());
            composition::Layer& copy = shot.addLayer("Trimmed");
            const core::Identifier id = copy.id;
            copy = trimmed;
            copy.id = id;
            copy.kind = composition::LayerKind::Plane;
            copy.clips[0].mediaId = core::Identifier();
            copy.clips[0].effects.clear();
            const QString path = temp.filePath("trimmed.vegfx");
            QVERIFY(project::VegfxSerializer::saveToFile(path, shot, media).isSuccess());
            composition::Composition loaded; media::MediaManager media2;
            QVERIFY(project::VegfxSerializer::loadFromFile(path, &loaded, &media2).isSuccess());
            QCOMPARE(loaded.layers().size(), 1);
            QCOMPARE(loaded.layers()[0].transform.positionXCurve.locations(), (QVector<int> {startFrame + 1, keyFrame}));
        }
        history.undo();
        QVERIFY(std::abs(comp->layers()[0].clips[0].startSeconds - clip.startSeconds) < 1e-9);
        QCOMPARE(comp->layers()[0].transform.positionXCurve.locations(), (QVector<int> {startFrame + 1, keyFrame}));
        comp->layerRef(0) = original;
        panel->setComposition(comp);
    }
    // A layer without clips (camera, light, point, model) lies on the shot's
    // timeline by its span - the reference's StartFrame and EndFrame. A span
    // of its own is written and read back; one to the shot's end goes out as
    // the shot's FrameCount (not an empty layer) and stays open; an older
    // save of this port, which wrote EndFrame = StartFrame, is the whole shot.
    void layersWithoutClipsKeepTheirSpan() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        media::MediaManager media;
        composition::Composition shot;
        shot.setSize(320, 180); shot.setFrameRate(30, 1); shot.setDurationSeconds(3);
        {
            auto& camera = shot.addLayer("Late camera");
            camera.kind = composition::LayerKind::Camera;
            camera.dimension = composition::LayerDimension::ThreeD;
            camera.spanStartSeconds = 1.0;
            camera.spanEndSeconds = 2.0;
            camera.transform.positionXCurve.set(45, 10.0);   // 0.5 s into it
            auto& light = shot.addLayer("Light");
            light.kind = composition::LayerKind::Light;
            light.dimension = composition::LayerDimension::ThreeD;
        }
        const QString path = temp.filePath("spans.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, shot, media).isSuccess());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(file.readAll());
        file.close();
        const QRegularExpression cameraSpan(QStringLiteral("<StartFrame>30</StartFrame>\\s*<EndFrame>60</EndFrame>"));
        const QRegularExpression openSpan(QStringLiteral("<StartFrame>0</StartFrame>\\s*<EndFrame>90</EndFrame>"));
        QVERIFY2(xml.contains(cameraSpan), "camera span");
        QVERIFY2(xml.contains(openSpan), "open span");
        QVERIFY(!xml.contains(QStringLiteral("<EndFrame>0</EndFrame>")));
        const auto load = [&](const QString& text, composition::Composition* loaded) {
            QFile out(path); if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            out.write(text.toUtf8()); out.close();
            media::MediaManager media2;
            return project::VegfxSerializer::loadFromFile(path, loaded, &media2).isSuccess()
                   && loaded->layers().size() == 2;
        };
        composition::Composition loaded;
        QVERIFY(load(xml, &loaded));
        QCOMPARE(loaded.layers()[0].spanStartSeconds, 1.0);
        QCOMPARE(loaded.layers()[0].spanEndSeconds, 2.0);
        QCOMPARE(loaded.layers()[0].transform.positionXCurve.locations(), (QVector<int> {45}));
        QCOMPARE(loaded.layers()[1].spanStartSeconds, 0.0);
        QCOMPARE(loaded.layers()[1].spanEndSeconds, -1.0);
        // The light's file as an older save wrote it: an empty span.
        QString older = xml;
        older.replace(openSpan, QStringLiteral("<StartFrame>0</StartFrame><EndFrame>0</EndFrame>"));
        composition::Composition olderLoaded;
        QVERIFY(load(older, &olderLoaded));
        QCOMPARE(olderLoaded.layers()[1].spanEndSeconds, -1.0);
        QVERIFY(composition::layerSpanCovers(olderLoaded.layers()[1], 89, 30.0));
    }
    // The active camera is the topmost visible one whose span holds the frame
    // (CompositionAsset::ActiveCameraLayer: StartFrame <= frame < EndFrame),
    // and a light lights only within its own.
    void camerasAndLightsTakePartWithinTheirSpan() {
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(30, 1); shot->setDurationSeconds(4);
        {
            auto& first = shot->addLayer("First");
            first.kind = composition::LayerKind::Camera;
            first.dimension = composition::LayerDimension::ThreeD;
            first.transform.position = QPointF(-100.0, 0.0);
            first.spanStartSeconds = 0.0; first.spanEndSeconds = 1.0;
            auto& second = shot->addLayer("Second");
            second.kind = composition::LayerKind::Camera;
            second.dimension = composition::LayerDimension::ThreeD;
            second.transform.position = QPointF(100.0, 0.0);
            second.spanStartSeconds = 1.0;
            auto& light = shot->addLayer("Light");
            light.kind = composition::LayerKind::Light;
            light.dimension = composition::LayerDimension::ThreeD;
            light.spanStartSeconds = 2.0; light.spanEndSeconds = 3.0;
        }
        const QSize canvas(320, 180);
        QVERIFY(render::sceneCameraOf(shot.get(), canvas, 29).position.x() < 0.0f);
        QVERIFY(render::sceneCameraOf(shot.get(), canvas, 30).position.x() > 0.0f);
        QVERIFY(render::sceneCameraOf(shot.get(), canvas, 119).position.x() > 0.0f);
        QVERIFY(render::sceneLightsOf(shot.get(), 59).isEmpty());
        QCOMPARE(render::sceneLightsOf(shot.get(), 60).size(), 1);
        QVERIFY(render::sceneLightsOf(shot.get(), 90).isEmpty());
    }
    // A camera's bar on the timeline: its body moves the span and the keys
    // with it; its end trims the span and leaves the keys; each drag one
    // undoable edit.
    void cameraBarMovesAndTrimsItsSpan() {
        composition::Layer camera;
        camera.name = "Camera"; camera.kind = composition::LayerKind::Camera;
        camera.dimension = composition::LayerDimension::ThreeD;
        camera.spanStartSeconds = 5.0; camera.spanEndSeconds = 15.0;
        const int fps = comp->fpsNumerator() / qMax(1, comp->fpsDenominator());
        camera.transform.positionXCurve.set(10 * fps, 20.0);
        QVERIFY(comp->insertLayer(0, camera));
        panel->setComposition(comp);
        panel->setSnapEnabled(false);
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); const int y = ui::kTimelineRulerHeight + 11;
        const auto xFor = [&](double seconds) {
            return int(std::lround(seconds * canvas->pixelsPerSecond())) - canvas->hScrollOffset();
        };
        const auto drag = [&](const QPoint& from, const QPoint& to) {
            QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
            QMouseEvent move(QEvent::MouseMove, to, canvas->mapToGlobal(to), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &move);
            QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
        };
        const int second = int(std::lround(canvas->pixelsPerSecond()));
        QVERIFY(second >= 4);
        const int historyBefore = history.count();
        // The body, two seconds on.
        drag(QPoint(xFor(10.0), y), QPoint(xFor(10.0) + 2 * second, y));
        const composition::Layer& moved = comp->layers()[0];
        const double shift = moved.spanStartSeconds - 5.0;
        QVERIFY2(shift > 1.0, qPrintable(QString::number(shift)));
        QVERIFY(std::abs(moved.spanEndSeconds - (15.0 + shift)) < 1e-9);
        QCOMPARE(moved.transform.positionXCurve.locations(),
                 (QVector<int> {10 * fps + int(std::lround(shift * fps))}));
        QCOMPARE(history.count(), historyBefore + 1);
        // The tail, a second back: the keys stay.
        const double end = moved.spanEndSeconds;
        const QVector<int> keys = moved.transform.positionXCurve.locations();
        drag(QPoint(xFor(end) - 3, y), QPoint(xFor(end) - 3 - second, y));
        QVERIFY2(comp->layers()[0].spanEndSeconds < end - 0.5, qPrintable(QString::number(comp->layers()[0].spanEndSeconds)));
        QVERIFY(std::abs(comp->layers()[0].spanStartSeconds - (5.0 + shift)) < 1e-9);
        QCOMPARE(comp->layers()[0].transform.positionXCurve.locations(), keys);
        QCOMPARE(history.count(), historyBefore + 2);
        history.undo(); history.undo();
        QCOMPARE(comp->layers()[0].spanStartSeconds, 5.0);
        QCOMPARE(comp->layers()[0].spanEndSeconds, 15.0);
        QCOMPARE(comp->layers()[0].transform.positionXCurve.locations(), (QVector<int> {10 * fps}));
    }
    // The shot timeline's other tools on a camera's bar, as the reference
    // has them: Slice cuts the layer in two (Slice Layer: a copy from the cut
    // on, the keys of both where they were), Rate Stretch on its end trims
    // it (no speed to change on a camera, FUN_1405cae00).
    void sliceAndStretchCutAndTrimACameraSpan() {
        composition::Layer camera;
        camera.name = "Camera"; camera.kind = composition::LayerKind::Camera;
        camera.dimension = composition::LayerDimension::ThreeD;
        camera.spanStartSeconds = 5.0; camera.spanEndSeconds = 15.0;
        const int fps = comp->fpsNumerator() / qMax(1, comp->fpsDenominator());
        camera.transform.positionXCurve.set(12 * fps, 20.0);
        QVERIFY(comp->insertLayer(0, camera));
        panel->setComposition(comp);
        panel->setSnapEnabled(false);
        auto* canvas = panel->findChild<ui::TimelineCanvas*>(); const int y = ui::kTimelineRulerHeight + 11;
        const auto xFor = [&](double seconds) {
            return int(std::lround(seconds * canvas->pixelsPerSecond())) - canvas->hScrollOffset();
        };
        const int historyBefore = history.count();
        const int layersBefore = comp->layers().size();
        panel->setTool(ui::EditorTool::Slice);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(xFor(8.0), y));
        QCOMPARE(comp->layers().size(), layersBefore + 1);
        const composition::Layer& head = comp->layers()[0];
        const composition::Layer& tail = comp->layers()[1];
        QCOMPARE(head.kind, composition::LayerKind::Camera);
        QCOMPARE(tail.kind, composition::LayerKind::Camera);
        QVERIFY(head.id != tail.id);
        QCOMPARE(head.spanStartSeconds, 5.0);
        QVERIFY(std::abs(head.spanEndSeconds - 8.0) < 0.1);
        QCOMPARE(tail.spanStartSeconds, head.spanEndSeconds);
        QCOMPARE(tail.spanEndSeconds, 15.0);
        QCOMPARE(head.transform.positionXCurve.locations(), (QVector<int> {12 * fps}));
        QCOMPARE(tail.transform.positionXCurve.locations(), (QVector<int> {12 * fps}));
        QCOMPARE(history.count(), historyBefore + 1);
        // Rate Stretch on the tail's end: a trim, the key where it was.
        panel->setTool(ui::EditorTool::Stretch);
        history.undo();
        QCOMPARE(comp->layers().size(), layersBefore);
        const double end = comp->layers()[0].spanEndSeconds;
        const int second = int(std::lround(canvas->pixelsPerSecond()));
        const QPoint from(xFor(end) - 3, y), to(xFor(end) - 3 - 2 * second, y);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, to, canvas->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
        QVERIFY2(comp->layers()[0].spanEndSeconds < end - 1.5,
                 qPrintable(QString::number(comp->layers()[0].spanEndSeconds)));
        QCOMPARE(comp->layers()[0].spanStartSeconds, 5.0);
        QCOMPARE(comp->layers()[0].transform.positionXCurve.locations(), (QVector<int> {12 * fps}));
        panel->setTool(ui::EditorTool::Select);
    }
    // A layer of this port with two clips goes out as two layers, as the
    // reference's Slice Layer would leave it - the second marked
    // OpenVegasSliceOf, its range its clip's - and comes back as one layer
    // with both clips; the reference, without this port's attributes, reads
    // two layers.
    void multiClipLayersGoOutAsSlices() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        media::MediaManager media;
        composition::Composition shot;
        shot.setSize(320, 180); shot.setFrameRate(30, 1); shot.setDurationSeconds(4);
        {
            auto& plane = shot.addLayer("Plane");
            plane.kind = composition::LayerKind::Plane;
            composition::Clip first; first.startSeconds = 0.0; first.durationSeconds = 1.0;
            composition::Clip second; second.startSeconds = 2.0; second.durationSeconds = 1.0;
            plane.clips << first << second;
            plane.transform.positionXCurve.set(75, 30.0);
        }
        const QString path = temp.filePath("slices.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, shot, media).isSuccess());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(file.readAll());
        file.close();
        QCOMPARE(xml.count(QStringLiteral("<AssetLayer ")), 2);
        QCOMPARE(xml.count(QStringLiteral("OpenVegasSliceOf=\"%1\"").arg(shot.layers()[0].id.value())), 1);
        QVERIFY(xml.contains(QRegularExpression(QStringLiteral("<StartFrame>60</StartFrame>\\s*<EndFrame>90</EndFrame>"))));
        const auto load = [&](const QString& text) {
            QFile out(path); if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return composition::Composition();
            out.write(text.toUtf8()); out.close();
            composition::Composition loaded; media::MediaManager media2;
            if (!project::VegfxSerializer::loadFromFile(path, &loaded, &media2).isSuccess()) return composition::Composition();
            return loaded;
        };
        const composition::Composition back = load(xml);
        QCOMPARE(back.layers().size(), 1);
        QCOMPARE(back.layers()[0].clips.size(), 2);
        QCOMPARE(back.layers()[0].transform.positionXCurve.locations(), (QVector<int> {75}));
        // As the reference sees it: two layers of one range each, the key
        // on both at its frame of the shot.
        QString reference = xml;
        reference.remove(QRegularExpression(QStringLiteral(" OpenVegasSliceOf=\"[^\"]*\"")));
        reference.remove(QRegularExpression(QStringLiteral("<OpenVegasClips[^>]*>.*?</OpenVegasClips>"),
                                            QRegularExpression::DotMatchesEverythingOption));
        const composition::Composition seen = load(reference);
        QCOMPARE(seen.layers().size(), 2);
        QVector<double> starts;
        for (const composition::Layer& layer : seen.layers()) {
            QCOMPARE(layer.clips.size(), 1);
            starts << layer.clips[0].startSeconds;
            QCOMPARE(layer.transform.positionXCurve.locations(), (QVector<int> {75}));
        }
        std::sort(starts.begin(), starts.end());
        QCOMPARE(starts, (QVector<double> {0.0, 2.0}));
    }
    // Key times in the file are relative to the layer's start, as the
    // reference writes them (StartFrame), and come back on the shot's
    // timeline; a save of this port from before kept them on the shot's
    // timeline and is read as it was.
    void keyTimesAreRelativeToTheLayer() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        media::MediaManager media;
        composition::Composition shot;
        shot.setSize(320, 180); shot.setFrameRate(30, 1); shot.setDurationSeconds(10);
        {
            auto& plane = shot.addLayer("Late");
            plane.kind = composition::LayerKind::Plane;
            composition::Clip clip; clip.startSeconds = 2.0; clip.durationSeconds = 3.0;
            plane.clips << clip;
            plane.transform.positionXCurve.set(60, 0.0);
            plane.transform.positionXCurve.set(90, 100.0);
            plane.transform.opacityCurve.set(75, 50.0);
        }
        const QString path = temp.filePath("late.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(path, shot, media).isSuccess());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QString xml = QString::fromUtf8(file.readAll());
        file.close();
        QVERIFY(xml.contains(QStringLiteral("OpenVegasKeyTimes=\"layer\"")));
        // The keys at 2 s and 3 s of the shot are 0 ms and 1000 ms of the layer.
        QVERIFY2(xml.contains(QStringLiteral("Ti=\"0.0000\"")) && xml.contains(QStringLiteral("Ti=\"1000.0000\""))
                     && xml.contains(QStringLiteral("Ti=\"500.0000\"")),
                 "relative key times");
        QVERIFY(!xml.contains(QStringLiteral("Ti=\"2000.0000\"")));
        const auto keysOf = [&](const QString& text) {
            QFile out(path); if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QVector<int>();
            out.write(text.toUtf8()); out.close();
            composition::Composition loaded; media::MediaManager media2;
            if (!project::VegfxSerializer::loadFromFile(path, &loaded, &media2).isSuccess()
                || loaded.layers().isEmpty())
                return QVector<int>();
            return loaded.layers()[0].transform.positionXCurve.locations()
                   + loaded.layers()[0].transform.opacityCurve.locations();
        };
        QCOMPARE(keysOf(xml), (QVector<int> {60, 90, 75}));
        // A save from before the change: the same times read as the shot's.
        QString older = xml;
        older.remove(QStringLiteral(" OpenVegasKeyTimes=\"layer\""));
        QCOMPARE(keysOf(older), (QVector<int> {0, 30, 15}));
        // The reference's own file (no attributes of this port): relative.
        QString reference = xml;
        reference.remove(QStringLiteral(" OpenVegasKeyTimes=\"layer\""));
        reference.remove(QRegularExpression(QStringLiteral(" OpenVegasKind=\"[^\"]*\"")));
        const QVector<int> referenceKeys = keysOf(reference);
        if (!referenceKeys.isEmpty()) QCOMPARE(referenceKeys, (QVector<int> {60, 90, 75}));
        // Shifting moves every curve of a layer with identities kept.
        composition::Layer layer = shot.layers()[0];
        const int id = layer.transform.positionXCurve.at(60)->id;
        composition::shiftLayerKeys(layer, -60);
        QCOMPARE(layer.transform.positionXCurve.locations(), (QVector<int> {0, 30}));
        QCOMPARE(layer.transform.positionXCurve.at(0)->id, id);
        QCOMPARE(layer.transform.opacityCurve.locations(), (QVector<int> {15}));
    }
    // The Layout panel's numeric fields are the reference's
    // DoubleScrubberSpinBox (Widgets.dll): a scrubber until clicked - one
    // step a pixel from the value at the press, Shift snapping to multiples
    // of 10 every five pixels in the drag's direction, held at the range's
    // ends, one edit when the button goes up - and a text field after a
    // click, Escape going back to where it was.
    void layoutFieldsScrubLikeTheReference() {
        using ui::DoubleScrubberSpinBox;
        QCOMPARE(DoubleScrubberSpinBox::snapped(23.0, 10.0, true), 30.0);
        QCOMPARE(DoubleScrubberSpinBox::snapped(23.0, 10.0, false), 20.0);
        QCOMPARE(DoubleScrubberSpinBox::snapped(30.0, 10.0, true), 30.0);
        QCOMPARE(DoubleScrubberSpinBox::snapped(-23.0, 10.0, true), -20.0);
        QCOMPARE(DoubleScrubberSpinBox::snapped(-23.0, 10.0, false), -30.0);
        QCOMPARE(DoubleScrubberSpinBox::formatted(12.0, 1, 1), QString("12.0"));
        QCOMPARE(DoubleScrubberSpinBox::formatted(12.0, 3, 0), QString("12"));
        QCOMPARE(DoubleScrubberSpinBox::formatted(12.5, 3, 0), QString("12.5"));
        QCOMPARE(DoubleScrubberSpinBox::formatted(105.0, 1, 1), QString("105.0"));
        QCOMPARE(DoubleScrubberSpinBox::formatted(-3.125, 3, 0), QString("-3.125"));

        QWidget window; auto* box = new DoubleScrubberSpinBox(&window);
        box->setRange(-99999, 99999); box->setValue(13.0);
        box->setGeometry(10, 10, 120, 24);
        window.resize(200, 60); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(box->isReadOnly());
        QCOMPARE(box->text(), QString("13.0"));
        QLineEdit* edit = box->findChild<QLineEdit*>();
        QVERIFY(edit);
        QSignalSpy started(box, &DoubleScrubberSpinBox::editingStarted);
        QSignalSpy released(box, &DoubleScrubberSpinBox::mouseReleased);
        const QPoint at(20, 10);
        const auto send = [&](QEvent::Type type, const QPoint& local, Qt::MouseButton button, Qt::MouseButtons buttons,
                              Qt::KeyboardModifiers modifiers) {
            QMouseEvent event(type, QPointF(local), QPointF(edit->mapToGlobal(local)), button, buttons, modifiers);
            QApplication::sendEvent(edit, &event);
        };
        // A plain drag: 30 px right is 30 steps.
        send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(started.count(), 1);
        send(QEvent::MouseMove, at + QPoint(30, 0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(box->value(), 43.0);
        send(QEvent::MouseButtonRelease, at + QPoint(30, 0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCOMPARE(released.count(), 1);
        QVERIFY(box->isReadOnly());
        // With Shift: 10 a five pixels, onto multiples of 10 the way it goes.
        box->setValue(13.0);
        send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
        // (Past the drag distance first: 12 px is 13 + 20, up to 40.)
        send(QEvent::MouseMove, at + QPoint(12, 0), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
        QCOMPARE(box->value(), 40.0);
        // The pointer is put back where it went down, so each move adds on:
        // 32 px in all, 13 + 60, up to 80.
        send(QEvent::MouseMove, at + QPoint(20, 0), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
        QCOMPARE(box->value(), 80.0);
        send(QEvent::MouseButtonRelease, at + QPoint(20, 0), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
        // Held at the end of its range.
        box->setRange(0, 100); box->setValue(0.0);
        send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        send(QEvent::MouseMove, at - QPoint(40, 0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(box->value(), 0.0);
        send(QEvent::MouseMove, at + QPoint(10, 0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(box->value(), 10.0);
        send(QEvent::MouseButtonRelease, at + QPoint(10, 0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCOMPARE(released.count(), 3);
        // A click without a drag edits the text; Escape goes back.
        box->setValue(12.5);
        send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        send(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCOMPARE(released.count(), 3);
        QTRY_VERIFY(!box->isReadOnly());
        QCOMPARE(box->text(), QString("12.5"));
        box->setValue(70.0);
        QTest::keyClick(box, Qt::Key_Escape);
        QCOMPARE(box->value(), 12.5);
        QVERIFY(box->isReadOnly());

        // The Layout panel's fields, and a scrub there is one edit.
        ui::LayoutPanel layout;
        auto* x = layout.findChild<DoubleScrubberSpinBox*>("spinBoxX");
        QVERIFY(x && layout.findChild<DoubleScrubberSpinBox*>("spinBoxHeight"));
        QCOMPARE(x->maximum(), 99999.0);
        QCOMPARE(x->focusPolicy(), Qt::StrongFocus);
    }
    // A layer's box follows its Behaviors, as the renderer moves the picture
    // (the reference's TransformWidget measures layers with
    // WorldTransformationAtTime, Behaviors included): MotionTrack's matrices
    // move a plane's box with it, frame by frame; without one the box stays.
    void layerBoxFollowsItsBehaviors() {
        const core::Identifier id("test.behavior.box.motiontrack");
        plugin::registerNativeBehaviorModule(id, QStringLiteral("C:/nowhere/MotionTrack.hfpl"), QString(), false, {});
        auto cleanup = qScopeGuard([] {
            plugin::clearNativeInstanceTransforms();
            plugin::clearNativeEffectModules();
        });
        QVERIFY(plugin::nativeBehaviorUsesInstance(id));
        composition::Composition shot;
        shot.setSize(320, 180); shot.setFrameRate(30, 1); shot.setDurationSeconds(1);
        auto& plane = shot.addLayer("Plane");
        plane.kind = composition::LayerKind::Plane;
        plane.transform.scalePercent = QPointF(50, 50);
        composition::Effect motion; motion.pluginId = id; motion.name = "Motion Track";
        composition::Clip clip; clip.durationSeconds = 1; clip.effects << motion;
        plane.clips << clip;
        const QRectF still = render::layerGeometry(shot, plane, nullptr, 0).bounds();
        QCOMPARE(still, QRectF(80, 45, 160, 90));
        // Frame 0 unmoved, frame 1 50 right and 20 up (Y up).
        QVector<std::array<float, 16>> matrices(2, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
        matrices[1][12] = 50.0f; matrices[1][13] = 20.0f;
        plugin::setNativeInstanceTransforms(render::nativeInstanceKey(plane.id, 0, 0), 0, matrices);
        QCOMPARE(render::layerGeometry(shot, plane, nullptr, 0).bounds(), still);
        QCOMPARE(render::layerGeometry(shot, plane, nullptr, 1).bounds(), still.translated(50, -20));
        QCOMPARE(render::layerBehaviorTransformAt(shot, nullptr, plane, 1), QTransform::fromTranslate(50, -20));
        // A disabled Behavior moves nothing.
        shot.layerRef(0).clips[0].effects[0].enabled = false;
        QCOMPARE(render::layerGeometry(shot, shot.layers()[0], nullptr, 1).bounds(), still);
    }
    // A 3D sheet's Behaviors move it in the scene, between its parent and
    // its own placement (ParentTransformationAtTime), and its box follows:
    // where the renderer draws the moved plane, frame by frame. The sheet
    // turned 30 degrees on its own moves along the scene's axes, not its own.
    void sheetBoxFollowsItsBehaviors() {
        const core::Identifier id("test.behavior.sheet.motiontrack");
        plugin::registerNativeBehaviorModule(id, QStringLiteral("C:/nowhere/MotionTrack.hfpl"), QString(), false, {});
        auto cleanup = qScopeGuard([] {
            plugin::clearNativeInstanceTransforms();
            plugin::clearNativeEffectModules();
        });
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(30, 1); shot->setDurationSeconds(1);
        {
            auto& plane = shot->addLayer("Plane");
            plane.kind = composition::LayerKind::Plane;
            plane.dimension = composition::LayerDimension::ThreeD;
            plane.planeColor = Qt::red;
            plane.transform.scalePercent = QPointF(50, 50);
            plane.transform.rotationDegrees = 30.0;
            composition::Effect motion; motion.pluginId = id; motion.name = "Motion Track";
            composition::Clip clip; clip.durationSeconds = 1; clip.effects << motion;
            plane.clips << clip;
        }
        const composition::Layer& plane = shot->layers()[0];
        const QRectF still = render::layerGeometry(*shot, plane, nullptr, 0).bounds();
        QVector<std::array<float, 16>> matrices(2, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
        matrices[1][12] = 60.0f; matrices[1][13] = -10.0f;
        plugin::setNativeInstanceTransforms(render::nativeInstanceKey(plane.id, 0, 0), 0, matrices);
        QMatrix4x4 shift; shift.translate(60.0f, -10.0f, 0.0f);
        QCOMPARE(render::layerBehaviorMatrixAt(*shot, nullptr, plane, 1), shift);
        QCOMPARE(render::layerBehaviorTransformAt(*shot, nullptr, plane, 1), QTransform());
        QCOMPARE(render::layerSheetMatrixAt(shot.get(), plane, 1),
                 shift * render::layerSheetMatrixAt(shot.get(), plane, 0));
        const QRectF moved = render::layerGeometry(*shot, plane, nullptr, 1).bounds();
        QVERIFY2(std::abs(moved.center().x() - still.center().x() - 60.0) < 0.01
                     && std::abs(moved.center().y() - still.center().y() - 10.0) < 0.01, "the box stayed");
        // The red pixels the renderer draws at frame 1.
        render::RenderManager manager; manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 1.0 / 30.0, QSize(320, 180));
        QVERIFY(QTest::qWaitFor([&] { return frames.count() > 0; }, 30000));
        const QByteArray bytes = frames[0][1].toByteArray();
        const QImage image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                                    QImage::Format_RGBA8888).copy();
        QRect drawn;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.red() > 128 && c.green() < 80) drawn |= QRect(x, y, 1, 1);
            }
        QVERIFY2(!drawn.isEmpty(), "nothing drawn");
        const auto near = [](double a, double b) { return std::abs(a - b) <= 3.0; };
        QVERIFY2(near(drawn.left(), moved.left()) && near(drawn.right() + 1, moved.right())
                     && near(drawn.top(), moved.top()) && near(drawn.bottom() + 1, moved.bottom()),
                 qPrintable(QStringLiteral("drawn %1,%2 %3x%4 box %5,%6 %7x%8")
                                .arg(drawn.x()).arg(drawn.y()).arg(drawn.width()).arg(drawn.height())
                                .arg(moved.x()).arg(moved.y()).arg(moved.width()).arg(moved.height())));
    }
    // A parented layer's Behaviors act inside its parent (the reference's
    // world is the parent's x the Behaviors x the layer's own): under a
    // parent turned a quarter clockwise, MotionTrack's 50 to the right moves
    // the plane 50 down on the canvas - its box and its drawn pixels.
    void behaviorsActInsideTheParent() {
        const core::Identifier id("test.behavior.parent.motiontrack");
        plugin::registerNativeBehaviorModule(id, QStringLiteral("C:/nowhere/MotionTrack.hfpl"), QString(), false, {});
        auto cleanup = qScopeGuard([] {
            plugin::clearNativeInstanceTransforms();
            plugin::clearNativeEffectModules();
        });
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(30, 1); shot->setDurationSeconds(1);
        core::Identifier parentId;
        {
            auto& parent = shot->addLayer("Parent");
            parent.kind = composition::LayerKind::Point;
            parent.transform.rotationDegrees = 90.0;
            parentId = parent.id;
            auto& plane = shot->addLayer("Plane");
            plane.kind = composition::LayerKind::Plane;
            plane.planeColor = Qt::red;
            plane.transform.scalePercent = QPointF(20, 20);
            plane.parentLayerId = parentId;
            composition::Effect motion; motion.pluginId = id; motion.name = "Motion Track";
            composition::Clip clip; clip.durationSeconds = 1; clip.effects << motion;
            plane.clips << clip;
        }
        const composition::Layer& plane = shot->layers()[1];
        const QRectF still = render::layerGeometry(*shot, plane, nullptr, 1).bounds();
        QVector<std::array<float, 16>> matrices(2, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
        matrices[1][12] = 50.0f;
        plugin::setNativeInstanceTransforms(render::nativeInstanceKey(plane.id, 0, 0), 0, matrices);
        const QRectF moved = render::layerGeometry(*shot, plane, nullptr, 1).bounds();
        QVERIFY2(std::abs(moved.center().x() - still.center().x()) < 0.01
                     && std::abs(moved.center().y() - still.center().y() - 50.0) < 0.01,
                 qPrintable(QStringLiteral("%1,%2 -> %3,%4").arg(still.center().x()).arg(still.center().y())
                                .arg(moved.center().x()).arg(moved.center().y())));
        render::RenderManager manager; manager.setComposition(shot);
        QSignalSpy frames(&manager, &render::RenderManager::frameReady);
        manager.requestFrame(0, 1.0 / 30.0, QSize(320, 180));
        QVERIFY(QTest::qWaitFor([&] { return frames.count() > 0; }, 30000));
        const QByteArray bytes = frames[0][1].toByteArray();
        const QImage image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                                    QImage::Format_RGBA8888).copy();
        QRect drawn;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.red() > 128 && c.green() < 80) drawn |= QRect(x, y, 1, 1);
            }
        QVERIFY2(!drawn.isEmpty(), "nothing drawn");
        QVERIFY2(std::abs(QRectF(drawn).center().x() + 0.5 - moved.center().x()) <= 2.0
                     && std::abs(QRectF(drawn).center().y() + 0.5 - moved.center().y()) <= 2.0,
                 qPrintable(QStringLiteral("drawn %1,%2 box %3,%4").arg(QRectF(drawn).center().x())
                                .arg(QRectF(drawn).center().y()).arg(moved.center().x()).arg(moved.center().y())));
    }
    // Several Behaviors multiply as the reference's do (B1 x B2: the last one
    // nearest the layer, so it acts first): a quarter turn first in the list
    // and a shift of 50 to the right after it take the plane 50 up, where the
    // other order would take it 50 right. And a parent's Behaviors carry its
    // children (ParentingWorldTransformationAtTime has them): the parent's
    // shift moves the child as much. The boxes and the drawn pixels agree.
    void behaviorsMultiplyInTheReferencesOrder() {
        const core::Identifier id("test.behavior.order.motiontrack");
        plugin::registerNativeBehaviorModule(id, QStringLiteral("C:/nowhere/MotionTrack.hfpl"), QString(), false, {});
        auto cleanup = qScopeGuard([] {
            plugin::clearNativeInstanceTransforms();
            plugin::clearNativeEffectModules();
        });
        const auto drawnCentre = [](const std::shared_ptr<composition::Composition>& shot, QColor colour) {
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 1.0 / 30.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 30000)) return QPointF(-1, -1);
            const QByteArray bytes = frames[0][1].toByteArray();
            const QImage image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                                        QImage::Format_RGBA8888).copy();
            QRect drawn;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (std::abs(c.red() - colour.red()) < 60 && std::abs(c.green() - colour.green()) < 60
                        && std::abs(c.blue() - colour.blue()) < 60)
                        drawn |= QRect(x, y, 1, 1);
                }
            return drawn.isEmpty() ? QPointF(-1, -1) : QRectF(drawn).center() + QPointF(0.5, 0.5);
        };
        const auto near = [](const QPointF& a, const QPointF& b, double tolerance) {
            return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
        };
        const auto plane = [&](composition::Composition& shot, const QString& name, QColor colour, int behaviors) {
            auto& layer = shot.addLayer(name);
            layer.kind = composition::LayerKind::Plane;
            layer.planeColor = colour;
            layer.transform.scalePercent = QPointF(20, 20);
            composition::Clip clip; clip.durationSeconds = 1;
            for (int i = 0; i < behaviors; ++i) {
                composition::Effect motion; motion.pluginId = id; motion.name = "Motion Track";
                clip.effects << motion;
            }
            layer.clips << clip;
            return layer.id;
        };
        const auto still = [] {
            return QVector<std::array<float, 16>>(2, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
        };
        {
            auto shot = std::make_shared<composition::Composition>();
            shot->setSize(320, 180); shot->setFrameRate(30, 1); shot->setDurationSeconds(1);
            const core::Identifier layerId = plane(*shot, "Plane", Qt::red, 2);
            auto turn = still();
            QMatrix4x4 quarter; quarter.rotate(90.0f, 0.0f, 0.0f, 1.0f);
            std::copy(quarter.constData(), quarter.constData() + 16, turn[1].begin());
            auto shift = still();
            shift[1][12] = 50.0f;
            plugin::setNativeInstanceTransforms(render::nativeInstanceKey(layerId, 0, 0), 0, turn);
            plugin::setNativeInstanceTransforms(render::nativeInstanceKey(layerId, 0, 1), 0, shift);
            const QPointF centre = render::layerGeometry(*shot, shot->layers()[0], nullptr, 1).bounds().center();
            QVERIFY2(near(centre, QPointF(160, 40), 0.01),
                     qPrintable(QStringLiteral("%1,%2").arg(centre.x()).arg(centre.y())));
            const QPointF drawn = drawnCentre(shot, Qt::red);
            QVERIFY2(near(drawn, centre, 2.0), qPrintable(QStringLiteral("%1,%2").arg(drawn.x()).arg(drawn.y())));
        }
        plugin::clearNativeInstanceTransforms();
        {
            auto shot = std::make_shared<composition::Composition>();
            shot->setSize(320, 180); shot->setFrameRate(30, 1); shot->setDurationSeconds(1);
            // The child above its parent, so its pixels show whole.
            plane(*shot, "Child", Qt::red, 0);
            const core::Identifier parentId = plane(*shot, "Parent", Qt::blue, 1);
            shot->layerRef(0).parentLayerId = parentId;
            shot->layerRef(0).transform.position = QPointF(0, 50);
            shot->layerRef(0).transform.scalePercent = QPointF(100, 100);
            auto shift = still();
            shift[1][12] = 40.0f;
            plugin::setNativeInstanceTransforms(render::nativeInstanceKey(parentId, 0, 0), 0, shift);
            const QPointF child0 = render::layerGeometry(*shot, shot->layers()[0], nullptr, 0).bounds().center();
            const QPointF child1 = render::layerGeometry(*shot, shot->layers()[0], nullptr, 1).bounds().center();
            QVERIFY2(near(child1 - child0, QPointF(40, 0), 0.01),
                     qPrintable(QStringLiteral("%1,%2").arg(child1.x() - child0.x()).arg(child1.y() - child0.y())));
            const QPointF drawn = drawnCentre(shot, Qt::red);
            QVERIFY2(near(drawn, child1, 2.0), qPrintable(QStringLiteral("%1,%2 box %3,%4").arg(drawn.x()).arg(drawn.y())
                                                              .arg(child1.x()).arg(child1.y())));
        }
    }
    // Motion blur samples a layer's Behaviors at their own times, as the
    // reference runs them at the sub-frame's: a plane without keys that
    // MotionTrack's matrices carry 8 pixels a frame is smeared along its
    // way, about where its box is, and a module's matrices are read in
    // between frames.
    void behaviorsBlurAtTheirSubFrames() {
        const core::Identifier id("test.behavior.blur.motiontrack");
        plugin::registerNativeBehaviorModule(id, QStringLiteral("C:/nowhere/MotionTrack.hfpl"), QString(), false, {});
        auto cleanup = qScopeGuard([] {
            plugin::clearNativeInstanceTransforms();
            plugin::clearNativeEffectModules();
        });
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(30, 1); shot->setDurationSeconds(1);
        {
            auto& plane = shot->addLayer("Plane");
            plane.kind = composition::LayerKind::Plane;
            plane.planeColor = Qt::red;
            plane.transform.scalePercent = QPointF(10, 10);
            composition::Effect motion; motion.pluginId = id; motion.name = "Motion Track";
            composition::Clip clip; clip.durationSeconds = 1; clip.effects << motion;
            plane.clips << clip;
        }
        const composition::Layer& plane = shot->layers()[0];
        QVector<std::array<float, 16>> matrices(30, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
        for (int f = 0; f < 30; ++f) matrices[f][12] = 8.0f * (f - 10);
        const QString key = render::nativeInstanceKey(plane.id, 0, 0);
        plugin::setNativeInstanceTransforms(key, 0, matrices);
        std::array<float, 16> between {};
        QVERIFY(plugin::nativeInstanceTransformAt(key, 10.25, &between));
        QCOMPARE(between[12], 2.0f);
        const auto drawn = [&](bool blurred, double* centre) {
            shot->layerRef(0).motionBlur = blurred;
            render::RenderManager manager; manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 10.0 / 30.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 30000)) return -1;
            const QByteArray bytes = frames[0][1].toByteArray();
            const QImage image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                                        QImage::Format_RGBA8888).copy();
            int first = -1, last = -1;
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, 90).red() > 25) { if (first < 0) first = x; last = x; }
            *centre = (first + last + 1) / 2.0;
            return first < 0 ? 0 : last - first + 1;
        };
        double sharpCentre = 0.0, smearedCentre = 0.0;
        const int sharp = drawn(false, &sharpCentre), smeared = drawn(true, &smearedCentre);
        const double box = render::layerGeometry(*shot, shot->layers()[0], nullptr, 10).bounds().center().x();
        QVERIFY2(sharp > 0 && smeared >= sharp + 2,
                 qPrintable(QStringLiteral("%1 %2").arg(sharp).arg(smeared)));
        QVERIFY2(std::abs(sharpCentre - box) <= 1.5 && std::abs(smearedCentre - box) <= 1.5,
                 qPrintable(QStringLiteral("%1 %2 box %3").arg(sharpCentre).arg(smearedCentre).arg(box)));
    }
    // Under a turned and scaled parent: Fit Layer to Box turns the box with
    // the parent and moves the layer in the parent's units, so the box lands
    // where asked; and the footage an instance tracks is placed under its
    // parent, as renderClip draws it.
    void parentedLayersFitAndPlaceTheirFootage() {
        composition::Composition shot;
        shot.setSize(320, 180); shot.setFrameRate(30, 1); shot.setDurationSeconds(1);
        auto& parent = shot.addLayer("Parent");
        parent.kind = composition::LayerKind::Point;
        parent.transform.position = QPointF(40, 10);
        parent.transform.rotationDegrees = 90.0;
        parent.transform.scalePercent = QPointF(200, 200);
        const core::Identifier parentId = parent.id;
        auto& child = shot.addLayer("Child");
        child.kind = composition::LayerKind::Plane;
        child.transform.scalePercent = QPointF(25, 25);
        child.transform.position = QPointF(10, 0);
        child.parentLayerId = parentId;
        composition::Clip clip; clip.durationSeconds = 1; child.clips << clip;
        const QRectF target(20, 30, 100, 60);
        QPointF position, scale;
        QVERIFY(render::fitLayerToBox(shot, shot.layers()[1], nullptr, 0, target, &position, &scale));
        composition::Layer fitted = shot.layers()[1];
        fitted.transform.position = position;
        fitted.transform.scalePercent = scale;
        const QRectF placed = render::layerGeometry(shot, fitted, nullptr, 0).bounds();
        QVERIFY2(std::abs(placed.left() - target.left()) < 0.5 && std::abs(placed.top() - target.top()) < 0.5
                     && std::abs(placed.width() - target.width()) < 0.5 && std::abs(placed.height() - target.height()) < 0.5,
                 qPrintable(QStringLiteral("%1 %2 %3 %4").arg(placed.left()).arg(placed.top())
                                .arg(placed.width()).arg(placed.height())));

        // The footage of an instance-backed effect under a parent that moves
        // it 30 right and 20 up.
        const core::Identifier id("test.parented.stabilizer");
        plugin::registerNativeEffectModule(id, QStringLiteral("C:/nowhere/AutomaticStabilizer.hfpl"), QString(), true, {});
        auto cleanup = qScopeGuard([] { plugin::clearNativeEffectModules(); });
        auto shared = std::make_shared<composition::Composition>();
        shared->setSize(320, 180); shared->setFrameRate(30, 1); shared->setDurationSeconds(1);
        auto& mover = shared->addLayer("Mover");
        mover.kind = composition::LayerKind::Point;
        mover.transform.position = QPointF(30, 20);
        const core::Identifier moverId = mover.id;
        auto& footage = shared->addLayer("Footage");
        footage.parentLayerId = moverId;
        composition::Effect fx; fx.pluginId = id;
        composition::Clip footageClip; footageClip.durationSeconds = 1; footageClip.effects << fx;
        footage.clips << footageClip;
        const core::Identifier footageId = footage.id;
        auto media = std::make_shared<media::MediaManager>();
        ui::NativeInstanceHost host([shared] { return shared; }, [media] { return media; });
        core::Identifier plugin; QStringList values; plugin::NativeCustomUiView view;
        QVERIFY(host.describe({footageId, 0, 0}, 0.0, &plugin, &values, &view));
        QCOMPARE(view.area, QSize(320, 180));
        QCOMPARE(view.matrix[12], 30.0f);
        QCOMPARE(view.matrix[13], -20.0f);
    }
    // The Metallic-Roughness clear coat (Flux's MR shader): the specular the
    // reflection maps take goes under an untinted coat, spec * (1 - coat *
    // F(N.V, 0, 0)) + coat * F(N.V, 0, coat roughness) - a rough, dull
    // surface seen at a grazing angle reflects far more with a smooth coat,
    // and the same seen square on hardly changes.
    void clearCoatBrightensGrazingReflections() {
        model3d::Mesh mesh;
        mesh.positions = {{-100, -100, 0}, {100, -100, 0}, {100, 100, 0}, {-100, 100, 0}};
        mesh.normals = {{0, 0, 1}};
        model3d::Material material;
        material.illuminationModel = model3d::IlluminationModel::MetallicRoughness;
        material.metallic = 0.0;
        material.roughness = 1.0;
        material.diffuseReflectivity = 0.0;
        material.specularReflectivity = 1.0;
        mesh.materials = {material};
        for (const auto& corners : {std::array<int, 3> {0, 1, 2}, std::array<int, 3> {0, 2, 3}}) {
            model3d::Triangle triangle;
            for (int i = 0; i < 3; ++i) { triangle.position[i] = corners[i]; triangle.normal[i] = 0; }
            triangle.material = 0;
            mesh.triangles << triangle;
        }
        QImage white(64, 32, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        model3d::Lighting lighting;
        lighting.reflections = std::make_shared<model3d::Reflections>(model3d::Reflections::fromLatLong(white));
        const QSize size(200, 200);
        const auto brightness = [&](double coat, double tilt) {
            model3d::Mesh coated = mesh;
            coated.materials[0].clearCoat = coat;
            coated.materials[0].clearCoatRoughness = 0.0;
            QMatrix4x4 placement;
            placement.rotate(float(tilt), 1.0f, 0.0f, 0.0f);
            const QImage image = model3d::renderMesh(coated, placement, model3d::defaultCameraForCanvas(size), size,
                                                     lighting, model3d::ShadingMode::Shaded, 1.0);
            return qGray(image.pixel(100, 100));
        };
        const int dull = brightness(0.0, -80.0), coated = brightness(1.0, -80.0);
        QVERIFY2(coated > dull + 40, qPrintable(QStringLiteral("%1 %2").arg(dull).arg(coated)));
        const int square = brightness(0.0, 0.0), squareCoated = brightness(1.0, 0.0);
        QVERIFY2(std::abs(squareCoated - square) <= 3, qPrintable(QStringLiteral("%1 %2").arg(square).arg(squareCoated)));
    }
    // 3D text drawn with Geometry takes its Environment Map on its mesh, as a
    // model does: grey letters reflecting a red picture turn red where the
    // same letters without one stay grey.
    void textGeometryReflectsItsEnvironment() {
        // A Geometry module that is only registered: the text stays the flat
        // glyph mesh, which is enough here.
        const core::Identifier geometryId("test.geometry.flat");
        plugin::registerNativeGeometryModule(geometryId, QStringLiteral("C:/nowhere/Extrude.hfpl"), QString(), true, {});
        auto cleanup = qScopeGuard([] { plugin::clearNativeEffectModules(); });
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QImage redPicture(64, 32, QImage::Format_RGBA8888);
        redPicture.fill(Qt::red);
        const QString redPath = temp.filePath("red.png");
        QVERIFY(redPicture.save(redPath));
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importFile(redPath).isSuccess());
        const core::Identifier redId = media->assetByFilePath(QFileInfo(redPath).absoluteFilePath()).id();
        composition::TextStyle style;
        style.text = QStringLiteral("HH");
        style.fontSize = 120;
        style.fontColor = QColor(90, 90, 90);
        auto shot = std::make_shared<composition::Composition>();
        shot->setSize(320, 180); shot->setFrameRate(24, 1); shot->setDurationSeconds(1);
        {
            auto& layer = shot->addLayer("Text");
            layer.kind = composition::LayerKind::Text;
            layer.dimension = composition::LayerDimension::ThreeD;
            composition::Clip clip; clip.durationSeconds = 1;
            composition::Effect text; text.pluginId = core::Identifier("text");
            text.name = "text"; text.parameterValues = composition::textStyleToParameters(style);
            composition::Effect geometry; geometry.pluginId = geometryId; geometry.name = "Flat";
            clip.effects << text << geometry;
            layer.clips << clip;
            layer.surface.diffuseReflectivity = 100.0;
        }
        const auto frameOf = [&] {
            render::RenderManager manager; manager.setMediaManager(media); manager.setComposition(shot);
            QSignalSpy frames(&manager, &render::RenderManager::frameReady);
            manager.requestFrame(0, 0.0, QSize(320, 180));
            if (!QTest::qWaitFor([&] { return frames.count() > 0; }, 30000)) return QImage();
            const QByteArray bytes = frames[0][1].toByteArray();
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 320, 180, 320 * 4,
                          QImage::Format_RGBA8888).copy();
        };
        // The letters' pixels: bright ones, and how red they are on average.
        const auto letters = [](const QImage& image, double* redShare) {
            int count = 0; double share = 0.0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (qMax(c.red(), qMax(c.green(), c.blue())) < 50) continue;   // the background
                    ++count;
                    share += c.red() / double(qMax(1, c.green() + c.blue() + c.red()));
                }
            *redShare = count ? share / count : 0.0;
            return count;
        };
        double plainShare = 0.0, mirroredShare = 0.0;
        const QImage plain = frameOf();
        QVERIFY2(letters(plain, &plainShare) > 200, "no letters drawn");
        shot->layerRef(0).surface.environmentMap = composition::SurfaceShading::EnvironmentMap::Asset;
        shot->layerRef(0).surface.environmentMapLayer = redId;
        const QImage mirrored = frameOf();
        mirrored.save(testArtifactPath(QStringLiteral("text-geometry-environment.png")));
        QVERIFY(letters(mirrored, &mirroredShare) > 200);
        QVERIFY2(mirroredShare > plainShare + 0.1,
                 qPrintable(QStringLiteral("%1 %2").arg(plainShare).arg(mirroredShare)));
    }
    void legacyProxyChoicesReachEncoderAndPreview() {
        for (const auto& low : {QStringLiteral("Низкое"), QStringLiteral("低")}) {
            const auto args = media::proxyEncodeArguments("source", "output", media::ProxyMode::Performance, low, "libx264");
            QCOMPARE(args.value(args.indexOf("-crf") + 1), QStringLiteral("28"));
        }
        for (const auto& high : {QStringLiteral("Высокое"), QStringLiteral("高")}) {
            const auto args = media::proxyEncodeArguments("source", "output", media::ProxyMode::Quality, high, "h264_qsv");
            QCOMPARE(args.value(args.indexOf("-global_quality") + 1), QStringLiteral("18"));
        }
        for (const auto& automatic : {QStringLiteral("Автоматически"), QStringLiteral("自動"), QStringLiteral("自动")}) {
            QVERIFY(app::proxyPreviewEnabled(automatic, true));
            QVERIFY(!app::proxyPreviewEnabled(automatic, false));
        }
        for (const auto& proxy : {QStringLiteral("Прокси"), QStringLiteral("プロキシ"), QStringLiteral("代理")}) {
            QVERIFY(app::proxyPreviewEnabled(proxy, true));
            QVERIFY(app::proxyPreviewEnabled(proxy, false));
        }
        for (const auto& full : {QStringLiteral("Полное разрешение"), QStringLiteral("フル解像度"), QStringLiteral("完整分辨率")}) {
            QVERIFY(!app::proxyPreviewEnabled(full, true));
            QVERIFY(!app::proxyPreviewEnabled(full, false));
        }
    }

    void proxyPublicationPreservesPreviousFile() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString output = dir.filePath("proxy.mp4");
        const QString part = output + ".part";
        const QByteArray previous("previous proxy"), replacement(2 * 1024 * 1024 + 17, 'n');
        const auto write = [](const QString& path, const QByteArray& data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        const auto read = [](const QString& path) {
            QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QByteArray();
            return file.readAll();
        };
        QVERIFY(write(output, previous));
        QString error;
        QVERIFY(!media::publishProxyFile(part, output, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(read(output), previous);
        QVERIFY(write(part, {}));
        QVERIFY(!media::publishProxyFile(part, output, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(read(output), previous);
        QVERIFY(QFileInfo::exists(part));
        QVERIFY(!media::publishProxyFile(output, output, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(read(output), previous);
        QVERIFY(write(part, replacement));
        const QString directory = dir.filePath("blocked-target");
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(write(QDir(directory).filePath("keep"), previous));
        QVERIFY(!media::publishProxyFile(part, directory, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(read(part), replacement);
        QCOMPARE(read(QDir(directory).filePath("keep")), previous);
#ifdef Q_OS_WIN
        // Allow reads/writes, but deny deletion/rename: force commit to fail.
        HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(output.utf16()), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(locked != INVALID_HANDLE_VALUE);
        auto close = qScopeGuard([locked] { CloseHandle(locked); });
        QVERIFY(!media::publishProxyFile(part, output, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(read(output), previous);
        QCOMPARE(read(part), replacement);
        CloseHandle(locked); close.dismiss();
#endif
        QVERIFY2(media::publishProxyFile(part, output, &error), qPrintable(error));
        QVERIFY(error.isEmpty()); QCOMPARE(read(output), replacement);
        QVERIFY(!QFileInfo::exists(part));
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

        // Failed transcodes and startup errors preserve an existing proxy.
        QFile previousProxy(quality); QVERIFY(previousProxy.open(QIODevice::ReadOnly));
        const auto previousBytes = previousProxy.readAll(); previousProxy.close();
        generator.enqueue(dir.filePath("missing.mp4"), quality, media::ProxyMode::Quality, "Medium", "libx264");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 30000);
        QVERIFY(!finished[2][2].toBool()); QVERIFY(!finished[2][3].toString().isEmpty());
        QVERIFY(previousProxy.open(QIODevice::ReadOnly)); QCOMPARE(previousProxy.readAll(), previousBytes);
        previousProxy.close(); QVERIFY(!QFileInfo::exists(quality + ".part"));
        generator.setFfmpeg(dir.filePath("missing-ffmpeg.exe"));
        generator.enqueue(source, quality, media::ProxyMode::Quality, "Medium", "libx264");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 4, 10000);
        QVERIFY(!finished[3][2].toBool()); QVERIFY(!finished[3][3].toString().isEmpty());
        QVERIFY(previousProxy.open(QIODevice::ReadOnly)); QCOMPARE(previousProxy.readAll(), previousBytes);
        previousProxy.close(); QVERIFY(!QFileInfo::exists(quality + ".part"));

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
        // A removed file another process still holds open (a virus scanner
        // looking at what was just written) stays listed until it lets go.
        QTRY_COMPARE_WITH_TIMEOUT(cachedFiles(), frames - 1, 5000);
        // Looking for a frame does not create its file - an empty one used to
        // be left behind (once its folder existed), and as an entry that
        // "exists" the frame was never stored - and a damaged entry gives way.
        QByteArray pixels;
        const QString key = QStringLiteral("00missing");
        QVERIFY(QDir(dir.path()).mkpath("00"));
        QVERIFY(!cache.load(key, QSize(4, 4), &pixels));
        QVERIFY(!cache.contains(key));
        {
            QFile bad(dir.filePath("00/00missing.ovframe"));
            QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("junk");
        }
        QVERIFY(cache.contains(key));
        QVERIFY(!cache.load(key, QSize(4, 4), &pixels));
        QVERIFY(!cache.contains(key));
        QVERIFY(cache.store(key, QSize(4, 4), QByteArray(64, '')));
        QVERIFY(cache.load(key, QSize(4, 4), &pixels));
        QCOMPARE(pixels, QByteArray(64, ''));
    }
    // The pre-render queue (PreRenderManager): shots one at a time, each with
    // PreRenderProgressLabel's state; a pre-rendered shot edited since is
    // made again once the edits settle; Remove takes the files, cancelling
    // stops everything.
    // A shot's pre-render has its audio too (the reference's pre-render
    // writes video and audio): the shot's mix as a WAV in its folder, which
    // export (and playback) take for the nested shot instead of mixing it
    // again - until the shot is edited, when its old audio no longer matches.
    // The media cache's conformed audio (the reference's ConformedAudioFile):
    // a file's audio decoded once to 16-bit stereo PCM at the shot's rate in
    // MediaCacheFiles, recorded when written, touched when read again; a
    // replaced file is conformed again.
    void mediaAudioIsConformedOnce() {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty()) QSKIP("FFmpeg is required");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString source = dir.filePath("mono44.wav");
        const auto writeTone = [&](qint16 value) {
            QFile wave(source); QVERIFY(wave.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QDataStream stream(&wave); stream.setByteOrder(QDataStream::LittleEndian);
            const quint32 frames = 22050;
            stream.writeRawData("RIFF", 4); stream << quint32(36 + frames * 2);
            stream.writeRawData("WAVEfmt ", 8);
            stream << quint32(16) << quint16(1) << quint16(1) << quint32(44100)
                   << quint32(88200) << quint16(2) << quint16(16);
            stream.writeRawData("data", 4); stream << quint32(frames * 2);
            for (quint32 frame = 0; frame < frames; ++frame) stream << value;
        };
        writeTone(4000);
        QStringList stored, used;
        media::ConformedAudioStore store;
        store.directory = dir.filePath("Conformed");
        store.stored = [&](const QString& key, const QString&) { stored << key; };
        store.used = [&](const QString& key) { used << key; };
        media::ConformedAudioCache cache;
        cache.setStore(store);
        QSignalSpy ready(&cache, &media::ConformedAudioCache::conformedReady);
        QVERIFY(cache.conformed(source, -1, 48000).isEmpty());
        QVERIFY(ready.wait(30000));
        QCOMPARE(stored.size(), 1);
        const QString conformed = cache.conformed(source, -1, 48000);
        QVERIFY(!conformed.isEmpty());
        QVERIFY(conformed.startsWith(store.directory));
        QCOMPARE(used, stored);
        // Its header: PCM, two channels, 48 kHz, 16-bit; half a second.
        QFile file(conformed); QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray header = file.read(44);
        QCOMPARE(header.mid(0, 4), QByteArray("RIFF"));
        QDataStream fields(header.mid(22, 14)); fields.setByteOrder(QDataStream::LittleEndian);
        quint16 channels = 0, bits = 0; quint32 rate = 0, byteRate = 0; quint16 align = 0;
        fields >> channels >> rate >> byteRate >> align >> bits;
        QCOMPARE(int(channels), 2); QCOMPARE(int(rate), 48000); QCOMPARE(int(bits), 16);
        QVERIFY2(std::abs(file.size() - 24000 * 4) < 2000, qPrintable(QString::number(file.size())));
        file.close();
        // Another rate is another file; a replaced file is conformed again.
        QVERIFY(cache.conformed(source, -1, 44100).isEmpty());
        QVERIFY(ready.wait(30000));
        QTest::qWait(1100);   // a modification time of its own
        writeTone(2000);
        QVERIFY(cache.conformed(source, -1, 48000).isEmpty());
        QVERIFY(ready.wait(30000));
        QCOMPARE(stored.size(), 3);
    }
    void preRenderCarriesTheShotsAudio() {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
        if (ffmpeg.isEmpty() || ffprobe.isEmpty()) QSKIP("FFmpeg is required");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString source = dir.filePath("tone.wav");
        {
            QFile wave(source); QVERIFY(wave.open(QIODevice::WriteOnly));
            QDataStream stream(&wave); stream.setByteOrder(QDataStream::LittleEndian);
            const quint32 frames = 48000;
            stream.writeRawData("RIFF", 4); stream << quint32(36 + frames * 4);
            stream.writeRawData("WAVEfmt ", 8);
            stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
                   << quint32(192000) << quint16(4) << quint16(16);
            stream.writeRawData("data", 4); stream << quint32(frames * 4);
            for (quint32 frame = 0; frame < frames; ++frame) stream << qint16(3000) << qint16(3000);
        }
        auto media = std::make_shared<media::MediaManager>();
        QVERIFY(media->importFile(source).isSuccess());
        auto child = std::make_shared<composition::Composition>();
        child->setName("Child"); child->setSize(32, 32); child->setDurationSeconds(1.0);
        child->setAudioSampleRate(48000);
        composition::Clip leaf; leaf.mediaId = media->assetByFilePath(source).id(); leaf.durationSeconds = 1.0;
        child->addLayer("Tone").clips.append(leaf);
        auto parent = std::make_shared<composition::Composition>();
        parent->setName("Parent"); parent->setSize(32, 32); parent->setDurationSeconds(1.0);
        parent->setAudioSampleRate(48000);
        composition::Clip nested; nested.nestedComposition = child; nested.nestedCompositionId = child->id();
        nested.mediaId = core::Identifier(QStringLiteral("composition:") + child->id().value());
        nested.durationSeconds = 1.0;
        parent->addLayer("Nested").clips.append(nested);

        const QString root = dir.filePath("prerenders");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(render::preRenderedAudio(root, *child, media.get()).isEmpty());
        render::PreRenderManager manager;
        manager.setPreRenderRoot(root);
        manager.setMediaManager(media);
        manager.request(child);
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(child->id().value()), render::PreRenderManager::State::Done, 60000);
        const QString audio = render::preRenderedAudio(root, *child, media.get());
        QVERIFY2(!audio.isEmpty(), "no pre-rendered audio");
        QVERIFY(audio.startsWith(render::preRenderFolder(root, *child)));
        // A second of 48 kHz 16-bit stereo, about.
        QVERIFY2(QFileInfo(audio).size() > 190000 && QFileInfo(audio).size() < 200000,
                 qPrintable(QString::number(QFileInfo(audio).size())));
        const auto inputsOf = [&](const QString& preRenders) {
            QTemporaryDir temp;
            render::ExportAudioRequest request;
            request.composition = parent.get(); request.media = media.get();
            request.ffmpeg = ffmpeg; request.ffprobe = ffprobe; request.temporaryDirectory = temp.path();
            request.frameRate = 30.0; request.exportStart = 0.0; request.exportEnd = 1.0;
            request.preRenderDirectory = preRenders;
            QVector<render::ExportAudioInput> inputs; QString error;
            if (!render::buildExportAudioInputs(request, inputs, &error)) return QStringList {error};
            QStringList paths;
            for (const auto& input : inputs) paths << input.path;
            return paths;
        };
        QCOMPARE(inputsOf(root), (QStringList {audio}));
        // Edited: the old audio no longer matches, the shot is mixed again.
        child->layerRef(0).clips[0].audioLevel = -6.0;
        QVERIFY(render::preRenderedAudio(root, *child, media.get()).isEmpty());
        const QStringList live = inputsOf(root);
        QVERIFY2(!live.contains(audio), qPrintable(live.join(", ")));
    }
    void preRendersWaitTheirTurn() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto makeShot = [](const QString& name, const QColor& color) {
            auto shot = std::make_shared<composition::Composition>();
            shot->setName(name); shot->setSize(32, 32); shot->setDurationSeconds(0.5);
            auto& plane = shot->addLayer("Plane");
            plane.kind = composition::LayerKind::Plane; plane.planeColor = color;
            composition::Clip clip; clip.durationSeconds = 0.5; plane.clips.append(clip);
            return shot;
        };
        auto first = makeShot("First", Qt::red), second = makeShot("Second", Qt::blue);
        const int frames = qRound(0.5 * double(first->fpsNumerator()) / qMax(1, first->fpsDenominator()));
        const auto files = [&](const std::shared_ptr<composition::Composition>& shot) {
            int count = 0;
            QDirIterator it(render::preRenderFolder(dir.path(), *shot), {"*.ovframe"}, QDir::Files,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) { it.next(); ++count; }
            return count;
        };
        render::PreRenderManager manager;
        manager.setPreRenderRoot(dir.path());
        manager.setSettleDelay(50);
        using State = render::PreRenderManager::State;
        QStringList seen;
        connect(&manager, &render::PreRenderManager::stateChanged, this,
                [&](const QString& id) { seen << id + ":" + QString::number(int(manager.state(id))); });
        const QString a = first->id().value(), b = second->id().value();
        manager.request(first);
        manager.request(second);
        // One at a time: the second waits.
        QVERIFY(manager.state(a) == State::Preparing || manager.state(a) == State::InProgress);
        QCOMPARE(manager.state(b), State::Queued);
        QCOMPARE(manager.statusText(b), QString("The pre-render is in the queue."));
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(a), State::Done, 15000);
        QCOMPARE(manager.statusText(a), QString("The media has been pre-rendered."));
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(b), State::Done, 15000);
        QVERIFY(!manager.isBusy());
        QCOMPARE(files(first), frames); QCOMPARE(files(second), frames);
        QCOMPARE(manager.framesDone(a), manager.framesTotal(a));
        // The second never ran before the first was done.
        QVERIFY(seen.indexOf(a + ":" + QString::number(int(State::Done)))
                < seen.indexOf(b + ":" + QString::number(int(State::InProgress))));

        // Edited: the old frames go, the shot is made again.
        first->layerRef(0).planeColor = Qt::green;
        manager.checkForChanges();
        QCOMPARE(manager.state(a), State::None);
        QCOMPARE(files(first), 0);
        QCOMPARE(manager.state(b), State::Done);   // untouched
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(a), State::Done, 15000);
        QCOMPARE(files(first), frames);
        manager.checkForChanges();                 // nothing changed since
        QCOMPARE(manager.state(a), State::Done);

        // Remove: out of the queue, its files gone.
        manager.remove(second);
        QCOMPARE(manager.state(b), State::None);
        QVERIFY(!QFileInfo::exists(render::preRenderFolder(dir.path(), *second)));

        // Cancelling stops the running one and empties the queue.
        manager.request(first); manager.request(second);
        manager.cancelAll();
        QVERIFY(!manager.isBusy());
        QCOMPARE(manager.state(a), State::None); QCOMPARE(manager.state(b), State::None);
        QTest::qWait(50);
        QVERIFY(!manager.isBusy());
    }
    void exportRendersFramesAtOnce() {
        QCOMPARE(render::ExportJob::rendererCount(3, 10), 3);
        QCOMPARE(render::ExportJob::rendererCount(8, 2), 2);
        const int automatic = render::ExportJob::rendererCount(0, 100);
        QVERIFY(automatic >= 1 && automatic <= 4);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        auto shot = std::make_shared<composition::Composition>();
        shot->setName("Moving"); shot->setSize(64, 48); shot->setFrameRate(10, 1); shot->setDurationSeconds(1.2);
        auto& plane = shot->addLayer("Plane");
        plane.kind = composition::LayerKind::Plane; plane.planeColor = QColor(30, 160, 220);
        plane.transform.scalePercent = QPointF(25, 25);
        plane.transform.positionXCurve.set(0, 10.0);
        plane.transform.positionXCurve.set(11, 54.0);
        composition::Clip fill; fill.durationSeconds = 1.2; plane.clips.append(fill);
        auto media = std::make_shared<media::MediaManager>();
        const int frames = 12;
        const auto run = [&](const QString& name, int threads, const QString& cache, int* fromCache) {
            render::ExportJob::Request request;
            request.composition = shot; request.media = media;
            request.outputPath = dir.filePath(name + "/frame.png");
            request.lastFrame = frames - 1; request.hardwareEncoding = false;
            request.renderThreads = threads; request.cacheDirectory = cache;
            render::ExportJob job(request);
            QSignalSpy finished(&job, &render::ExportJob::finished);
            QSignalSpy progress(&job, &render::ExportJob::progress);
            job.start();
            QVector<QImage> images;
            if (!QTest::qWaitFor([&] { return finished.count() > 0; }, 60000) || !finished[0][0].toBool())
                return images;
            // Frames finish out of order, the count still goes up one by one.
            for (int i = 0; i < progress.size(); ++i)
                if (progress[i][0].toInt() != i || progress[i][1].toInt() != frames) return images;
            *fromCache = job.framesFromCache();
            for (int i = 1; i <= frames; ++i)
                images << QImage(dir.filePath(QString("%1/frame-%2.png").arg(name).arg(i, 8, 10, QLatin1Char('0'))))
                              .convertToFormat(QImage::Format_RGBA8888);
            return images;
        };
        int fromCache = -1;
        const QVector<QImage> serial = run("one", 1, QString(), &fromCache);
        QCOMPARE(serial.size(), frames);
        QCOMPARE(fromCache, 0);
        QVERIFY(serial.first() != serial.last());   // the plane moves
        // Three renderers at once make the same frames.
        const QVector<QImage> parallel = run("three", 3, QString(), &fromCache);
        QCOMPARE(parallel.size(), frames);
        for (int i = 0; i < frames; ++i) QVERIFY2(parallel[i] == serial[i], qPrintable(QString::number(i)));

        // Turbo: frames the playback cache made of this state come from the
        // timeline cache instead of being rendered again.
        const QString cacheDirectory = dir.filePath("cache");
        {
            render::RenderManager cacher;
            cacher.setComposition(shot); cacher.setMediaManager(media);
            cacher.setDiskCacheDirectory(cacheDirectory);
            QSignalSpy cached(&cacher, &render::RenderManager::playbackCacheFinished);
            cacher.startPlaybackCache(0.0, QSize(64, 48), true, false);
            QTRY_COMPARE_WITH_TIMEOUT(cached.count(), 1, 30000);
        }
        const QVector<QImage> turbo = run("turbo", 2, cacheDirectory, &fromCache);
        QCOMPARE(turbo.size(), frames);
        QCOMPARE(fromCache, frames);
        for (int i = 0; i < frames; ++i) QVERIFY2(turbo[i] == serial[i], qPrintable(QString::number(i)));
        // An edited shot no longer matches the cache: rendered afresh.
        shot->layerRef(0).planeColor = QColor(220, 60, 30);
        const QVector<QImage> edited = run("edited", 2, cacheDirectory, &fromCache);
        QCOMPARE(edited.size(), frames);
        QCOMPARE(fromCache, 0);
        QVERIFY(edited.first() != serial.first());
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

        // A Half preview draws the mesh reduced: the full frame reduced.
        const QImage expected = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                                    .scaled(160, 120, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                    .convertToFormat(QImage::Format_RGBA8888);
        ready.clear();
        manager.requestFrame(2, 0.0, QSize(160, 120), true, true);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        manager.cancelAll();
        QCOMPARE(ready[0][2].toSize(), QSize(160, 120));
        const QByteArray halfBytes = ready[0][1].toByteArray();
        const QImage half(reinterpret_cast<const uchar*>(halfBytes.constData()), 160, 120, 160 * 4,
                          QImage::Format_RGBA8888);
        double sum = 0.0;
        for (int y = 0; y < 120; ++y)
            for (int x = 0; x < 160; ++x) {
                const QColor a = half.pixelColor(x, y), b = expected.pixelColor(x, y);
                sum += (qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue())
                        + qAbs(a.alpha() - b.alpha())) / 4.0;
            }
        half.save(testArtifactPath(QStringLiteral("half-geometry.png")));
        QVERIFY2(sum / (160.0 * 120.0) < 3.0, qPrintable(QStringLiteral("mean %1").arg(sum / (160.0 * 120.0))));

        // Its shadow is its mesh's: turned edge-on to a light in front, the
        // extruded "O" still throws its 40-unit walls onto a red floor
        // behind it, where its flat picture would throw a line.
        auto lit = std::make_shared<composition::Composition>();
        lit->setSize(320, 240); lit->setDurationSeconds(1);
        {
            auto& floor = lit->addLayer("Floor");
            composition::Clip plain; plain.durationSeconds = 1;
            floor.kind = composition::LayerKind::Plane; floor.planeColor = Qt::red;
            floor.dimension = composition::LayerDimension::ThreeD; floor.clips << plain;
            floor.transform.positionZ = -100;
            floor.transform.scalePercent = QPointF(300, 300);
            auto& word = lit->addLayer("Text");
            word.kind = composition::LayerKind::Text; word.dimension = composition::LayerDimension::ThreeD;
            word.clips << clip;
            word.transform.rotationYDegrees = 90;
            word.surface.castShadows = true;
            auto& lamp = lit->addLayer("Lamp");
            lamp.kind = composition::LayerKind::Light; lamp.dimension = composition::LayerDimension::ThreeD;
            lamp.transform.position = QPointF(300, 0); lamp.transform.positionZ = 400;
            lamp.light.castShadows = true; lamp.light.shadowOpacity = 100;
        }
        const auto darkInRow = [&](const QString& name) {
            render::RenderManager shaded; shaded.setComposition(lit);
            QSignalSpy shadedReady(&shaded, &render::RenderManager::frameReady);
            shaded.requestFrame(3, 0.0, QSize(320, 240), true, true);
            if (!QTest::qWaitFor([&] { return shadedReady.count() > 0; }, 10000)) return -1;
            shaded.cancelAll();
            const QByteArray shadedBytes = shadedReady[0][1].toByteArray();
            const QImage frame(reinterpret_cast<const uchar*>(shadedBytes.constData()), 320, 240, 320 * 4,
                               QImage::Format_RGBA8888);
            frame.copy().save(testArtifactPath(name));
            int dark = 0;
            for (int x = 0; x < 320; ++x) {
                const QColor c = frame.pixelColor(x, 80);
                if (c.red() < 60 && c.green() < 60 && c.blue() < 60) ++dark;
            }
            return dark;
        };
        const int casting = darkInRow(QStringLiteral("geometry-shadow.png"));
        lit->layerRef(1).surface.castShadows = false;
        const int none = darkInRow(QStringLiteral("geometry-no-shadow.png"));
        QVERIFY2(casting > none + 20, qPrintable(QStringLiteral("%1 %2").arg(casting).arg(none)));
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
