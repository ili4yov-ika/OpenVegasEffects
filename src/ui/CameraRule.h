#pragma once

#include "composition/Composition.h"

#include <QUndoCommand>
#include <QVector>

#include <functional>
#include <memory>

class QWidget;

namespace openvegas {
namespace ui {

// The reference's rule that 3D layers need a camera (MARKDOWN/RE_3D_Camera_Rule.md).
//
// CompositionAsset::Is3D (Project.dll 0x180372bc0) is not a flag: a composite
// shot is 3D exactly when one of its layers is a camera. Making a layer 3D,
// creating or pasting one in a shot without a camera first asks to add a
// camera (ShowAddCameraDialog); removing the last camera asks to turn the shot
// back to 2D (ShowRemoveCameraDialog). Projects that already hold 3D layers
// without a camera are left as they are - the renderer's default camera still
// shows them - the reference does not migrate them either.

bool compositionIs3D(const composition::Composition& composition);

// Layers that only exist in a 3D shot - Dimension() != 0 in the reference: a
// layer switched to 3D, a light, a 3D model. Not the camera itself.
bool layerNeedsCamera(const composition::Layer& layer);

// FUN_140a45760: the camera the prompts add, "New Camera" over the whole shot,
// standing where the renderer's default camera does, so adding it does not
// move the picture.
composition::Layer newCameraLayer(const composition::Composition& composition);

enum class AddCameraReason
{
    SetDimension,   // FUN_1405a38a0 "Set Layer Dimension(s)"
    CreateLayer,    // FUN_1405a8740
    PasteLayers,    // FUN_140a59960 "Paste Layer(s)"
};

// The question (Yes|Cancel, Yes by default) under the Options key
// "Adding3DCameras" ("Prompt me before converting 2D composite shots to 3D").
// True means add the camera and go ahead; false means cancel the action.
bool confirmAddCamera(QWidget* parent, AddCameraReason reason);

// Remove Layer(s) (FUN_140593b70): removing `removed` takes every camera of a
// 3D shot while other layers stay.
bool removesLastCamera(const composition::Composition& composition, const QVector<int>& removed);

// "Removing3DCameras" ("Prompt me before converting 3D composite shots to 2D").
bool confirmRemoveLastCamera(QWidget* parent);

// What the reference does once the last camera is gone (lambda
// FUN_1405b2af0): lights are removed and every other 3D layer goes back to 2D.
// A 3D model cannot be 2D in this port, so it stays and the default camera
// keeps showing it. Returns whether anything changed.
bool convertTo2D(composition::Composition& composition);

// One undo record over the whole layer stack, for edits that add or remove
// layers beside the change itself (a camera added with a 3D switch, lights
// removed with the last camera). `notify` runs after undo and redo.
class LayerStackCommand : public QUndoCommand
{
public:
    LayerStackCommand(std::shared_ptr<composition::Composition> composition,
                      QVector<composition::Layer> before, QVector<composition::Layer> after,
                      const QString& text, std::function<void()> notify,
                      bool alreadyApplied = true);
    void undo() override;
    void redo() override;

private:
    void apply(const QVector<composition::Layer>& layers);
    std::shared_ptr<composition::Composition> m_composition;
    QVector<composition::Layer> m_before;
    QVector<composition::Layer> m_after;
    std::function<void()> m_notify;
    bool m_skipRedo = true;
};

} // namespace ui
} // namespace openvegas
