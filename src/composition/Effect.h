#pragma once

#include "composition/KeyFrame.h"
#include "core/Identifier.h"

#include <QMap>
#include <QString>
#include <QStringList>

namespace openvegas {
namespace composition {

struct Effect
{
    core::Identifier pluginId;
    QString name;
    QStringList parameterValues;
    bool enabled = true;

    // Animation per parameter index. A parameter with no entry is static and
    // reads from parameterValues; one with an entry is driven by its curve.
    // The reference keeps the same split: a property holds a default value and
    // an optional KeyFrameList (see MARKDOWN/RE_VegasEffects.md).
    QMap<int, KeyFrameList> animation;

    bool isAnimated(int parameterIndex) const
    {
        const auto it = animation.constFind(parameterIndex);
        return it != animation.constEnd() && !it->isEmpty();
    }

    // Static value, or the animated one when the parameter has keyframes.
    QVariant parameterAt(int parameterIndex, int frame) const
    {
        const auto it = animation.constFind(parameterIndex);
        if (it != animation.constEnd() && !it->isEmpty()) {
            return it->valueAt(frame);
        }
        return (parameterIndex >= 0 && parameterIndex < parameterValues.size())
                   ? QVariant(parameterValues.at(parameterIndex))
                   : QVariant();
    }
};

} // namespace composition
} // namespace openvegas