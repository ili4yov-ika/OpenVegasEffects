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
          << layer.name << int(layer.kind) << layer.zIndex << layer.visible << layer.locked << layer.muted << layer.motionBlur << layer.labelColor
          << layer.blendMode << layer.opacity << int(layer.dimension) << layer.camera.zoom
          << layer.camera.focusDistance << layer.camera.aperture << layer.camera.blur
          << layer.camera.depthOfField << layer.camera.nearClip << layer.camera.farClip;
        writeCurve(s, layer.camera.zoomCurve);
        writeCurve(s, layer.camera.focusDistanceCurve);
        writeCurve(s, layer.camera.apertureCurve);
        writeCurve(s, layer.camera.blurCurve);
        for (auto p : transformPropertiesFor(LayerDimension::ThreeD)) {
            for (int axis = 0; axis < axisCount(p, LayerDimension::ThreeD); ++axis) {
                s << layer.transform.valueAt(p, axis, 0);
                if (auto* curve = layer.transform.curve(p, axis)) writeCurve(s, *curve);
            }
        }
        const auto& t = layer.transform;
        s << t.anchorPoint << t.anchorPointZ << t.position << t.scalePercent << t.rotationDegrees << t.positionZ << t.scaleZPercent
          << t.rotationXDegrees << t.rotationYDegrees << t.orientationX << t.orientationY << t.orientationZ
          << layer.planeColor << layer.modelAssetId.value() << t.audioLevel;
        writeCurve(s, t.audioLevelCurve);
        s << int(layer.autoOrient) << layer.autoOrientAxis << layer.autoOrientLayer.value()
          << t.targetX << t.targetY << t.targetZ;
        writeCurve(s, t.targetXCurve);
        writeCurve(s, t.targetYCurve);
        writeCurve(s, t.targetZCurve);
        const LightSettings& light = layer.light;
        s << int(light.type) << light.color << light.intensity << light.castShadows << int(light.falloff)
          << light.reach << light.coneAngle << light.feather << light.shadowOpacity << light.shadowDiffusion
          << light.shadowColor;
        writeCurve(s, light.intensityCurve);
        for (const KeyFrameList* curve : {&light.colorCurve, &light.reachCurve, &light.coneAngleCurve,
                                          &light.featherCurve, &light.shadowOpacityCurve,
                                          &light.shadowDiffusionCurve, &light.shadowColorCurve})
            writeCurve(s, *curve);
        const SurfaceShading& surface = layer.surface;
        s << surface.illuminated << surface.castShadows << surface.receiveShadows
          << surface.shadowColor << surface.ambient << surface.diffuse
          << surface.specular << surface.shininess << surface.emissive << surface.castShadowsIfInvisible
          << surface.billboardedLights << surface.billboardedShadows << surface.selfShadowBias
          << surface.castAmbientOcclusion << surface.receiveAmbientOcclusion << surface.aoSamples
          << surface.aoSampleRadius << surface.aoIntensity << surface.aoDepthScale << surface.aoBias
          << surface.aoBlurRadius << surface.aoDepthFalloff << surface.includeSemiTransparentTriangles
          << surface.castReflections << surface.castReflectionsIfInvisible << surface.receiveReflections
          << surface.specularReflectivity << surface.diffuseReflectivity << int(surface.environmentMap)
          << surface.environmentMapLayer.value();
        for (const KeyFrameList* curve : {&surface.ambientCurve, &surface.diffuseCurve, &surface.specularCurve,
                                          &surface.shininessCurve, &surface.emissiveCurve,
                                          &surface.specularReflectivityCurve, &surface.diffuseReflectivityCurve})
            writeCurve(s, *curve);
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
        // Model nodes only once a layer has any, so other layers keep their
        // state as it was.
        if (!layer.modelNodes.isEmpty()) {
            s << QStringLiteral("model-nodes") << layer.modelNodes.size();
            for (const auto& node : layer.modelNodes) {
                s << node.path;
                for (auto p : transformPropertiesFor(LayerDimension::ThreeD)) {
                    for (int axis = 0; axis < axisCount(p, LayerDimension::ThreeD); ++axis) {
                        s << node.transform.valueAt(p, axis, 0);
                        if (auto* curve = node.transform.curve(p, axis)) writeCurve(s, *curve);
                    }
                }
                s << node.transform.anchorPoint << node.transform.anchorPointZ;
            }
        }
        if (!layer.materialOverrides.isEmpty()) {
            s << QStringLiteral("material-overrides") << layer.materialOverrides.size();
            for (const auto& material : layer.materialOverrides) {
                s << material.material << material.bumpMapScale;
                for (const auto& map : material.maps) s << int(map.source) << map.layer.value();
            }
        }
        if (!layer.modelAnimations.isEmpty()) {
            s << QStringLiteral("model-animations") << layer.modelAnimations.size();
            for (const auto& animation : layer.modelAnimations) {
                s << animation.id.value() << animation.name << animation.filePath << animation.animationIndex
                  << animation.frameRateOverridden << animation.frameRate << animation.strength
                  << animation.loop << animation.customPlayback << animation.playback;
                writeCurve(s, animation.strengthCurve);
                writeCurve(s, animation.playbackCurve);
            }
        }
        s << layer.spanStartSeconds << layer.spanEndSeconds;
        s << layer.clips.size();
        for (const auto& clip : layer.clips) {
            s << clip.mediaId.value() << clip.startSeconds << clip.durationSeconds << clip.sourceStartSeconds
              << clip.speed << clip.audioLevel << clip.effects.size();
            for (const auto& effect : clip.effects) {
                s << effect.pluginId.value() << effect.name << effect.enabled << effect.parameterValues
                  << int(effect.transitionEdge) << effect.transitionSeconds << effect.animation.size();
                // A tracked layer moves with the module's data, not its values.
                if (!effect.instanceData.isEmpty()) s << effect.instanceData;
                for (auto it = effect.animation.cbegin(); it != effect.animation.cend(); ++it) {
                    s << it.key(); writeCurve(s, it.value());
                }
            }
        }
    }
    return bytes;
}
// The static value of a transform property; Opacity is not the
// transform's but its layer's, and is left to setLayerTransformValue.
inline void setTransformValue(LayerTransform& t, TransformProperty prop, int axis, double value)
{
    switch (prop) {
    case TransformProperty::Opacity: break;
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
    case TransformProperty::AudioLevel: t.audioLevel = value; break;
    case TransformProperty::Target:
        if (axis == 0) t.targetX = value; else if (axis == 1) t.targetY = value; else t.targetZ = value; break;
    }
}
// What a model node's transform shows: a node's own properties, without the
// layer's opacity (and its audio).
inline QVector<TransformProperty> modelNodeTransformProperties()
{
    return {TransformProperty::AnchorPoint, TransformProperty::Position, TransformProperty::Scale,
            TransformProperty::Orientation, TransformProperty::RotationX,
            TransformProperty::RotationY, TransformProperty::Rotation};
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
    case TransformProperty::AudioLevel: t.audioLevel = value; break;
    case TransformProperty::Target:
        if (axis == 0) t.targetX = value; else if (axis == 1) t.targetY = value; else t.targetZ = value; break;
    }
}
}
