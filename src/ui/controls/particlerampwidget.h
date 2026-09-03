/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PARTICLERAMPWIDGET_H
#define PARTICLERAMPWIDGET_H

// The over-life ramp editors for the emitter panel (PARTICLES_FX2_SPEC §6).
//
// Two small controls, one shape: up to SIX stops, because that is exactly how
// many stages the renderer's interpolators have. Each stop is a time (a life
// fraction, 0 at birth and 1 at death) plus a value; the strip along the top
// previews what a particle actually does over its life.
//
// They edit a plain value type and emit `changed`; the panel owns the document
// write. Neither control knows what a ParticleSystemNode is.

#include <QColor>
#include <QVector>
#include <QWidget>

class QDoubleSpinBox;
class QPushButton;
class QVBoxLayout;

/// One colour stop. Channels are LINEAR and may exceed 1 — HDR is what makes a
/// flame bloom rather than read as an orange sticker, so the intensity is
/// edited separately from the hue (`intensity` multiplies the swatch colour).
struct ParticleRampStop {
    float time = 0.0f;
    QColor colour = Qt::white;
    float intensity = 1.0f;
};

class ParticleColourRampWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ParticleColourRampWidget(QWidget *parent = nullptr);

    /// Splits the linear RGBA of each key into a displayable 0-255 swatch plus
    /// the intensity it was scaled by, so a 4.0 red still shows as red.
    void setStops(const QVector<ParticleRampStop> &stops);
    QVector<ParticleRampStop> stops() const { return mStops; }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void rebuildRows();
    QVector<ParticleRampStop> mStops;
    QVBoxLayout *mRows = nullptr;
    QPushButton *mAdd = nullptr;
    QWidget *mPreview = nullptr;
};

struct ParticleScaleStop {
    float time = 0.0f;
    float scale = 1.0f;
};

class ParticleScaleRampWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ParticleScaleRampWidget(QWidget *parent = nullptr);

    void setStops(const QVector<ParticleScaleStop> &stops);
    QVector<ParticleScaleStop> stops() const { return mStops; }

signals:
    void changed();

private:
    void rebuildRows();
    QVector<ParticleScaleStop> mStops;
    QVBoxLayout *mRows = nullptr;
    QPushButton *mAdd = nullptr;
};

#endif // PARTICLERAMPWIDGET_H
