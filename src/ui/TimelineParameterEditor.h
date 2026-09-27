#include "ui/Theme.h"
#pragma once
#include "plugin/EffectSpec.h"
#include "ui/TextPropertyWidgets.h"
#include "ui/ScreenColorPicker.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>

namespace openvegas::ui {
// Editors have an explicit refresh path: playhead changes update values without
// destroying the focused field, popup or an in-progress scrub.
inline QWidget* timelineParameterEditor(QWidget* parent, const plugin::EffectParameterSpec& spec,
    const QString& name, std::function<QVariant()> read, std::function<void(QVariant)> write,
    QVector<std::function<void()>>& readers)
{
    auto* root = new QWidget(parent);
    root->setObjectName(name);
    root->setMinimumWidth(30);
    auto* layout = new QHBoxLayout(root);
    layout->setContentsMargins(0, 0, 2, 0);
    layout->setSpacing(3);
    const QString type = spec.type.toLower();
    const auto refresh = [&readers](QWidget* widget, std::function<void()> action) {
        QPointer<QWidget> guard(widget);
        readers.append([guard, action] {
            if (!guard || guard->hasFocus() || guard->isAncestorOf(QApplication::focusWidget())) return;
            const QSignalBlocker block(guard);
            action();
        });
        action();
    };
    if (!spec.choices.isEmpty() || type == "layer") {
        auto* combo = new QComboBox(root);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(1);
        combo->addItems(spec.choices);
        if (combo->findText(read().toString()) < 0) combo->addItem(read().toString());
        layout->addWidget(combo);
        refresh(combo, [combo, read] { combo->setCurrentText(read().toString()); });
        QObject::connect(combo, &QComboBox::currentTextChanged, root, [write](const QString& text) { write(text); });
    } else if (type == "label") {
        auto* label = new QLabel(root);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(label, 1);
        refresh(label, [label, read] { label->setText(read().toString()); });
    } else if (type == "button") {
        auto* button = new QToolButton(root);
        button->setText(spec.displayName);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        layout->addWidget(button, 1);
        QObject::connect(button, &QToolButton::clicked, root, [root, write] {
            // Tannen models PluginUIButton as a transient bool property. Keep
            // it true long enough for the queued preview render to consume the
            // action, then return it to its idle value.
            write(QStringLiteral("true"));
            QTimer::singleShot(200, root,
                               [write] { write(QStringLiteral("false")); });
        });
    } else if (type == "bool" || type == "checkbox") {
        auto* check = new QCheckBox(root);
        layout->addStretch(); layout->addWidget(check);
        refresh(check, [check, read] { check->setChecked(read().toString() == "1" || read().toString() == "true"); });
        QObject::connect(check, &QCheckBox::toggled, root, [write](bool on) { write(on ? "true" : "false"); });
    } else if (type == "int" || type == "double" || type == "float" || type == "angle") {
        auto* spin = new TextScrubber<QDoubleSpinBox>(root);
        spin->setRange(spec.minimum, spec.maximum);
        spin->setDecimals(type == "int" ? 0 : spec.decimals);
        spin->setSingleStep(spec.step);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setAlignment(Qt::AlignRight);
        if (!spec.unit.isEmpty()) spin->setSuffix(" " + spec.unit);
        spin->setMinimumWidth(25);
        layout->addWidget(spin);
        refresh(spin, [spin, read] { spin->setValue(read().toDouble()); });
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, root, [write, type](double value) {
            write(type == "int" ? QVariant(qRound(value)) : QVariant(value));
        });
    } else if (type == "point2d" || type == "point3d" || type == "orientation") {
        const int componentCount = type == "point2d" ? 2 : 3;
        const QStringList axisNames = type == "orientation"
                                          ? QStringList{QStringLiteral("X°"), QStringLiteral("Y°"), QStringLiteral("Z°")}
                                          : QStringList{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
        const auto components = [read, componentCount] {
            const QStringList parts = read().toString().split(QLatin1Char(','));
            QVector<double> values(componentCount, 0.0);
            for (int i = 0; i < componentCount && i < parts.size(); ++i) {
                bool ok = false;
                const double value = parts.at(i).trimmed().toDouble(&ok);
                if (ok) values[i] = value;
            }
            return values;
        };
        for (int axis = 0; axis < componentCount; ++axis) {
            auto* spin = new TextScrubber<QDoubleSpinBox>(root);
            spin->setRange(spec.minimum, spec.maximum);
            spin->setDecimals(spec.decimals);
            spin->setSingleStep(spec.step);
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            spin->setAlignment(Qt::AlignRight);
            spin->setPrefix(axisNames.at(axis) + QStringLiteral(":"));
            spin->setMinimumWidth(38);
            layout->addWidget(spin, 1);
            refresh(spin, [spin, components, axis] { spin->setValue(components().at(axis)); });
            QObject::connect(spin, &QDoubleSpinBox::valueChanged, root,
                             [write, components, axis](double value) {
                QVector<double> values = components();
                values[axis] = value;
                QStringList encoded;
                for (double component : values) encoded.append(QString::number(component, 'g', 12));
                write(encoded.join(QLatin1Char(',')));
            });
        }
    } else if (type == "color") {
        auto* swatch = new QToolButton(root);
        swatch->setFixedSize(16, 16);
        swatch->setObjectName(name + ".swatch");
        swatch->setToolTip(QObject::tr("Choose color"));
        const auto updateColor = [swatch, read] {
            QColor color(read().toString());
            swatch->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid #888; }")
                .arg(color.isValid() ? color.name() : "#ffffff"));
        };
        for (int axis = 0; axis < 3; ++axis) {
            auto* channel = new TextScrubber<QSpinBox>(root);
            channel->setRange(0, 255);
            channel->setMinimumWidth(26);
            channel->setButtonSymbols(QAbstractSpinBox::NoButtons);
            channel->setAlignment(Qt::AlignRight);
            channel->setToolTip(QStringList{"Red", "Green", "Blue"}[axis]);
            layout->addWidget(channel, 1);
            refresh(channel, [channel, axis, read] {
                const QColor c(read().toString());
                channel->setValue(axis == 0 ? c.red() : axis == 1 ? c.green() : c.blue());
            });
            QObject::connect(channel, &QSpinBox::valueChanged, root, [read, write, axis, updateColor](int value) {
                QColor c(read().toString());
                if (axis == 0) c.setRed(value); else if (axis == 1) c.setGreen(value); else c.setBlue(value);
                write(c.name()); updateColor();
            });
        }
        auto* pipette = new QToolButton(root);
        pipette->setObjectName(name + ".pipette");
        pipette->setIcon(QIcon(":/text-icons/pipette.svg"));
        pipette->setFixedSize(18, 18);
        pipette->setToolTip(QObject::tr("Pick a color on screen"));
        layout->addWidget(pipette); layout->addWidget(swatch);
        refresh(swatch, updateColor);
        QObject::connect(swatch, &QToolButton::clicked, root, [root, read, write, updateColor] {
            const QColor color = interfaceColor(QColor(read().toString()), root);
            if (color.isValid()) { write(color.name()); updateColor(); }
        });
        QObject::connect(pipette, &QToolButton::clicked, root, [root, write, updateColor] {
            auto* picker = new ScreenColorPicker(root, [write, updateColor](QColor c) { write(c.name()); updateColor(); });
            picker->show(); picker->activateWindow(); picker->setFocus();
        });
    } else {
        auto* field = new QLineEdit(root);
        field->setMinimumWidth(25);
        layout->addWidget(field, 1);
        refresh(field, [field, read] { field->setText(read().toString()); });
        QObject::connect(field, &QLineEdit::editingFinished, root, [field, write] { write(field->text()); });
        if (type == "directory" || type == "multiline"
            || type == "open-file" || type == "save-file") {
            auto* browse = new QToolButton(root); browse->setText("…");
            layout->addWidget(browse);
            QObject::connect(browse, &QToolButton::clicked, root,
                             [root, field, type, fileFilter = spec.fileFilter,
                              read, write] {
                bool ok = false;
                QString text;
                if (type == "directory") {
                    text = QFileDialog::getExistingDirectory(
                        root, QObject::tr("Choose folder"), read().toString());
                } else if (type == "open-file") {
                    text = QFileDialog::getOpenFileName(
                        root, QObject::tr("Choose file"), read().toString(), fileFilter);
                } else if (type == "save-file") {
                    text = QFileDialog::getSaveFileName(
                        root, QObject::tr("Choose file"), read().toString(), fileFilter);
                } else {
                    text = QInputDialog::getMultiLineText(
                        root, QObject::tr("Edit text"), QString(), read().toString(), &ok);
                }
                if (ok || (type != "multiline" && !text.isEmpty())) {
                    field->setText(text);
                    write(text);
                }
            });
        }
    }
    for (auto* widget : root->findChildren<QWidget*>()) {
        if (widget->objectName().isEmpty()) widget->setObjectName(name + ".value");
        widget->setAccessibleName(spec.displayName);
    }
    return root;
}
}
