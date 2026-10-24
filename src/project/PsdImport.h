#pragma once

#include "composition/Composition.h"
#include "plugin/EffectSpec.h"

#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <memory>

namespace openvegas {
namespace media {
class MediaManager;
}
namespace project {

// Photoshop documents imported by layers, as a composite shot of the
// document's size: one layer per pixel, shape, type or smart object layer,
// with its name, position, opacity, visibility and blend mode, in the
// document's stacking order; adjustment layers become Grade layers with the
// matching effect; layer styles and smart filters (Camera Raw among them)
// become effects of the layer. What has no equivalent here is listed in the
// result's notes.

// The plug-in spec of an effect by its identifier ("com.FXHOME.HitFilm.
// DropShadow"); a spec without an id when that plug-in is not installed.
using EffectSpecLookup = std::function<plugin::EffectSpec(const QString& identifier)>;

struct PsdImportOptions
{
    int fpsNumerator = 30;
    int fpsDenominator = 1;
    double durationSeconds = 5.0;
    int audioSampleRate = 48000;
};

struct PsdImportResult
{
    std::shared_ptr<composition::Composition> shot;   // null when the file could not be read
    int layerCount = 0;
    QStringList notes;                                 // what could not be carried over, translated
    QString error;
};

// Reads `path` and builds its composite shot; the layers' media items are
// added to `media`.
PsdImportResult importPsdComposition(const QString& path, media::MediaManager& media,
                                     const EffectSpecLookup& specOf, const PsdImportOptions& options);

// The layer blend mode for a Photoshop blend mode key ('mul ', 'scrn', ...);
// `exact` says whether it composites the same.
QString psdBlendModeName(const QString& key, bool* exact = nullptr);
// The effects for a layer style ('lfx2'), in the order Photoshop draws them.
QVector<composition::Effect> psdLayerStyleEffects(const QVariantMap& style, int globalLightAngle,
                                                  const QSizeF& layerSize, const EffectSpecLookup& specOf,
                                                  const QString& layerName, QStringList* notes);
// The effects for a smart object's smart filters; `complete` is false when
// one of them has no equivalent (the layer then keeps its filtered pixels).
QVector<composition::Effect> psdSmartFilterEffects(const QVariantMap& smartObject, const EffectSpecLookup& specOf,
                                                   const QString& layerName, bool* complete, QStringList* notes);

} // namespace project
} // namespace openvegas
