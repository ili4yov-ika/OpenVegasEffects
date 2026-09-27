#pragma once

#include <QImage>
#include <QPointF>
#include <QVector>

namespace openvegas::composition {

class MotionTracker
{
public:
    // Locates the patch around `point` from previousFrame inside the search
    // window in currentFrame. Images may have any QImage pixel format.
    static QPointF trackStep(const QImage& previousFrame, const QImage& currentFrame,
                             const QPointF& point, int sampleRadius = 8,
                             int searchRadius = 24, double* confidence = nullptr);

    static QVector<QPointF> track(const QVector<QImage>& frames, const QPointF& firstPoint,
                                  int sampleRadius = 8, int searchRadius = 24);
};

} // namespace openvegas::composition
