#pragma once

#include <QMap>
#include <QPointF>
#include <QVariant>
#include <QVector>

namespace openvegas {
namespace composition {

// Animation model, ported from the reference (biff::project::KeyFrame and
// KeyFrameList in Project.dll; see MARKDOWN/RE_VegasEffects.md).
//
// Points kept from the reference design:
//   * time is an integer frame number, not seconds;
//   * a keyframe carries an incoming and an outgoing bezier handle plus
//     separate influence factors, and the handles can be locked together;
//   * a property that cannot be interpolated (bool, enum, string) holds its
//     value until the next keyframe instead of blending;
//   * keyframes are addressable by a stable id, so moving one in time does not
//     invalidate references to it.
//
// Not ported: the reference's second, spatial (3D motion path) interpolation
// with arc-length parameterisation. Only the temporal curve is implemented.

// Temporal interpolation. The reference offers six types, exposed as the
// toolButtonKeyFrameType* group and set by its "Set Keyframe Temporal Type"
// command. Menu labels are "Constant", "Linear", "Smooth", "Smooth In",
// "Smooth Out" and "Manual Bezier"; the icons name the eased ones after their
// After Effects equivalents (key-frame-easy-ease, -ease-in, -ease-out).
enum class TemporalType
{
    Linear = 0,    // straight line between values
    Hold,          // "Constant": keep the value until the next keyframe
    EasyEase,      // "Smooth": eased both leaving and arriving
    EaseIn,        // "Smooth In": eased on the side arriving at this keyframe
    EaseOut,       // "Smooth Out": eased on the side leaving this keyframe
    ManualBezier,  // handles positioned by hand, see below
};

// Menu label the reference uses for a type.
const char* temporalTypeName(TemporalType type);

struct KeyFrame
{
    int id = 0;                 // stable identity, survives a move in time
    int frame = 0;              // reference stores time as a frame number
    QVariant value;
    TemporalType temporal = TemporalType::Linear;

    // Bezier handles, in (frames, value-fraction) space, as the reference's
    // temporal incoming/outgoing control points.
    QPointF incomingHandle;
    QPointF outgoingHandle;
    double incomingInfluence = 0.0;
    double outgoingInfluence = 0.0;
    bool handlesLocked = false;

    bool isHold() const { return temporal == TemporalType::Hold; }
};

// A segment is shaped by the outgoing handle of the keyframe it leaves and the
// incoming handle of the one it reaches, so each type resolves to one control
// point on each side. ManualBezier returns the handles stored on the keyframe;
// the rest return the preset that type stands for.
QPointF outgoingHandleFor(const KeyFrame& keyFrame);
QPointF incomingHandleFor(const KeyFrame& keyFrame);

// Ordered by frame, exactly like the reference's std::map<int, KeyFrame>.
class KeyFrameList
{
public:
    KeyFrameList() = default;
    explicit KeyFrameList(const QVariant& defaultValue)
        : m_defaultValue(defaultValue)
    {
    }

    bool isEmpty() const { return m_frames.isEmpty(); }
    int count() const { return static_cast<int>(m_frames.size()); }

    const QVariant& defaultValue() const { return m_defaultValue; }
    void setDefaultValue(const QVariant& value) { m_defaultValue = value; }

    // Reference: CanTemporallyInterpolate - false for discrete properties, and
    // the serialised .vegfx marks it per property as CanInterpT.
    bool canInterpolate() const { return m_canInterpolate; }
    void setCanInterpolate(bool on) { m_canInterpolate = on; }

    // Insert or replace the keyframe at `frame`.
    int set(int frame, const QVariant& value,
            TemporalType temporal = TemporalType::Linear);
    void add(const KeyFrame& keyFrame);

    bool removeAt(int frame);
    bool removeById(int id);
    // Reference: Move(FXID, newTime) - identity is preserved.
    bool moveById(int id, int newFrame);

    bool contains(int frame) const { return m_frames.contains(frame); }
    const KeyFrame* at(int frame) const;
    const KeyFrame* byId(int id) const;
    // Mutable access, for an editor that reshapes a key in place - the value
    // graph drags a node's value and its bezier handles, and going through
    // set() instead would leave the handles behind and mint a new identity.
    KeyFrame* keyAt(int frame);
    KeyFrame* keyById(int id);

    QVector<int> locations() const;
    QVector<KeyFrame> all() const;

    const KeyFrame* firstByTime() const;
    const KeyFrame* lastByTime() const;
    const KeyFrame* nearestToTime(int frame) const;

    // Reference: AdjacentKeyFrames(time, prev, next). Either may be null.
    void adjacentKeyFrames(int frame, const KeyFrame** previous,
                           const KeyFrame** next) const;

    // Value of the animated property at `frame`.
    QVariant valueAt(int frame) const;

    void clear();

private:
    QMap<int, KeyFrame> m_frames;
    QVariant m_defaultValue;
    bool m_canInterpolate = true;
    int m_nextId = 1;
};

} // namespace composition
} // namespace openvegas
