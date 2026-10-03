#include "ui/MediaSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>

#include <cmath>

#include "composition/Composition.h"

namespace openvegas::ui {

namespace {

// The combos' items as the reference's .ui has them (context
// "MediaSettingsDialog"). The aspect list follows the PAR values (no custom
// one: MediaOverrideOptions keeps a PAR, not a number).
const char* const kAspects[] = {
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Square Pixels (1.0)"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "DV NTSC (0.91)"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "DV NTSC Wide (1.21)"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "DV PAL (1.09)"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "DV PAL Wide (1.46)"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "HD Anamorphic 1080 (1.33)"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "DVCPro HD (1.5)"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Anamorphic 2:1 (2.0)"),
};
const struct { const char* text; double rate; } kRates[] = {
    {QT_TRANSLATE_NOOP("MediaSettingsDialog", "23.976"), 24000.0 / 1001.0},
    {QT_TRANSLATE_NOOP("MediaSettingsDialog", "24"), 24.0},
    {QT_TRANSLATE_NOOP("MediaSettingsDialog", "25 (PAL)"), 25.0},
    {QT_TRANSLATE_NOOP("MediaSettingsDialog", "29.97 (NTSC)"), 30000.0 / 1001.0},
    {QT_TRANSLATE_NOOP("MediaSettingsDialog", "30"), 30.0},
    {QT_TRANSLATE_NOOP("MediaSettingsDialog", "50"), 50.0},
    {QT_TRANSLATE_NOOP("MediaSettingsDialog", "59.94"), 60000.0 / 1001.0},
    {QT_TRANSLATE_NOOP("MediaSettingsDialog", "60"), 60.0},
};
const char* const kAlpha[] = {
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Straight"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Premultiplied"),
};
const char* const kLevels[] = {
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Automatic"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Video (Studio)"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Video (Studio) with Super Whites & Blacks"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Computer (Full)"),
};
const char* const kSpaces[] = {
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Automatic"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Rec. 601"),
    QT_TRANSLATE_NOOP("MediaSettingsDialog", "Rec. 709"),
};

QString translated(const char* source)
{
    return QCoreApplication::translate("MediaSettingsDialog", source);
}

QLabel* valueLabel(const QString& value, QWidget* parent)
{
    auto* label = new QLabel(value, parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

// A combo of rates with `rate` selected - added as an item of its own when
// it is none of the usual ones.
void selectRate(QComboBox* combo, double rate)
{
    for (int i = 0; i < combo->count(); ++i) {
        if (std::abs(combo->itemData(i).toDouble() - rate) < 0.001) {
            combo->setCurrentIndex(i);
            return;
        }
    }
    if (rate <= 0.0) return;
    combo->addItem(QString::number(rate, 'g', 6), rate);
    combo->setCurrentIndex(combo->count() - 1);
}

// A combo with a "From File" box beside it: ticked, it shows what the file
// says and cannot be changed.
QHBoxLayout* fromFileRow(QComboBox* combo, QCheckBox* box)
{
    auto* row = new QHBoxLayout;
    row->addWidget(combo, 1);
    row->addWidget(box);
    return row;
}

} // namespace

MediaSettingsDialog::MediaSettingsDialog(const media::MediaAsset& asset, QWidget* parent)
    : QDialog(parent)
    , m_asset(asset)
{
    setObjectName(QStringLiteral("MediaSettingsDialog"));
    setWindowTitle(QCoreApplication::translate("biff::ui::media::MediaSettingsDialog",
                                               "Media Properties"));
    const bool audioOnly = asset.kind() == media::MediaKind::Audio;
    const bool still = asset.kind() == media::MediaKind::Image;
    const bool sequence = asset.isImageSequence();
    const bool decoded = asset.kind() == media::MediaKind::Video && !sequence;

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->addRow(QCoreApplication::translate("MediaSettingsDialog", "Name:"),
                 valueLabel(asset.fileName(), this));
    auto* pathRow = new QHBoxLayout;
    // A long path is shortened in the middle; the whole of it is the tooltip.
    auto* path = valueLabel(QString(), this);
    path->setObjectName(QStringLiteral("mediaSettingsPath"));
    path->setMinimumWidth(320);
    const auto showPath = [path](const QString& file) {
        const QString native = QDir::toNativeSeparators(file);
        path->setText(path->fontMetrics().elidedText(native, Qt::ElideMiddle, 320));
        path->setToolTip(native);
    };
    showPath(asset.sourcePath());
    pathRow->addWidget(path, 1);
    auto* relink = new QPushButton(QCoreApplication::translate("MediaSettingsDialog", "Relink"), this);
    relink->setObjectName(QStringLiteral("mediaSettingsRelink"));
    pathRow->addWidget(relink);
    form->addRow(QCoreApplication::translate("MediaSettingsDialog", "Path:"), pathRow);
    form->addRow(QCoreApplication::translate("MediaSettingsDialog", "Container:"),
                 valueLabel(QFileInfo(asset.sourcePath()).suffix().toUpper(), this));
    if (!still) {
        const QTime duration = QTime(0, 0).addMSecs(qRound64(asset.durationSeconds() * 1000.0));
        form->addRow(QCoreApplication::translate("MediaSettingsDialog", "Duration:"),
                     valueLabel(duration.toString(QStringLiteral("hh:mm:ss.zzz")), this));
    }
    layout->addLayout(form);

    if (!audioOnly) {
        auto* video = new QGroupBox(QCoreApplication::translate("MediaSettingsDialog", "Video"), this);
        auto* videoForm = new QFormLayout(video);
        const QSize size = asset.frameSize();
        videoForm->addRow(translated("Format:"), valueLabel(asset.fileStreams().videoFormat.isEmpty()
                           ? QStringLiteral("-") : asset.fileStreams().videoFormat, video));
        videoForm->addRow(translated("Codec:"), valueLabel(asset.fileStreams().videoCodec.isEmpty()
                           ? QStringLiteral("-") : asset.fileStreams().videoCodec, video));
        videoForm->addRow(QCoreApplication::translate("MediaSettingsDialog", "Resolution:"),
                          valueLabel(size.isValid() ? QStringLiteral("%1 x %2").arg(size.width()).arg(size.height())
                                                    : QStringLiteral("-"), video));

        // Frame rate: the file's, or the one its frames are played at.
        if (!still) {
            m_rate = new QComboBox(video);
            m_rate->setObjectName(QStringLiteral("mediaSettingsFrameRate"));
            for (const auto& entry : kRates) m_rate->addItem(translated(entry.text), entry.rate);
            m_rateFromFile = new QCheckBox(QCoreApplication::translate("MediaSettingsDialog", "From File"), video);
            m_rateFromFile->setObjectName(QStringLiteral("mediaSettingsFrameRateFromFile"));
            if (sequence) {
                // A sequence has no rate of its own to fall back on.
                m_rateFromFile->hide();
                selectRate(m_rate, asset.sequenceFrameRate());
            } else {
                m_rateFromFile->setChecked(!asset.overridesFrameRate());
                m_rateFromFile->setEnabled(asset.fileFrameRate() > 0.0);
                selectRate(m_rate, asset.overridesFrameRate() ? asset.frameRateOverride() : asset.fileFrameRate());
                const auto showFile = [this] {
                    if (m_rateFromFile->isChecked()) selectRate(m_rate, m_asset.fileFrameRate());
                    m_rate->setEnabled(!m_rateFromFile->isChecked());
                };
                showFile();
                connect(m_rateFromFile, &QCheckBox::toggled, this, showFile);
            }
            videoForm->addRow(QCoreApplication::translate("MediaSettingsDialog", "Frame Rate:"),
                              fromFileRow(m_rate, m_rateFromFile));
        }

        // Alpha: only where the file has an alpha channel.
        m_alpha = new QComboBox(video);
        m_alpha->setObjectName(QStringLiteral("mediaSettingsAlpha"));
        for (const char* text : kAlpha) m_alpha->addItem(translated(text));
        m_alphaFromFile = new QCheckBox(QCoreApplication::translate("MediaSettingsDialog", "From File"), video);
        m_alphaFromFile->setObjectName(QStringLiteral("mediaSettingsAlphaFromFile"));
        m_alphaFromFile->setChecked(!asset.overridesAlpha());
        m_alpha->setCurrentIndex(asset.overridesAlpha() ? asset.alphaOverride() : media::MediaAsset::StraightAlpha);
        const auto showAlpha = [this] {
            if (m_alphaFromFile->isChecked()) m_alpha->setCurrentIndex(media::MediaAsset::StraightAlpha);
            m_alpha->setEnabled(m_asset.fileHasAlpha() && !m_alphaFromFile->isChecked());
        };
        m_alphaFromFile->setEnabled(asset.fileHasAlpha());
        showAlpha();
        connect(m_alphaFromFile, &QCheckBox::toggled, this, showAlpha);
        videoForm->addRow(QCoreApplication::translate("MediaSettingsDialog", "Alpha:"),
                          fromFileRow(m_alpha, m_alphaFromFile));

        // Pixel aspect ratio.
        m_aspect = new QComboBox(video);
        m_aspect->setObjectName(QStringLiteral("mediaSettingsAspect"));
        for (const char* name : kAspects) m_aspect->addItem(translated(name));
        m_aspectFromFile = new QCheckBox(QCoreApplication::translate("MediaSettingsDialog", "From File"), video);
        m_aspectFromFile->setObjectName(QStringLiteral("mediaSettingsAspectFromFile"));
        const auto showAspect = [this] {
            const bool fromFile = m_aspectFromFile->isChecked();
            if (fromFile) m_aspect->setCurrentIndex(m_asset.filePixelAspectKind());
            m_aspect->setEnabled(!fromFile);
        };
        m_aspectFromFile->setChecked(!asset.overridesPixelAspect());
        m_aspect->setCurrentIndex(asset.overridesPixelAspect() ? asset.pixelAspectOverride()
                                                               : asset.filePixelAspectKind());
        showAspect();
        connect(m_aspectFromFile, &QCheckBox::toggled, this, showAspect);
        videoForm->addRow(QCoreApplication::translate("MediaSettingsDialog", "Aspect Ratio:"),
                          fromFileRow(m_aspect, m_aspectFromFile));

        // Levels and colour space of decoded video.
        if (decoded) {
            m_levels = new QComboBox(video);
            m_levels->setObjectName(QStringLiteral("mediaSettingsColorLevels"));
            for (const char* text : kLevels) m_levels->addItem(translated(text));
            m_levels->setCurrentIndex(asset.colorLevels());
            videoForm->addRow(QCoreApplication::translate("MediaSettingsDialog", "Color Levels:"), m_levels);
            m_space = new QComboBox(video);
            m_space->setObjectName(QStringLiteral("mediaSettingsColorSpace"));
            for (const char* text : kSpaces) m_space->addItem(translated(text));
            m_space->setCurrentIndex(asset.colorSpace());
            videoForm->addRow(QCoreApplication::translate("MediaSettingsDialog", "Color Space:"), m_space);
            m_hardware = new QCheckBox(QCoreApplication::translate("MediaSettingsDialog",
                                                                   "Use hardware decoding if available"), video);
            m_hardware->setObjectName(QStringLiteral("mediaSettingsHardwareDecoding"));
            m_hardware->setChecked(asset.hardwareDecoding());
            videoForm->addRow(m_hardware);
        }
        layout->addWidget(video);
    }

    if (!asset.fileStreams().audio.isEmpty() || audioOnly) {
        auto* audio = new QGroupBox(translated("Audio"), this);
        auto* audioForm = new QFormLayout(audio);
        m_audioStream = new QComboBox(audio);
        m_audioStream->setObjectName(QStringLiteral("mediaSettingsAudioStream"));
        m_audioStream->addItem(translated("Automatic"), -1);
        const auto& streams = asset.fileStreams().audio;
        for (int i = 0; i < streams.size(); ++i) {
            const auto& stream = streams.at(i);
            QString label = translated("Stream %1").arg(i + 1);
            if (!stream.name.isEmpty()) label += QStringLiteral(" — ") + stream.name;
            if (!stream.language.isEmpty()) label += QStringLiteral(" (%1)").arg(stream.language);
            m_audioStream->addItem(label, i);
        }
        int selected = m_audioStream->findData(asset.audioStreamIndex());
        if (selected < 0) {
            m_audioStream->addItem(translated("Unavailable stream %1").arg(asset.audioStreamIndex() + 1),
                                   asset.audioStreamIndex());
            selected = m_audioStream->count() - 1;
        }
        m_audioStream->setCurrentIndex(selected);
        m_audioStream->setEnabled(!streams.isEmpty());
        audioForm->addRow(translated("Audio Stream:"), m_audioStream);
        auto* format = valueLabel(QString(), audio);
        auto* codec = valueLabel(QString(), audio);
        auto* sampleRate = valueLabel(QString(), audio);
        sampleRate->setObjectName(QStringLiteral("mediaSettingsSampleRate"));
        auto* channels = valueLabel(QString(), audio);
        audioForm->addRow(translated("Format:"), format);
        audioForm->addRow(translated("Codec:"), codec);
        audioForm->addRow(translated("Sample Rate:"), sampleRate);
        audioForm->addRow(translated("Channels:"), channels);
        const auto showStream = [this, format, codec, sampleRate, channels] {
            const auto& available = m_asset.fileStreams().audio;
            int index = m_audioStream->currentData().toInt();
            if (index < 0 && available.size() == 1) index = 0;
            // Automatic on a multi-stream file is the decoder's preference;
            // do not present another track's rate/codec as if it were known.
            const auto* stream = index >= 0 && index < available.size() ? &available.at(index) : nullptr;
            const QString unknown = QStringLiteral("-");
            format->setText(stream && !stream->format.isEmpty() ? stream->format : unknown);
            codec->setText(stream && !stream->codec.isEmpty() ? stream->codec : unknown);
            sampleRate->setText(stream && stream->sampleRate
                               ? translated("%1 Hz").arg(stream->sampleRate) : unknown);
            channels->setText(stream && stream->channels ? QString::number(stream->channels) : unknown);
        };
        showStream();
        connect(m_audioStream, &QComboBox::currentIndexChanged, this, showStream);
        layout->addWidget(audio);
    }

    connect(relink, &QPushButton::clicked, this, [this, showPath] {
        const QString chosen = QFileDialog::getOpenFileName(
            this, QCoreApplication::translate("MediaSettingsDialog", "Relink Media"),
            QFileInfo(m_asset.sourcePath()).absolutePath());
        if (chosen.isEmpty()) return;
        m_relinkPath = chosen;
        showPath(chosen);
    });

    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(QCoreApplication::translate("MediaSettingsDialog", "OK"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QCoreApplication::translate("MediaSettingsDialog", "Cancel"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

media::MediaAsset MediaSettingsDialog::result() const
{
    media::MediaAsset asset = m_asset;
    if (m_audioStream) asset.setAudioStreamIndex(m_audioStream->currentData().toInt());
    if (m_rate) {
        const double rate = m_rate->currentData().toDouble();
        if (asset.isImageSequence()) asset.setSequenceFrameRate(rate);
        else asset.setFrameRateOverride(!m_rateFromFile->isChecked(), rate);
    }
    if (m_alpha && m_asset.fileHasAlpha())
        asset.setAlphaOverride(!m_alphaFromFile->isChecked(), m_alpha->currentIndex());
    if (m_aspect) asset.setPixelAspectOverride(overridesPixelAspect(), pixelAspect());
    if (m_levels) asset.setColorLevels(m_levels->currentIndex());
    if (m_space) asset.setColorSpace(m_space->currentIndex());
    if (m_hardware) asset.setHardwareDecoding(m_hardware->isChecked());
    return asset;
}

bool MediaSettingsDialog::overridesPixelAspect() const
{
    return m_aspectFromFile ? !m_aspectFromFile->isChecked() : m_asset.overridesPixelAspect();
}

int MediaSettingsDialog::pixelAspect() const
{
    return m_aspect ? qBound(0, m_aspect->currentIndex(), int(composition::Composition::CustomAspect) - 1)
                    : m_asset.pixelAspectOverride();
}

} // namespace openvegas::ui
