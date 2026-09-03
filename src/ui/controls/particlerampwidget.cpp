/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/particlerampwidget.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {

constexpr int kMaxStops = 6;   // the renderer's interpolators have six stages

/// A stop editor row is deliberately plain: a time spinbox, then whatever the
/// value control is, then a remove button. No custom painting, no drag
/// handles — the strip above already shows the result, and a drag-handle
/// gradient editor is a week of its own.
QDoubleSpinBox *makeSpin(double min, double max, double step, int decimals, double value)
{
    auto *s = new QDoubleSpinBox();
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setValue(value);
    s->setMinimumWidth(64);
    return s;
}

}  // namespace

// ---- colour ramp -----------------------------------------------------------

ParticleColourRampWidget::ParticleColourRampWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(4);

    // The strip: what a particle's colour actually does from birth to death.
    mPreview = new QWidget(this);
    mPreview->setMinimumHeight(18);
    mPreview->setToolTip(tr("A particle's colour from birth (left) to death (right)."));
    outer->addWidget(mPreview);

    mRows = new QVBoxLayout();
    mRows->setContentsMargins(0, 0, 0, 0);
    mRows->setSpacing(2);
    outer->addLayout(mRows);

    mAdd = new QPushButton(tr("Add colour key"), this);
    mAdd->setToolTip(tr("Up to six keys — the renderer's colour interpolator has six stages."));
    connect(mAdd, &QPushButton::clicked, this, [this]() {
        if (mStops.size() >= kMaxStops) return;
        ParticleRampStop s;
        s.time = mStops.isEmpty() ? 0.0f : std::min(1.0f, mStops.last().time + 0.25f);
        s.colour = mStops.isEmpty() ? QColor(255, 200, 120) : mStops.last().colour;
        s.intensity = mStops.isEmpty() ? 1.0f : mStops.last().intensity;
        mStops.append(s);
        rebuildRows();
        emit changed();
    });
    outer->addWidget(mAdd);
    rebuildRows();
}

void ParticleColourRampWidget::setStops(const QVector<ParticleRampStop> &stops)
{
    mStops = stops;
    if (mStops.size() > kMaxStops) mStops.resize(kMaxStops);
    rebuildRows();
}

void ParticleColourRampWidget::paintEvent(QPaintEvent *)
{
    if (!mPreview) return;
    QPainter p(this);
    const QRect r = mPreview->geometry();
    if (mStops.isEmpty()) {
        p.fillRect(r, QColor(40, 40, 40));
        p.setPen(QColor(140, 140, 140));
        p.drawText(r, Qt::AlignCenter, tr("no ramp — particles keep their emission colour"));
        return;
    }
    QVector<ParticleRampStop> sorted = mStops;
    std::sort(sorted.begin(), sorted.end(),
              [](const ParticleRampStop &a, const ParticleRampStop &b) { return a.time < b.time; });
    QLinearGradient g(r.topLeft(), r.topRight());
    for (const ParticleRampStop &s : sorted) {
        // The swatch, not the HDR value: a 4.0 red and a 1.0 red are the same
        // hue and the strip is about hue and alpha over life. Intensity is the
        // spinbox beside it, and the bloom is the view's business.
        QColor c = s.colour;
        c.setAlphaF(qBound(0.0, double(s.colour.alphaF()), 1.0));
        g.setColorAt(qBound(0.0, double(s.time), 1.0), c);
    }
    // A checkerboard behind it, because alpha over life is half the point.
    for (int y = r.top(); y < r.bottom(); y += 6)
        for (int x = r.left(); x < r.right(); x += 6)
            p.fillRect(QRect(x, y, 6, 6),
                       ((x / 6 + y / 6) % 2) ? QColor(60, 60, 60) : QColor(85, 85, 85));
    p.fillRect(r, g);
}

void ParticleColourRampWidget::rebuildRows()
{
    while (QLayoutItem *item = mRows->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (int i = 0; i < mStops.size(); ++i) {
        auto *row = new QWidget(this);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);

        auto *time = makeSpin(0.0, 1.0, 0.05, 2, mStops[i].time);
        time->setToolTip(tr("When in the particle's life this key applies: 0 is birth, 1 is death."));
        connect(time, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, i](double v) {
                    if (i >= mStops.size()) return;
                    mStops[i].time = float(v);
                    update();
                    emit changed();
                });
        h->addWidget(time);

        auto *swatch = new QPushButton(row);
        swatch->setFixedWidth(48);
        swatch->setAutoFillBackground(true);
        swatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #222;")
                                  .arg(mStops[i].colour.name()));
        swatch->setToolTip(tr("The key's colour. Alpha fades the particle out."));
        connect(swatch, &QPushButton::clicked, this, [this, i, swatch]() {
            if (i >= mStops.size()) return;
            const QColor picked = QColorDialog::getColor(
                mStops[i].colour, this, tr("Particle colour"), QColorDialog::ShowAlphaChannel);
            if (!picked.isValid()) return;
            mStops[i].colour = picked;
            swatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #222;")
                                      .arg(picked.name()));
            update();
            emit changed();
        });
        h->addWidget(swatch);

        auto *intensity = makeSpin(0.0, 32.0, 0.25, 2, mStops[i].intensity);
        intensity->setToolTip(tr(
            "HDR multiplier. Above 1 the key is brighter than white — which is what makes a "
            "flame bloom instead of reading as an orange sticker. Needs HDR and Bloom on in "
            "World settings to be visible."));
        connect(intensity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, i](double v) {
                    if (i >= mStops.size()) return;
                    mStops[i].intensity = float(v);
                    emit changed();
                });
        h->addWidget(intensity);

        auto *remove = new QPushButton(QStringLiteral("-"), row);
        remove->setFixedWidth(24);
        connect(remove, &QPushButton::clicked, this, [this, i]() {
            if (i >= mStops.size()) return;
            mStops.remove(i);
            rebuildRows();
            update();
            emit changed();
        });
        h->addWidget(remove);
        mRows->addWidget(row);
    }
    if (mAdd) mAdd->setEnabled(mStops.size() < kMaxStops);
    update();
}

// ---- scale ramp ------------------------------------------------------------

ParticleScaleRampWidget::ParticleScaleRampWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(4);

    mRows = new QVBoxLayout();
    mRows->setContentsMargins(0, 0, 0, 0);
    mRows->setSpacing(2);
    outer->addLayout(mRows);

    mAdd = new QPushButton(tr("Add scale key"), this);
    mAdd->setToolTip(tr("Up to six keys. NOTE a system with a scale ramp draws SQUARE particles: "
                        "the renderer's scale affector replaces both dimensions rather than "
                        "multiplying them."));
    connect(mAdd, &QPushButton::clicked, this, [this]() {
        if (mStops.size() >= kMaxStops) return;
        ParticleScaleStop s;
        s.time = mStops.isEmpty() ? 0.0f : std::min(1.0f, mStops.last().time + 0.25f);
        s.scale = mStops.isEmpty() ? 1.0f : mStops.last().scale;
        mStops.append(s);
        rebuildRows();
        emit changed();
    });
    outer->addWidget(mAdd);
    rebuildRows();
}

void ParticleScaleRampWidget::setStops(const QVector<ParticleScaleStop> &stops)
{
    mStops = stops;
    if (mStops.size() > kMaxStops) mStops.resize(kMaxStops);
    rebuildRows();
}

void ParticleScaleRampWidget::rebuildRows()
{
    while (QLayoutItem *item = mRows->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (int i = 0; i < mStops.size(); ++i) {
        auto *row = new QWidget(this);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);

        auto *time = makeSpin(0.0, 1.0, 0.05, 2, mStops[i].time);
        time->setToolTip(tr("0 is birth, 1 is death."));
        connect(time, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, i](double v) {
                    if (i >= mStops.size()) return;
                    mStops[i].time = float(v);
                    emit changed();
                });
        h->addWidget(time);

        auto *scale = makeSpin(0.0, 16.0, 0.05, 2, mStops[i].scale);
        scale->setToolTip(tr("A multiplier on Particle Scale."));
        connect(scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, i](double v) {
                    if (i >= mStops.size()) return;
                    mStops[i].scale = float(v);
                    emit changed();
                });
        h->addWidget(scale);
        h->addStretch();

        auto *remove = new QPushButton(QStringLiteral("-"), row);
        remove->setFixedWidth(24);
        connect(remove, &QPushButton::clicked, this, [this, i]() {
            if (i >= mStops.size()) return;
            mStops.remove(i);
            rebuildRows();
            emit changed();
        });
        h->addWidget(remove);
        mRows->addWidget(row);
    }
    if (mAdd) mAdd->setEnabled(mStops.size() < kMaxStops);
}
