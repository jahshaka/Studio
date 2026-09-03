/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/iesprofile.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {

/// A whitespace/comma-separated float stream over the numeric tail of the file.
/// IES writers wrap freely (some put all 19 vertical angles on one line, some
/// on nineteen), so line structure carries no meaning past the TILT marker.
class NumberStream
{
public:
    explicit NumberStream(const QString &text) : mTokens(
        text.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {}

    bool next(float *out)
    {
        while (mIndex < mTokens.size()) {
            bool okConv = false;
            const float v = mTokens.at(mIndex++).toFloat(&okConv);
            if (okConv) { *out = v; return true; }
            return false;   // a non-numeric token in the data region is corruption
        }
        return false;
    }

private:
    QStringList mTokens;
    int         mIndex = 0;
};

QString keywordValue(const QString &header, const QString &key)
{
    const QRegularExpression re(QStringLiteral("\\[%1\\]\\s*(.*)").arg(key),
                                QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(header);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

}  // namespace

IesProfile IesProfile::parse(const QString &path)
{
    IesProfile p;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        p.error = QStringLiteral("cannot read %1").arg(QFileInfo(path).fileName());
        return p;
    }
    const QString text = QString::fromLatin1(file.readAll());
    file.close();

    // --- Header version. The renderer accepts exactly these five strings.
    static const char *kVersions[] = { "IESNA:LM-63-1986", "IESNA:LM-63-1991", "IESNA91",
                                       "IESNA:LM-63-1995", "IESNA:LM-63-2002" };
    bool validVersion = false;
    for (const char *v : kVersions)
        if (text.contains(QLatin1String(v))) { validVersion = true; break; }
    if (!validVersion) {
        p.error = QStringLiteral("%1 is not an IES photometric file (no IESNA LM-63 header)")
                      .arg(QFileInfo(path).fileName());
        return p;
    }

    // --- TILT. Must exist and must be NONE (tilt tables are unsupported).
    const int tiltPos = text.indexOf(QLatin1String("TILT"));
    if (tiltPos < 0) {
        p.error = QStringLiteral("%1 has no TILT section").arg(QFileInfo(path).fileName());
        return p;
    }
    const int tiltNonePos = text.indexOf(QLatin1String("TILT=NONE"), tiltPos);
    if (tiltNonePos < 0) {
        p.error = QStringLiteral("%1 uses a TILT table; only TILT=NONE files are supported")
                      .arg(QFileInfo(path).fileName());
        return p;
    }

    p.manufacturer = keywordValue(text.left(tiltPos), QStringLiteral("MANUFAC"));
    p.luminaire = keywordValue(text.left(tiltPos), QStringLiteral("LUMINAIRE"));
    if (p.luminaire.isEmpty())
        p.luminaire = keywordValue(text.left(tiltPos), QStringLiteral("LUMCAT"));

    const int dataStart = text.indexOf(QLatin1Char('\n'), tiltNonePos);
    if (dataStart < 0) {
        p.error = QStringLiteral("%1 is truncated after TILT").arg(QFileInfo(path).fileName());
        return p;
    }
    NumberStream nums(text.mid(dataStart + 1));

    // --- 13 header values, in the LM-63 order.
    float header[13];
    for (int i = 0; i < 13; ++i) {
        if (!nums.next(&header[i])) {
            p.error = QStringLiteral("%1 is truncated: it has %2 of the 13 required header values")
                          .arg(QFileInfo(path).fileName()).arg(i);
            return p;
        }
    }

    const int numLamps = int(header[0]);
    if (numLamps != 1) {
        p.error = QStringLiteral("%1 describes %2 lamps; only single-lamp files are supported")
                      .arg(QFileInfo(path).fileName()).arg(numLamps);
        return p;
    }
    p.lumensPerLamp = header[1];
    p.candelaMultiplier = header[2];
    p.numVerticalAngles = int(header[3]);
    p.numHorizontalAngles = int(header[4]);
    const int photometricType = int(header[5]);
    p.ballastFactor = header[10];
    p.ballastLampPhotometricFactor = header[11];
    p.inputWatts = header[12];

    if (p.numVerticalAngles <= 0 || p.numHorizontalAngles <= 0) {
        p.error = QStringLiteral("%1 declares no vertical or horizontal angles")
                      .arg(QFileInfo(path).fileName());
        return p;
    }
    if (photometricType != 1) {
        p.error = QStringLiteral("%1 is photometric type %2; only type C is supported")
                      .arg(QFileInfo(path).fileName()).arg(photometricType);
        return p;
    }

    // --- Angles: all the vertical ones, then all the horizontal ones.
    p.verticalAngles.reserve(p.numVerticalAngles);
    for (int i = 0; i < p.numVerticalAngles; ++i) {
        float v = 0.0f;
        if (!nums.next(&v)) {
            p.error = QStringLiteral("%1 contains fewer vertical angles than it declares")
                          .arg(QFileInfo(path).fileName());
            return p;
        }
        p.verticalAngles.append(v);
    }
    QVector<float> horizontal;
    horizontal.reserve(p.numHorizontalAngles);
    for (int i = 0; i < p.numHorizontalAngles; ++i) {
        float v = 0.0f;
        if (!nums.next(&v)) {
            p.error = QStringLiteral("%1 contains fewer horizontal angles than it declares")
                          .arg(QFileInfo(path).fileName());
            return p;
        }
        horizontal.append(v);
    }

    // --- Vertical range: exactly [0;90] or [0;180], strictly ascending.
    const float first = p.verticalAngles.first();
    const float last  = p.verticalAngles.last();
    if (std::fabs(first) > 1e-6f || (std::fabs(last - 90.0f) > 1e-6f &&
                                     std::fabs(last - 180.0f) > 1e-6f)) {
        p.error = QStringLiteral("%1 sweeps %2 to %3 degrees vertically; only 0-90 and 0-180 "
                                 "are supported")
                      .arg(QFileInfo(path).fileName())
                      .arg(double(first)).arg(double(last));
        return p;
    }
    p.coneType = std::fabs(last - 90.0f) <= 1e-6f ? QStringLiteral("90") : QStringLiteral("180");
    for (int i = 1; i < p.verticalAngles.size(); ++i) {
        if (p.verticalAngles.at(i) <= p.verticalAngles.at(i - 1)) {
            p.error = QStringLiteral("%1 has unsorted or repeated vertical angles at index %2")
                          .arg(QFileInfo(path).fileName()).arg(i);
            return p;
        }
    }

    // --- Horizontal set. THE DIVERGENCE (see the header comment): only a
    // single angle or a full 360-degree sweep is accepted. The renderer's
    // 180/90-degree branches overwrite the vertical cone type with a horizontal
    // one, which changes how it treats data past 90 degrees; rather than patch
    // the renderer we refuse the files that would hit it.
    const float horizSpan = std::fabs(horizontal.last() - horizontal.first());
    const bool singleSlice = p.numHorizontalAngles == 1;
    const bool full360 = std::fabs(horizSpan - 360.0f) <= 1e-6f;
    if (!singleSlice && !full360) {
        p.error = QStringLiteral("%1 sweeps %2 degrees horizontally. Jahshaka accepts only "
                                 "radially symmetric profiles (one horizontal angle) or a full "
                                 "360-degree sweep — partial sweeps hit a renderer defect that "
                                 "mislabels the vertical cone type.")
                      .arg(QFileInfo(path).fileName()).arg(double(horizSpan));
        return p;
    }

    // --- Candela grid. Every value is read (short files are corrupt), but only
    // the first horizontal slice is kept: that is the only one that is sampled.
    const int total = p.numVerticalAngles * p.numHorizontalAngles;
    p.candela.reserve(p.numVerticalAngles);
    for (int i = 0; i < total; ++i) {
        float v = 0.0f;
        if (!nums.next(&v)) {
            p.error = QStringLiteral("%1 contains %2 of the %3 candela values it declares")
                          .arg(QFileInfo(path).fileName()).arg(i).arg(total);
            return p;
        }
        if (i < p.numVerticalAngles) p.candela.append(v);
    }

    p.ok = true;
    return p;
}

float IesProfile::peakCandela() const
{
    float peak = 0.0f;
    for (float c : candela) peak = std::max(peak, c);
    return peak;
}

float IesProfile::normalisation() const
{
    const float scale = candelaMultiplier * ballastFactor * ballastLampPhotometricFactor;
    const float n = peakCandela() / 1024.0f * scale;
    // A degenerate profile must not divide the light's intensity to infinity.
    return n > 1e-6f ? n : 1.0f;
}

QVector<float> IesProfile::attenuationLut(int samples) const
{
    QVector<float> lut;
    if (!ok || candela.isEmpty() || samples < 2) return lut;
    const float peak = std::max(peakCandela(), 1e-6f);
    const bool stopsAtHorizon = coneType == QStringLiteral("90");
    lut.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        const float angle = 180.0f * float(i) / float(samples - 1);
        float value = 0.0f;
        if (angle <= verticalAngles.first()) {
            value = candela.first();
        } else if (angle >= verticalAngles.last()) {
            // No data past the last angle: a [0;90] file is dark above the
            // horizon; a [0;180] file simply holds its last value.
            value = stopsAtHorizon ? 0.0f : candela.last();
        } else {
            for (int k = 1; k < verticalAngles.size(); ++k) {
                if (angle > verticalAngles.at(k)) continue;
                const float a0 = verticalAngles.at(k - 1), a1 = verticalAngles.at(k);
                const float span = a1 - a0;
                const float t = span > 1e-6f ? (angle - a0) / span : 0.0f;
                value = candela.at(k - 1) + (candela.at(k) - candela.at(k - 1)) * t;
                break;
            }
        }
        lut.append(std::max(0.0f, value / peak));
    }
    return lut;
}

QJsonObject IesProfile::metadata(const QString &path) const
{
    QJsonObject meta;
    const QFileInfo info(path);
    meta["format"] = info.suffix().toLower();
    meta["fileSize"] = double(info.size());
    if (!ok) return meta;
    meta["verticalAngles"] = numVerticalAngles;
    meta["horizontalAngles"] = numHorizontalAngles;
    meta["coneType"] = coneType;
    meta["peakCandela"] = double(peakCandela());
    meta["lumensPerLamp"] = double(lumensPerLamp);
    meta["inputWatts"] = double(inputWatts);
    // What the mirror divides intensity by so a profile changes shape, not
    // exposure. Read back by LightBindings when a profile is bound.
    meta["normalisationFactor"] = double(normalisation());
    if (!manufacturer.isEmpty()) meta["manufacturer"] = manufacturer;
    if (!luminaire.isEmpty()) meta["luminaire"] = luminaire;
    return meta;
}

QImage IesProfile::polarThumbnail(int size) const
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(24, 24, 28));
    if (!ok || candela.isEmpty()) return image;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const float cx = size * 0.5f;
    const float cy = size * 0.30f;                 // the lamp sits near the top
    const float radius = size * 0.62f;
    const float peak = std::max(peakCandela(), 1e-6f);

    // Grid: the lamp axis (straight down, 0 degrees) and the horizon.
    painter.setPen(QPen(QColor(60, 60, 70), 1));
    painter.drawLine(QPointF(cx, cy), QPointF(cx, cy + radius));
    painter.drawLine(QPointF(cx - radius, cy), QPointF(cx + radius, cy));

    // The lobe, mirrored about the axis (type C photometry is radial here).
    QPainterPath lobe;
    lobe.moveTo(cx, cy);
    auto point = [&](float angleDeg, float value, float sign) {
        // 0 degrees points straight DOWN from the luminaire.
        const float a = float(angleDeg * M_PI / 180.0);
        const float r = radius * (value / peak);
        return QPointF(cx + sign * r * std::sin(a), cy + r * std::cos(a));
    };
    for (int i = 0; i < candela.size(); ++i)
        lobe.lineTo(point(verticalAngles.at(i), candela.at(i), -1.0f));
    for (int i = candela.size() - 1; i >= 0; --i)
        lobe.lineTo(point(verticalAngles.at(i), candela.at(i), 1.0f));
    lobe.closeSubpath();

    painter.setPen(QPen(QColor(255, 214, 120), 1.5));
    painter.setBrush(QColor(255, 196, 64, 90));
    painter.drawPath(lobe);

    // The luminaire itself.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(240, 240, 245));
    painter.drawEllipse(QPointF(cx, cy), 3.0, 3.0);
    painter.end();
    return image;
}
