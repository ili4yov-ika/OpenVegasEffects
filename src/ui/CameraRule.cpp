#include "ui/CameraRule.h"

#include "model3d/Renderer3D.h"
#include "ui/PromptMessage.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>

namespace openvegas {
namespace ui {

bool compositionIs3D(const composition::Composition& composition)
{
    return std::any_of(composition.layers().cbegin(), composition.layers().cend(),
                       [](const composition::Layer& layer) {
                           return layer.kind == composition::LayerKind::Camera;
                       });
}

bool layerNeedsCamera(const composition::Layer& layer)
{
    if (layer.kind == composition::LayerKind::Camera) return false;   // it is the camera
    return layer.dimension == composition::LayerDimension::ThreeD
           || layer.kind == composition::LayerKind::Light
           || layer.kind == composition::LayerKind::Model3D;
}

composition::Layer newCameraLayer(const composition::Composition& composition)
{
    composition::Layer camera;
    camera.name = QCoreApplication::translate("LayerFactory", "New Camera");
    camera.kind = composition::LayerKind::Camera;
    camera.dimension = composition::LayerDimension::ThreeD;
    camera.blendMode = QStringLiteral("None");
    // The default camera looks at the shot from where its height fills the
    // vertical field of view; the camera layer starts there with the same lens.
    const model3d::Camera standIn = model3d::defaultCameraForCanvas(composition.displaySize());
    camera.cameraFieldOfView = standIn.fieldOfViewDegrees;
    camera.transform.positionZ = standIn.position.z();
    return camera;
}

bool confirmAddCamera(QWidget* parent, AddCameraReason reason)
{
    QString text;
    switch (reason) {
    case AddCameraReason::SetDimension:
        // The reference's own string, trailing space included.
        text = QCoreApplication::translate(
            "CompositionTools",
            "Composite shots cannot contain 3D layers without a camera. \n\n"
            "Do you want to add a camera?");
        break;
    case AddCameraReason::CreateLayer:
        text = QCoreApplication::translate(
            "CompositionTools",
            "To create this layer you must first add a camera.\n\nDo you want to add a camera?");
        break;
    case AddCameraReason::PasteLayers:
        text = QCoreApplication::translate(
            "PasteUtilities",
            "The composite shot requires a camera to accommodate the layers to be pasted.\n\n"
            "Do you want to add a camera?");
        break;
    }
    // FUN_1405b2bc0: a question box without a title, Yes|Cancel, Yes by
    // default, tagged "add-messagebox-auto-accept" - once silenced it accepts.
    return showPrompt(parent, QStringLiteral("Adding3DCameras"), QMessageBox::Question, QString(),
                      text, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes, {},
                      QMessageBox::Yes)
           == QMessageBox::Yes;
}

bool removesLastCamera(const composition::Composition& composition, const QVector<int>& removed)
{
    const QSet<int> gone(removed.cbegin(), removed.cend());
    bool hadCamera = false;
    bool keepsCamera = false;
    bool keepsLayer = false;
    for (int i = 0; i < composition.layers().size(); ++i) {
        const bool camera = composition.layers().at(i).kind == composition::LayerKind::Camera;
        hadCamera |= camera;
        if (gone.contains(i)) continue;
        keepsLayer = true;
        keepsCamera |= camera;
    }
    // No question when a camera stays, or when everything goes.
    return hadCamera && !keepsCamera && keepsLayer;
}

bool confirmRemoveLastCamera(QWidget* parent)
{
    return showPrompt(parent, QStringLiteral("Removing3DCameras"), QMessageBox::Question, QString(),
                      QCoreApplication::translate(
                          "CompositionTools",
                          "3D composite shots must have at least one camera.\n\n"
                          "All 3D layers will be converted to 2D or removed if all cameras are "
                          "removed.\n\nDo you want to continue?"),
                      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes, {},
                      QMessageBox::Yes)
           == QMessageBox::Yes;
}

bool convertTo2D(composition::Composition& composition)
{
    bool changed = false;
    for (int i = composition.layers().size() - 1; i >= 0; --i) {
        const composition::Layer& layer = composition.layers().at(i);
        if (layer.kind == composition::LayerKind::Light) {
            composition.removeLayer(i);
            changed = true;
        } else if (layer.kind != composition::LayerKind::Model3D
                   && layer.kind != composition::LayerKind::Camera
                   && layer.dimension == composition::LayerDimension::ThreeD) {
            composition.layerRef(i).dimension = composition::LayerDimension::TwoD;
            changed = true;
        }
    }
    return changed;
}

LayerStackCommand::LayerStackCommand(std::shared_ptr<composition::Composition> composition,
                                     QVector<composition::Layer> before,
                                     QVector<composition::Layer> after, const QString& text,
                                     std::function<void()> notify, bool alreadyApplied)
    : QUndoCommand(text)
    , m_composition(std::move(composition))
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_notify(std::move(notify))
    , m_skipRedo(alreadyApplied)
{
}

void LayerStackCommand::undo()
{
    apply(m_before);
}

void LayerStackCommand::redo()
{
    if (m_skipRedo) {
        m_skipRedo = false;
        return;
    }
    apply(m_after);
}

void LayerStackCommand::apply(const QVector<composition::Layer>& layers)
{
    if (!m_composition) return;
    m_composition->setLayers(layers);
    if (m_notify) m_notify();
}

} // namespace ui
} // namespace openvegas
