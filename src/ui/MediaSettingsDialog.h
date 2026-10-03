#pragma once

#include <QDialog>
#include <QString>

#include "media/MediaAsset.h"

class QCheckBox;
class QComboBox;

namespace openvegas::ui {

// Media > Properties: the reference's biff::ui::media::MediaSettingsDialog
// ("Media Properties", retranslated by FUN_14073de70) for a video, an image
// sequence or a still - what the file is, and the overrides the project keeps
// for it (MediaOverrideOptions): frame rate and alpha with "From File", the
// pixel aspect ratio, colour levels and colour space. A sequence's frame rate
// is its own (no "From File"); a still has only its alpha and pixel aspect.
// Alpha is offered for media whose file carries an alpha channel; levels,
// colour space and "Use hardware decoding if available" for decoded video.
// Audio streams show their codecs/rates/channels and can be selected for playback.
class MediaSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MediaSettingsDialog(const media::MediaAsset& asset, QWidget* parent = nullptr);

    // The asset with the dialog's settings applied.
    media::MediaAsset result() const;
    // Kept for callers that only want the pixel aspect.
    bool overridesPixelAspect() const;
    int pixelAspect() const;
    // The file Relink chose; empty when it was not used.
    QString relinkPath() const { return m_relinkPath; }

private:
    media::MediaAsset m_asset;
    QComboBox* m_audioStream = nullptr;
    QComboBox* m_rate = nullptr;
    QCheckBox* m_rateFromFile = nullptr;
    QComboBox* m_alpha = nullptr;
    QCheckBox* m_alphaFromFile = nullptr;
    QComboBox* m_aspect = nullptr;
    QCheckBox* m_aspectFromFile = nullptr;
    QComboBox* m_levels = nullptr;
    QComboBox* m_space = nullptr;
    QCheckBox* m_hardware = nullptr;
    QString m_relinkPath;
};

} // namespace openvegas::ui
