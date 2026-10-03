#pragma once

#include "core/Identifier.h"

#include <QStringList>
#include "plugin/EffectSpec.h"
#include "plugin/NativeEffectRender.h"

class QImage;

namespace openvegas {
namespace plugin {

// Identifiers of the built-in effects registered by the default plugin manager.
inline constexpr const char* kBuiltinGrayscale = "openvegas.builtin.grayscale";
inline constexpr const char* kBuiltinSepia = "openvegas.builtin.sepia";
inline constexpr const char* kBuiltinInvert = "openvegas.builtin.invert";
inline constexpr const char* kBuiltinTint = "openvegas.builtin.tint";
inline constexpr const char* kBuiltinBrightness = "openvegas.builtin.brightness";
inline constexpr const char* kBuiltinBlur = "openvegas.builtin.blur";
inline constexpr const char* kBuiltinColorWheels = "openvegas.builtin.color-wheels";
QVector<EffectSpec> timelineBuiltinSpecs();

// Applies the effect identified by pluginId to the image in place, honoring the
// given parameters (the parameter values use the same order as the EffectSpec's
// parameter list). Returns false for unknown/unsupported effect ids.
// `time` reaches native modules (NativeFrameTime); the built-in effects do
// not depend on it.
bool applyEffectToImage(QImage& image, const core::Identifier& pluginId, const QStringList& parameters,
                        const NativeFrameTime& time = {});

} // namespace plugin
} // namespace openvegas
