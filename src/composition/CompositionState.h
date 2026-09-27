#pragma once
#include "composition/Composition.h"
#include <QDataStream>
#include <QIODevice>
namespace openvegas::composition {
inline void writeCurve(QDataStream& stream, const KeyFrameList& curve)
{
    stream << curve.defaultValue() << curve.canInterpolate() << curve.count();
    for (int frame : curve.locations()) {
        const auto& k = *curve.at(frame);
        stream << k.id << k.frame << k.value << int(k.temporal) << k.incomingHandle << k.outgoingHandle
               << k.incomingInfluence << k.outgoingInfluence << k.handlesLocked;
    }
}
inline QByteArray layerState(const QVector<Layer>& layers)
{
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    s << layers.size();
    for (const auto& layer : layers) {
        s << layer.id.value() << layer.parentLayerId.value()
          << layer.name << int(layer.kind) << layer.zIndex << layer.visible << layer.locked << layer.muted << layer.labelColor
          << layer.blendMode << layer.opacity << int(layer.dimension) << layer.cameraFieldOfView;
        for (auto p : transformPropertiesFor(LayerDimension::ThreeD)) {
            for (int axis = 0; axis < axisCount(p, LayerDimension::ThreeD); ++axis) {
                s << layer.transform.valueAt(p, axis, 0);
                if (auto* curve = layer.transform.curve(p, axis)) writeCurve(s, *curve);
            }
        }
        const auto& t = layer.transform;
        s << t.anchorPoint << t.anchorPointZ << t.position << t.scalePercent << t.rotationDegrees << t.positionZ << t.scaleZPercent
          << t.rotationXDegrees << t.rotationYDegrees << t.orientationX << t.orientationY << t.orientationZ
          << layer.planeColor << layer.modelAssetId.value();
        s << layer.masks.size();
        for (const auto& mask : layer.masks)
            s << mask.id.value() << mask.name << int(mask.shape) << mask.bounds
              << mask.points << mask.enabled << mask.inverted << mask.opacity << mask.feather << mask.expansion;
        s << layer.motionTracks.size();
        for (const auto& track : layer.motionTracks) {
            s << track.id.value() << track.name << track.enabled << track.point
              << track.sampleRadius << track.searchRadius;
            writeCurve(s, track.xCurve);
            writeCurve(s, track.yCurve);
        }
        s << layer.clips.size();
        for (const auto& clip : layer.clips) {
            s << clip.mediaId.value() << clip.startSeconds << clip.durationSeconds << clip.sourceStartSeconds
              << clip.speed << clip.audioLevel << clip.effects.size();
            for (const auto& effect : clip.effects) {
                s << effect.pluginId.value() << effect.name << effect.enabled << effect.parameterValues << effect.animation.size();
                for (auto it = effect.animation.cbegin(); it != effect.animation.cend(); ++it) {
                    s << it.key(); writeCurve(s, it.value());
                }
            }
        }
    }
    return bytes;
}
inline void setLayerTransformValue(Layer& layer, TransformProperty prop, int axis, double value)
{
    auto& t = layer.transform;
    switch (prop) {
    case TransformProperty::Opacity: layer.opacity = qBound(0.0, value / 100., 1.); break;
    case TransformProperty::Position:
        if (axis == 0) t.position.setX(value); else if (axis == 1) t.position.setY(value); else t.positionZ = value; break;
    case TransformProperty::AnchorPoint:
        if (axis == 0) t.anchorPoint.setX(value); else if (axis == 1) t.anchorPoint.setY(value); else t.anchorPointZ = value; break;
    case TransformProperty::Scale:
        if (axis == 0) t.scalePercent.setX(value); else if (axis == 1) t.scalePercent.setY(value); else t.scaleZPercent = value; break;
    case TransformProperty::Orientation:
        if (axis == 0) t.orientationX = value; else if (axis == 1) t.orientationY = value; else t.orientationZ = value; break;
    case TransformProperty::RotationX: t.rotationXDegrees = value; break;
    case TransformProperty::RotationY: t.rotationYDegrees = value; break;
    case TransformProperty::Rotation: t.rotationDegrees = value; break;
    }
}
}
