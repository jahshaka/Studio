/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef IESPROFILE_H
#define IESPROFILE_H

// IESNA LM-63 photometric file parsing — OUR reader, deliberately, even though
// the renderer has one (LIGHTS_COMPLETION_SPEC §2.6).
//
// Three jobs the engine's loader cannot do for us:
//   1. VALIDATION BEFORE IMPORT. Ogre's IesLoader throws, and it throws at
//      first render — a bad file would become a viewport error message instead
//      of a clean "this file is not importable" at the import dialog.
//   2. The PEAK PHOTOMETRIC SCALE. The renderer multiplies a light's
//      attenuation by `candela/1024 * candelaMultiplier * ballastFactor *
//      ballastLampPhotometricFactor` — raw magnitude, not normalised. Real
//      luminaires push that into the hundreds, so binding a profile would
//      silently multiply the light's brightness. We record the peak here and
//      divide it out in the mirror, which keeps intensity meaning what the
//      user set it to.
//   3. LIBRARY METADATA AND THE THUMBNAIL. A polar plot of the vertical slice
//      is the only way a user tells two profiles apart in the asset list.
//
// One deliberate divergence from the engine's loader, and it is a defect
// dodge, not a policy: upstream's horizontal-angle branch assigns
// `mLampConeType` (the VERTICAL cone type) instead of `mLampHorizType` for the
// 180-degree and 90-degree cases, clobbering the value it set moments earlier
// and changing whether candela data past 90 degrees reads as black. We accept
// only files with a single horizontal angle or a full 360-degree sweep — the
// one branch that does not clobber — and reject the rest with a message that
// says why. Zero patches to Ogre.

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>

class IesProfile
{
public:
    /// Parse `path`. `ok` false ⇒ `error` carries a user-facing reason.
    static IesProfile parse(const QString &path);

    bool    ok = false;
    QString error;

    // Header (IesLoader's headerValues indices in brackets).
    float   candelaMultiplier = 1.0f;    // [2]
    int     numVerticalAngles = 0;       // [3]
    int     numHorizontalAngles = 0;     // [4]
    float   lumensPerLamp = 0.0f;        // [1]
    float   ballastFactor = 1.0f;        // [10]
    float   ballastLampPhotometricFactor = 1.0f;  // [11]
    float   inputWatts = 0.0f;           // [12]
    /// "90" (data stops at the horizon) or "180" (full sphere) — the vertical
    /// range, which is also what decides whether the renderer treats angles
    /// past 90 degrees as black.
    QString coneType;
    QString manufacturer;                // [MANUFAC] keyword, if present
    QString luminaire;                   // [LUMINAIRE] / [LUMCAT] keyword

    /// The vertical angles, ascending, degrees.
    QVector<float> verticalAngles;
    /// Candela values for the FIRST horizontal slice — the only one the
    /// renderer samples (every profile is treated as radially symmetric).
    QVector<float> candela;

    /// The renderer's peak attenuation multiplier for this profile:
    /// max(candela) / 1024 * multiplier * ballast * photometric factor.
    /// Never zero (a degenerate all-zero file yields 1.0, i.e. no correction).
    float normalisation() const;

    /// Peak candela in the file's own units — display only.
    float peakCandela() const;

    /// The metadata block recorded on the library row (AssetMetadata shape:
    /// always carries format + fileSize, then the per-type fields).
    QJsonObject metadata(const QString &path) const;

    /// A polar plot of the vertical candela slice, `size` x `size`, for the
    /// asset thumbnail. Pure QPainter — no engine round-trip.
    QImage polarThumbnail(int size = 128) const;

    /// The attenuation lookup the renderers sample, resampled to `samples`
    /// evenly spaced vertical angles across [0;180] degrees and NORMALISED so
    /// the peak is 1.
    ///
    /// This is deliberately OUR curve and not a third-party loader's: Ogre's
    /// shader reads `u = acos(dot(-lightDir, spotDir)) / pi` and takes the red
    /// channel, and three.js's IESSpotLightNode reads `acos(...) * (1/pi)` and
    /// takes the red channel — the same scheme, so one generator keeps the web
    /// export's lobe identical to the viewport's instead of approximately like
    /// it. Angles past 90 degrees read as zero for a [0;90] file, exactly as
    /// the renderer's own conversion does.
    QVector<float> attenuationLut(int samples = 180) const;
};

#endif // IESPROFILE_H
