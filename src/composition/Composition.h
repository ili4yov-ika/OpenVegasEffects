#pragma once

#include "composition/EditorSequence.h"
#include "composition/Layer.h"

#include <QString>
#include <QUuid>
#include <QVector>

namespace openvegas {
namespace composition {

class Composition
{
public:
    Composition() = default;

    const core::Identifier& id() const { return m_id; }
    void setId(const core::Identifier& id) { if (id.isValid()) m_id = id; }

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    int width() const { return m_width; }
    int height() const { return m_height; }
    void setSize(int width, int height)
    {
        m_width = width;
        m_height = height;
    }

    int fpsNumerator() const { return m_fpsNumerator; }
    int fpsDenominator() const { return m_fpsDenominator; }
    void setFrameRate(int numerator, int denominator)
    {
        m_fpsNumerator = numerator;
        m_fpsDenominator = denominator;
    }

    double durationSeconds() const { return m_durationSeconds; }
    void setDurationSeconds(double seconds) { m_durationSeconds = seconds; }

    const QVector<Layer>& layers() const { return m_layers; }
    Layer& addLayer(const QString& name);
    // Puts a whole layer back at a given position. Undo needs this: removing a
    // layer and re-appending it would move it to the bottom of the stack.
    bool insertLayer(int index, const Layer& layer);
    bool removeLayer(int index);
    // Copy of the layer at index, for commands that have to restore it.
    Layer layerCopy(int index) const;
    void setLayers(const QVector<Layer>& layers);
    bool swapLayers(int a, int b);
    Layer& layerRef(int index);

    Clip* addClip(const QString& layerName, const core::Identifier& mediaId, double startSeconds,
                  double durationSeconds);
    Layer* layer(const QString& name);
    Clip* clipAt(int layerIndex, int clipIndex);

    EditorSequence& editorSequence() { return m_editorSequence; }
    const EditorSequence& editorSequence() const { return m_editorSequence; }

    void clear();
    bool isEmpty() const { return m_layers.isEmpty(); }

private:
    core::Identifier m_id = core::Identifier(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString m_name = QStringLiteral("Untitled");
    int m_width = 1920;
    int m_height = 1080;
    int m_fpsNumerator = 30;
    int m_fpsDenominator = 1;
    double m_durationSeconds = 10.0;
    QVector<Layer> m_layers;
    EditorSequence m_editorSequence;
};

} // namespace composition
} // namespace openvegas
