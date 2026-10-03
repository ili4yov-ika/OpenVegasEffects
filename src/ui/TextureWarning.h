#pragma once

#include "composition/Composition.h"
#include "media/MediaManager.h"
#include "plugin/EffectSpec.h"
#include "ui/PromptMessage.h"

#include <QCoreApplication>
#include <QSize>

namespace openvegas {
namespace ui {

// Pixel size a layer would supply as a texture: its first clip's media, a
// nested shot's frame, or the composition for planes and other generated
// content. Empty when nothing is known.
inline QSize layerTextureSize(const composition::Composition& composition,
                              const QString& layerId, const media::MediaManager* media)
{
    for (const composition::Layer& layer : composition.layers()) {
        if (layer.id.value() != layerId) continue;
        for (const composition::Clip& clip : layer.clips) {
            if (clip.nestedComposition) {
                return QSize(clip.nestedComposition->width(), clip.nestedComposition->height());
            }
            if (media) {
                const QSize size = media->assetById(clip.mediaId).frameSize();
                if (size.isValid() && !size.isEmpty()) return size;
            }
        }
        if (layer.kind == composition::LayerKind::Plane
            || layer.kind == composition::LayerKind::Text) {
            return QSize(composition.width(), composition.height());
        }
        return {};
    }
    return {};
}

// Reference particle texture check (VegasEffects FUN_1403e1850, gated by
// ShowTextureSizeWarning and shown as "Prompt me before using oversized
// particle textures"): picking a layer wider and taller than 1024 x 1024 as
// a particle texture warns that rendering may slow down.
inline void warnIfOversizedTexture(QWidget* parent, const plugin::EffectSpec& spec,
                                   const composition::Composition& composition,
                                   const QString& layerId, const media::MediaManager* media)
{
    if (!spec.category.startsWith(QLatin1String("Particles"))) return;
    const QSize size = layerTextureSize(composition, layerId, media);
    if (size.width() <= 1024 || size.height() <= 1024) return;
    showPrompt(parent, QStringLiteral("OversizedAssets"), QMessageBox::Warning,
               spec.displayName,
               QCoreApplication::translate(
                   "openvegas::ui::TimelineWidget",
                   "The width and height of chosen layer exceed 1024 x 1024. \n\n"
                   "This may cause rendering performance to degrade."),
               QMessageBox::Ok, QMessageBox::Ok);
}

} // namespace ui
} // namespace openvegas
