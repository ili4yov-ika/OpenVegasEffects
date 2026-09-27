#include "composition/KeyFrame.h"

#include <QtGlobal>

#include <cmath>

namespace openvegas {
namespace composition {

namespace {

// Numeric blend for the types our properties actually animate. Anything the
// reference would keep in its variant but we cannot blend falls back to the
// previous value, which is what a non-interpolatable property does anyway.
bool numericValue(const QVariant& v, double& out)
{
    switch (v.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Float:
    case QMetaType::Double:
        out = v.toDouble();
        return true;
    default:
        return false;
    }
}

// Cubic bezier through (0,0) and (1,1) with the two handles, solved for y at a
// given x. The reference does the same thing in
// KeyFrameList::ParamTforTemporalCurveTime: find the curve parameter that
// matches the time, then read the value off the curve.
double bezierEase(double x, const QPointF& out, const QPointF& in)
{
    const double x1 = qBound(0.0, out.x(), 1.0);
    const double y1 = out.y();
    const double x2 = qBound(0.0, in.x(), 1.0);
    const double y2 = in.y();

    auto curveX = [&](double t) {
        const double u = 1.0 - t;
        return 3.0 * u * u * t * x1 + 3.0 * u * t * t * x2 + t * t * t;
    };
    auto curveY = [&](double t) {
        const double u = 1.0 - t;
        return 3.0 * u * u * t * y1 + 3.0 * u * t * t * y2 + t * t * t;
    };

    // Bisection: monotone in x for handles inside [0,1], and cheap enough that
    // the reference's per-pair curve cache is not needed yet.
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 24; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (curveX(mid) < x) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return curveY(0.5 * (lo + hi));
}

} // namespace

const char* temporalTypeName(TemporalType type)
{
    switch (type) {
    case TemporalType::Linear:       return "Linear";
    case TemporalType::Hold:         return "Constant";
    case TemporalType::EasyEase:     return "Smooth";
    case TemporalType::EaseIn:       return "Smooth In";
    case TemporalType::EaseOut:      return "Smooth Out";
    case TemporalType::ManualBezier: return "Manual Bezier";
    }
    return "Linear";
}

namespace {
// Control points on the unit curve from (0,0) to (1,1). A handle sitting on the
// diagonal leaves the segment straight; pulling its y flat against the endpoint
// is what makes the value ease in or out of the keyframe.
constexpr double kHandleX = 1.0 / 3.0;
const QPointF kOutLinear(kHandleX, kHandleX);
const QPointF kOutEased(kHandleX, 0.0);
const QPointF kInLinear(1.0 - kHandleX, 1.0 - kHandleX);
const QPointF kInEased(1.0 - kHandleX, 1.0);
} // namespace

QPointF outgoingHandleFor(const KeyFrame& keyFrame)
{
    switch (keyFrame.temporal) {
    case TemporalType::ManualBezier:
        return keyFrame.outgoingHandle;
    case TemporalType::EasyEase:
    case TemporalType::EaseOut:
        return kOutEased;
    default:
        return kOutLinear;
    }
}

QPointF incomingHandleFor(const KeyFrame& keyFrame)
{
    switch (keyFrame.temporal) {
    case TemporalType::ManualBezier:
        return keyFrame.incomingHandle;
    case TemporalType::EasyEase:
    case TemporalType::EaseIn:
        return kInEased;
    default:
        return kInLinear;
    }
}

int KeyFrameList::set(int frame, const QVariant& value, TemporalType temporal)
{
    auto it = m_frames.find(frame);
    if (it != m_frames.end()) {
        it->value = value;
        it->temporal = temporal;
        return it->id;
    }
    KeyFrame kf;
    kf.id = m_nextId++;
    kf.frame = frame;
    kf.value = value;
    kf.temporal = temporal;
    m_frames.insert(frame, kf);
    return kf.id;
}

void KeyFrameList::add(const KeyFrame& keyFrame)
{
    KeyFrame kf = keyFrame;
    if (kf.id <= 0) {
        kf.id = m_nextId++;
    } else {
        m_nextId = qMax(m_nextId, kf.id + 1);
    }
    m_frames.insert(kf.frame, kf);
}

bool KeyFrameList::removeAt(int frame)
{
    return m_frames.remove(frame) > 0;
}

bool KeyFrameList::removeById(int id)
{
    for (auto it = m_frames.begin(); it != m_frames.end(); ++it) {
        if (it->id == id) {
            m_frames.erase(it);
            return true;
        }
    }
    return false;
}

bool KeyFrameList::moveById(int id, int newFrame)
{
    for (auto it = m_frames.begin(); it != m_frames.end(); ++it) {
        if (it->id != id) {
            continue;
        }
        if (it.key() == newFrame) {
            return true;
        }
        KeyFrame kf = it.value();
        kf.frame = newFrame;
        m_frames.erase(it);
        m_frames.insert(newFrame, kf);
        return true;
    }
    return false;
}

const KeyFrame* KeyFrameList::at(int frame) const
{
    const auto it = m_frames.constFind(frame);
    return it == m_frames.constEnd() ? nullptr : &it.value();
}

const KeyFrame* KeyFrameList::byId(int id) const
{
    for (auto it = m_frames.constBegin(); it != m_frames.constEnd(); ++it) {
        if (it->id == id) {
            return &it.value();
        }
    }
    return nullptr;
}

KeyFrame* KeyFrameList::keyAt(int frame)
{
    const auto it = m_frames.find(frame);
    return it == m_frames.end() ? nullptr : &it.value();
}

KeyFrame* KeyFrameList::keyById(int id)
{
    for (auto it = m_frames.begin(); it != m_frames.end(); ++it) {
        if (it.value().id == id) {
            return &it.value();
        }
    }
    return nullptr;
}

QVector<int> KeyFrameList::locations() const

{
    QVector<int> out;
    out.reserve(m_frames.size());
    for (auto it = m_frames.constBegin(); it != m_frames.constEnd(); ++it) {
        out.append(it.key());
    }
    return out;
}

QVector<KeyFrame> KeyFrameList::all() const
{
    QVector<KeyFrame> out;
    out.reserve(m_frames.size());
    for (auto it = m_frames.constBegin(); it != m_frames.constEnd(); ++it) {
        out.append(it.value());
    }
    return out;
}

const KeyFrame* KeyFrameList::firstByTime() const
{
    return m_frames.isEmpty() ? nullptr : &m_frames.constBegin().value();
}

const KeyFrame* KeyFrameList::lastByTime() const
{
    if (m_frames.isEmpty()) {
        return nullptr;
    }
    auto it = m_frames.constEnd();
    --it;
    return &it.value();
}

const KeyFrame* KeyFrameList::nearestToTime(int frame) const
{
    const KeyFrame* previous = nullptr;
    const KeyFrame* next = nullptr;
    adjacentKeyFrames(frame, &previous, &next);
    if (!previous) {
        return next;
    }
    if (!next) {
        return previous;
    }
    return (frame - previous->frame) <= (next->frame - frame) ? previous : next;
}

void KeyFrameList::adjacentKeyFrames(int frame, const KeyFrame** previous,
                                     const KeyFrame** next) const
{
    if (previous) {
        *previous = nullptr;
    }
    if (next) {
        *next = nullptr;
    }
    for (auto it = m_frames.constBegin(); it != m_frames.constEnd(); ++it) {
        if (it.key() <= frame) {
            if (previous) {
                *previous = &it.value();
            }
        } else {
            if (next) {
                *next = &it.value();
            }
            break;
        }
    }
}

QVariant KeyFrameList::valueAt(int frame) const
{
    if (m_frames.isEmpty()) {
        return m_defaultValue;
    }

    const KeyFrame* previous = nullptr;
    const KeyFrame* next = nullptr;
    adjacentKeyFrames(frame, &previous, &next);

    if (!previous) {
        return next->value;          // before the first key: hold it
    }
    if (!next) {
        return previous->value;      // after the last key: hold it
    }
    if (!m_canInterpolate || previous->isHold()) {
        return previous->value;
    }

    double a = 0.0;
    double b = 0.0;
    if (!numericValue(previous->value, a) || !numericValue(next->value, b)) {
        return previous->value;      // nothing sensible to blend
    }

    const int span = next->frame - previous->frame;
    if (span <= 0) {
        return previous->value;
    }
    double t = static_cast<double>(frame - previous->frame) / span;
    // Either end can shape the segment: "Smooth Out" on the keyframe being left
    // and "Smooth In" on the one being reached ease their own side of it, so the
    // straight-line shortcut only applies when both ends are Linear.
    if (previous->temporal != TemporalType::Linear || next->temporal != TemporalType::Linear) {
        t = bezierEase(t, outgoingHandleFor(*previous), incomingHandleFor(*next));
    }
    return QVariant(a + (b - a) * t);
}

void KeyFrameList::clear()
{
    m_frames.clear();
    m_nextId = 1;
}

} // namespace composition
} // namespace openvegas
