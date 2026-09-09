/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FITSIZE_H
#define FITSIZE_H

// FIT TO SIZE — the import-time size policy (owner report 2026-09-09:
// "everything is coming in messed up in the asset module and the avatars").
//
// THE CONVENTION IS 1 UNIT = 1 METRE. Two things already defend it:
//   * iris::ImportFlags::Canonical carries aiProcess_GlobalScale, so a file
//     that DECLARES its unit correctly arrives in metres (importer.fbx §2);
//   * the Avatar module normalizes a character's height on spawn.
// Neither covers the common case: a package whose declaration is WRONG,
// dropped straight into a scene or the Assets module. The owner's Dreyar
// download says centimetres and is authored in millimetres (17.25 m tall); a
// downloaded mask measures 526 units on its longest side. Those arrive raw.
//
// So the ONE import pipeline MEASURES every model it imports and records the
// measurement — plus the fit it infers — in the asset's metadata block. The
// fit is a property of the ASSET (the Object row), applied once at the root
// node wherever the asset is instantiated (SceneEditService::addMaterialMesh,
// which is the single instantiation route drag-drop, assets.addToScene and
// avatar.spawn all go through).
//
// AVATAR / DOUBLE-NORMALISATION (the coordination point, documented at both
// sites): the fit is applied INSIDE addMaterialMesh, and avatar.spawn calls
// avatar::normalizeCharacterHeight AFTER that call returns — so the avatar
// path measures the FITTED character, finds a plausible height and does
// nothing. A fitted character is never normalized twice, and an explicit
// avatar.spawn({height}) still wins because it is applied on top.
//
// KINDS AND ENVELOPES. A file says nothing about what it is FOR, so there are
// exactly two things we can tell apart, and the envelopes follow:
//
//   Character  the file carries a skeleton (metadata `hasSkeleton`). Measured
//              on its HEIGHT, because that is the meaningful dimension of a
//              character and the number the Avatar module normalizes.
//              Plausible 0.5-3 m, target 1.75 m — byte-identical to
//              avatar::kMinPlausibleHeight / kMaxPlausibleHeight /
//              kTargetCharacterHeight, which include this header so the two
//              rules can never drift apart.
//
//   Object     everything else: a prop, a piece of set dressing, or a whole
//              environment. Measured on its LARGEST extent, band 0.01-100 m.
//              BOTH bounds are deliberately wider than a prop's own plausible
//              size, because nothing in a plain model file says which of the
//              two it is:
//                * the TOP is the environment envelope's 100 m, not a prop's
//                  20 m — shrinking a real environment is far worse than
//                  leaving a large prop alone. A "ruined city" at ~50 units
//                  is therefore untouched; a 526-unit mask is not.
//                * the FLOOR is 5 mm, not 5 cm — a coin, a screw and a die
//                  are real assets at centimetre scale, and
//                  tests/importer/fixtures/unit_cube_cm.fbx is a CORRECTLY
//                  declared 1 cm cube that must survive untouched (it measures
//                  0.00999999977 after the float round trip, which is why the
//                  bound sits below a centimetre rather than on it). Under
//                  half a centimetre the declaration is what is wrong, not the
//                  model — unit_cube_mm.fbx, a 1 mm cube, is fitted.
//              Target 1.0 m on the largest extent.
//
// Outside its envelope -> scale so the measure lands on the kind's target,
// record fitScale + fitReason, log ONE line. Inside -> untouched, fitScale 1.
// (Considered and NOT taken: snapping a too-large object to the nearest
// decimal unit mistake — /100 for centimetres, /1000 for millimetres — which
// would preserve a pack's relative proportions. It guesses more than it
// knows, and assets.setFit is the answer for the cases it would have caught.)
//
// The block this writes into the asset's `metadata` (assets.metadata reads
// it, the lazy backfill computes it for rows imported before this landed):
//
//   extent     {x, y, z}  the model's axis-aligned world size in METRES
//   unitScale  metres per source unit as the FILE declared it (FBX
//              UnitScaleFactor / 100; 1.0 for formats with no declaration)
//   fitKind    "character" | "object"
//   fitScale   what to multiply the root node's scale by (1 = nothing to do)
//   fitReason  one human sentence; absent when fitScale is 1
//   fitSource  "auto" (this policy) | "manual" (assets.setFit)

#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace fitsize
{

enum class Kind { Character, Object };

/// A size envelope, in metres: anything in [min, max] is left alone, anything
/// outside is scaled so the measured dimension becomes `target`.
struct Envelope
{
    double min;
    double max;
    double target;
};

/// Rigged models, measured on HEIGHT. The Avatar module's normalization band.
inline constexpr Envelope kCharacter { 0.5, 3.0, 1.75 };
/// Everything else, measured on the LARGEST extent. See the header note for
/// why the max is the environment envelope's 100 m and not the prop's 20 m.
inline constexpr Envelope kObject { 0.005, 100.0, 1.0 };

/// A model's axis-aligned size in metres. `valid` is false for a file with no
/// geometry to measure (an animation-only or skeleton-only export) — which is
/// NOT fittable, exactly as it is not normalizable.
struct Extent
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool valid = false;

    double largest() const { return std::max(x, std::max(y, z)); }
    double height() const { return y; }
};

/// The decision: what to multiply by, and why.
struct Fit
{
    double scale = 1.0;
    QString reason;            ///< empty when nothing was inferred
    Kind kind = Kind::Object;
    bool applied = false;      ///< the policy changed something
};

inline const char *kindName(Kind kind)
{
    return kind == Kind::Character ? "character" : "object";
}

inline Kind kindFromName(const QString &name)
{
    return name == QStringLiteral("character") ? Kind::Character : Kind::Object;
}

/// The one classification rule: a skeleton makes it a character.
inline Kind kindFor(bool hasSkeleton) { return hasSkeleton ? Kind::Character : Kind::Object; }

inline const Envelope &envelopeFor(Kind kind)
{
    return kind == Kind::Character ? kCharacter : kObject;
}

/// The dimension the kind's envelope is expressed in.
inline double measureFor(const Extent &extent, Kind kind)
{
    return kind == Kind::Character ? extent.height() : extent.largest();
}

/// THE POLICY. Pure arithmetic on a measured extent — no I/O, no document.
inline Fit compute(const Extent &extent, Kind kind)
{
    Fit fit;
    fit.kind = kind;
    if (!extent.valid) return fit;

    const double measured = measureFor(extent, kind);
    if (!(measured > 0.0) || !std::isfinite(measured)) return fit;

    const Envelope &env = envelopeFor(kind);
    if (measured >= env.min && measured <= env.max) return fit;   // plausible

    fit.scale = env.target / measured;
    fit.applied = true;
    fit.reason = QStringLiteral("%1 measures %2 m %3 (plausible %4-%5 m) — a unit "
                                "declaration the file got wrong; fitted to %6 m (x%7)")
                     .arg(kind == Kind::Character ? QStringLiteral("character height")
                                                  : QStringLiteral("largest extent"))
                     .arg(measured, 0, 'g', 6)
                     .arg(measured < env.min ? QStringLiteral("— too small")
                                             : QStringLiteral("— too large"))
                     .arg(env.min, 0, 'g', 3)
                     .arg(env.max, 0, 'g', 3)
                     .arg(env.target, 0, 'g', 4)
                     .arg(fit.scale, 0, 'g', 6);
    return fit;
}

// ---------------------------------------------------------------------------
// The metadata block

/// Writes extent/unitScale/fitKind/fitScale/fitReason/fitSource into `meta`.
/// `hasSkeleton` is the model block's own field, so the classification and the
/// rig detection can never disagree.
inline void writeBlock(QJsonObject &meta, const Extent &extent, double unitScale,
                       bool hasSkeleton)
{
    if (extent.valid) {
        QJsonObject e;
        e["x"] = extent.x;
        e["y"] = extent.y;
        e["z"] = extent.z;
        meta["extent"] = e;
    } else {
        meta.remove("extent");
    }
    meta["unitScale"] = unitScale > 0.0 ? unitScale : 1.0;

    const Fit fit = compute(extent, kindFor(hasSkeleton));
    meta["fitKind"] = QString::fromLatin1(kindName(fit.kind));
    meta["fitScale"] = fit.scale;
    meta["fitSource"] = QStringLiteral("auto");
    if (fit.applied) meta["fitReason"] = fit.reason;
    else meta.remove("fitReason");
}

/// Records a user override (assets.setFit). `scale` <= 0 is refused upstream.
inline void writeOverride(QJsonObject &meta, double scale)
{
    meta["fitScale"] = scale;
    meta["fitSource"] = QStringLiteral("manual");
    meta["fitReason"] = QStringLiteral("set by hand (assets.setFit)");
}

/// Reads the recorded extent back, `valid` false when the block has none.
inline Extent extentOf(const QJsonObject &meta)
{
    Extent out;
    const QJsonObject e = meta.value(QStringLiteral("extent")).toObject();
    if (e.isEmpty()) return out;
    out.x = e.value("x").toDouble();
    out.y = e.value("y").toDouble();
    out.z = e.value("z").toDouble();
    out.valid = out.x > 0.0 || out.y > 0.0 || out.z > 0.0;
    return out;
}

/// THE READ SIDE, used by every instantiation. Anything missing, non-finite,
/// zero or negative reads as 1 — a broken block can never make an asset
/// vanish.
inline double fitScaleOf(const QJsonObject &meta)
{
    const double scale = meta.value(QStringLiteral("fitScale")).toDouble(1.0);
    if (!std::isfinite(scale) || scale <= 0.0) return 1.0;
    return scale;
}

/// True when the scale is worth a node write (and a log line).
inline bool isFitted(double scale) { return std::fabs(scale - 1.0) > 1e-6; }

// ---------------------------------------------------------------------------
// The document side

/// World-space axis-aligned size of every mesh under `node`, in metres. The
/// three-axis form of avatar::measureCharacterHeight's walk (mesh AABB corners
/// through the node's global transform), so the two agree by construction.
inline Extent measureNode(const iris::SceneNodePtr &node)
{
    Extent out;
    if (!node) return out;

    double minv[3] = { std::numeric_limits<double>::max(),
                       std::numeric_limits<double>::max(),
                       std::numeric_limits<double>::max() };
    double maxv[3] = { -std::numeric_limits<double>::max(),
                       -std::numeric_limits<double>::max(),
                       -std::numeric_limits<double>::max() };
    bool any = false;

    std::function<void(iris::SceneNode *)> walk = [&](iris::SceneNode *n) {
        if (!n) return;
        if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto *meshNode = static_cast<iris::MeshNode *>(n);
            if (auto mesh = meshNode->getMesh()) {
                const iris::Mat4 toWorld = n->getGlobalTransform();
                const iris::AABB local = mesh->getAABB();
                const iris::Vec3 mn = local.getMin();
                const iris::Vec3 mx = local.getMax();
                for (int c = 0; c < 8; ++c) {
                    const iris::Vec3 w = toWorld * iris::Vec3((c & 1) ? mx.x() : mn.x(),
                                                              (c & 2) ? mx.y() : mn.y(),
                                                              (c & 4) ? mx.z() : mn.z());
                    const double p[3] = { w.x(), w.y(), w.z() };
                    for (int a = 0; a < 3; ++a) {
                        minv[a] = std::min(minv[a], p[a]);
                        maxv[a] = std::max(maxv[a], p[a]);
                    }
                    any = true;
                }
            }
        }
        const int kids = n->childCount();
        for (int i = 0; i < kids; ++i) walk(n->childAt(i));
    };
    walk(node.data());

    if (!any) return out;
    out.x = maxv[0] - minv[0];
    out.y = maxv[1] - minv[1];
    out.z = maxv[2] - minv[2];
    out.valid = out.x > 0.0 || out.y > 0.0 || out.z > 0.0;
    return out;
}

/// Applies the asset's fit at the ROOT node, MULTIPLYING the existing local
/// scale (a file may carry its own root scale, and that is part of how big it
/// measured). No-op for a scale of 1. Returns true when it wrote.
inline bool applyFit(const iris::SceneNodePtr &node, double scale)
{
    if (!node || !isFitted(scale) || !std::isfinite(scale) || scale <= 0.0) return false;
    const iris::Vec3 s = node->getLocalScale();
    node->setLocalScale(iris::Vec3(s.x() * float(scale),
                                   s.y() * float(scale),
                                   s.z() * float(scale)));
    return true;
}

} // namespace fitsize

#endif // FITSIZE_H
