#pragma once

#include <QWidget>

#include <QString>

#include "media/MediaManager.h"

class QListWidget;
class QSlider;
class QSpinBox;
class QGraphicsView;
class QDragEnterEvent;
class QDropEvent;
class QLabel;

namespace openvegas {
namespace ui {

// Inner scrub/timeline strip for the Trimmer panel. Draws the selected media
// clip as a strip with trimmer in/out markers and a scrub playhead, mirroring
// the reference TrimmerWidget (object names "trimmer-frame" / "trimmer-in-out").
class TrimmerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrimmerWidget(QWidget* parent = nullptr);

    void setAsset(const media::MediaAsset& asset);
    void clearAsset();

    media::MediaAsset asset() const { return m_asset; }
    bool hasAsset() const { return m_asset.isValid(); }

    int trimInPoint() const { return m_trimIn; }
    int trimOutPoint() const { return m_trimOut; }
    int frameCount() const { return m_frameCount; }
    double frameRate() const { return m_frameRate; }
    void setFrameRate(double framesPerSecond);
    void setTrimInPoint(int frames);
    void setTrimOutPoint(int frames);

    double positionSeconds() const { return m_position; }
    void setPositionSeconds(double seconds);

signals:
    void trimInChanged(int frame);
    void trimOutChanged(int frame);
    void positionChanged(double seconds);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class DragMode { None, Playhead, TrimIn, TrimOut };

    double secondsFromX(int x) const;
    int xFromSeconds(double seconds) const;
    int frameFromX(int x) const;
    void updateDragPosition(int x);

    media::MediaAsset m_asset;
    int m_trimIn = 0;
    int m_trimOut = 0;
    int m_frameCount = 1;
    double m_frameRate = 30.0;
    double m_position = 0.0;
    DragMode m_dragMode = DragMode::None;
};

// Mirrors the reference "Trimmer" panel (type 2050): shows the selected media
// clip for scrubbing and setting trimmer in/out points, with Insert/Overlay
// into the sequence (reference shortcuts B / N).
class TrimmerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TrimmerPanel(QWidget* parent = nullptr);

    void setMediaManager(media::MediaManager* manager);
    void setFrameRate(double framesPerSecond);
    void openAsset(const media::MediaAsset& asset);

    TrimmerWidget* trimmerWidget() const { return m_widget; }

    int trimInPoint() const { return m_widget->trimInPoint(); }
    int trimOutPoint() const { return m_widget->trimOutPoint(); }

public slots:
    void setTrimInPoint();
    void setTrimOutPoint();

signals:
    void insertRequested(const QString& filePath, int trimInFrame, int trimOutFrame);
    void overlayRequested(const QString& filePath, int trimInFrame, int trimOutFrame);
    void trimRangeChanged(const QString& filePath, int trimInFrame, int trimOutFrame);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void pickAsset(const QString& filePath);
    void persistTrimRange();
    void updateReadouts();
    QString timecode(double seconds) const;

    media::MediaManager* m_manager = nullptr;
    TrimmerWidget* m_widget = nullptr;
    QListWidget* m_assetList = nullptr;
    QSlider* m_scrub = nullptr;
    QLabel* m_positionLabel = nullptr;
    QLabel* m_rangeLabel = nullptr;
};

} // namespace ui
} // namespace openvegas
