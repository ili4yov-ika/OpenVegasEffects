#include "composition/MotionTracker.h"

#include <QtGlobal>
#include <cmath>
#include <limits>

namespace openvegas::composition {
namespace {

QImage gray8(const QImage& image)
{
    return image.format() == QImage::Format_Grayscale8
        ? image : image.convertToFormat(QImage::Format_Grayscale8);
}

bool patchFits(const QImage& image, int x, int y, int radius)
{
    return x - radius >= 0 && y - radius >= 0
        && x + radius < image.width() && y + radius < image.height();
}

double patchScore(const QImage& a, int ax, int ay, const QImage& b, int bx, int by, int radius)
{
    const int side = radius * 2 + 1;
    const int pixels = side * side;
    int sumA = 0;
    int sumB = 0;
    for (int y = -radius; y <= radius; ++y) {
        const uchar* rowA = a.constScanLine(ay + y);
        const uchar* rowB = b.constScanLine(by + y);
        for (int x = -radius; x <= radius; ++x) {
            sumA += rowA[ax + x];
            sumB += rowB[bx + x];
        }
    }
    const double meanA = double(sumA) / pixels;
    const double meanB = double(sumB) / pixels;
    double score = 0.0;
    for (int y = -radius; y <= radius; ++y) {
        const uchar* rowA = a.constScanLine(ay + y);
        const uchar* rowB = b.constScanLine(by + y);
        for (int x = -radius; x <= radius; ++x)
            score += std::abs((rowA[ax + x] - meanA) - (rowB[bx + x] - meanB));
    }
    return score;
}

} // namespace

QPointF MotionTracker::trackStep(const QImage& previousFrame, const QImage& currentFrame,
                                 const QPointF& point, int sampleRadius, int searchRadius,
                                 double* confidence)
{
    if (confidence) *confidence = 0.0;
    if (previousFrame.isNull() || currentFrame.isNull()
        || previousFrame.size() != currentFrame.size()) return point;

    const QImage previous = gray8(previousFrame);
    const QImage current = gray8(currentFrame);
    sampleRadius = qBound(2, sampleRadius, 64);
    searchRadius = qBound(sampleRadius, searchRadius, 256);
    const int px = qRound(point.x());
    const int py = qRound(point.y());
    if (!patchFits(previous, px, py, sampleRadius)) return point;

    double best = std::numeric_limits<double>::max();
    QPoint bestPoint(px, py);
    for (int y = py - searchRadius; y <= py + searchRadius; ++y) {
        for (int x = px - searchRadius; x <= px + searchRadius; ++x) {
            if (!patchFits(current, x, y, sampleRadius)) continue;
            const double score = patchScore(previous, px, py, current, x, y, sampleRadius);
            // Prefer the closest candidate when two flat patches tie.
            const double distancePenalty = 1e-6 * ((x - px) * (x - px) + (y - py) * (y - py));
            if (score + distancePenalty < best) {
                best = score + distancePenalty;
                bestPoint = QPoint(x, y);
            }
        }
    }
    if (confidence) {
        const int pixels = (sampleRadius * 2 + 1) * (sampleRadius * 2 + 1);
        *confidence = qBound(0.0, 1.0 - best / (255.0 * pixels), 1.0);
    }
    return bestPoint;
}

QVector<QPointF> MotionTracker::track(const QVector<QImage>& frames, const QPointF& firstPoint,
                                      int sampleRadius, int searchRadius)
{
    QVector<QPointF> points;
    if (frames.isEmpty()) return points;
    points.reserve(frames.size());
    points.append(firstPoint);
    for (int i = 1; i < frames.size(); ++i)
        points.append(trackStep(frames[i - 1], frames[i], points.last(), sampleRadius, searchRadius));
    return points;
}

} // namespace openvegas::composition
