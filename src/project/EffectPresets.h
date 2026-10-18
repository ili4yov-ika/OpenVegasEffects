#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "composition/Effect.h"

namespace openvegas {
namespace project {

// An effect preset file (.hfpr), as the reference writes it
// (VegasEffects.exe FUN_14038be20) and reads it (FUN_14038e350):
//   <Preset Type="2D"|"3D" Version="1" Editable="1" HasVisualEffects
//           HasAudioEffects HasGeometryEffects HasBehaviorEffects
//           HasCompositeShotOnly>
//     <ID>...</ID>
//     <EffectsPreset> <Instance>...</Instance> ... </EffectsPreset>
//   </Preset>
// Each Instance is an EffectInstance as a project holds it. A 3D preset
// keeps an Effect3DLayer instead, which this port has no layer for.
struct EffectPreset
{
    QString path;
    QString name;            // the file's base name
    QString id;
    bool threeD = false;
    bool editable = true;
    bool hasVisualEffects = false;
    bool hasAudioEffects = false;
    bool hasGeometryEffects = false;
    bool hasBehaviorEffects = false;
    bool compositeShotOnly = false;
};

// The newest preset version the reference reads.
constexpr int kEffectPresetVersion = 1;

// What the reference tells the user (FUN_14038e1d0), empty on success.
QString effectPresetError(int code);
enum EffectPresetResult
{
    PresetOk = 1000,
    PresetCannotOpen = 1001,
    PresetCannotWrite = 1002,
    PresetCannotRead = 1003,
    PresetNoPath = 1004,
    PresetTooNew = 1005,
    PresetNothingToSave = 1006,   // "Error during preset creation."
};

// The preset's header; false (with the reason) when it cannot be used.
bool readEffectPreset(const QString& path, EffectPreset* preset, int* result = nullptr);
// The effects a 2D preset applies, their keys from the layer's start at
// `fps`; empty with the reason when there are none to apply.
QVector<composition::Effect> effectPresetEffects(const QString& path, double fps, int* result = nullptr);
// Writes `effects` (keys from the layer's start) as a new 2D preset.
int writeEffectPreset(const QString& path, const QVector<composition::Effect>& effects, double fps);

// Every .hfpr under `directory`, sorted by name.
QVector<EffectPreset> scanEffectPresets(const QString& directory);
// `path`, or "<name> (N).<suffix>" beside it when that is taken
// (VegasEffects.exe FUN_14066e230).
QString uniqueFilePath(const QString& path);

} // namespace project
} // namespace openvegas
