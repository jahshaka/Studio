/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EXTENTMEASURE_H
#define EXTENTMEASURE_H

// HOW BIG IS THIS, IN METRES — the measurement, and nothing else.
//
// This is what is left of services/fitsize.h after the IMPORT DIALOG
// (SPECS/IMPORT_DIALOG_SPEC.md §6). The FIT-TO-SIZE POLICY is gone: there is no
// envelope, no inferred `fitScale`, no per-instance multiply at instantiation,
// no `assets.setFit`. A model's size is decided by a person, ONCE, in the
// import dialog, and baked into the asset — so every instance is placed at
// scale 1 and the editor never second-guesses a file it cannot ask.
//
// (The policy existed because an import had no other answer to a package whose
// unit declaration is wrong. It guessed from two envelopes — a rigged file is a
// character, 0.5-3 m; everything else is an object, 5 mm - 100 m — and it
// guessed on every instantiation, forever. The dialog is the honest version of
// the same question, asked once, of someone who can see the model.)
//
// WHAT SURVIVES is the measurement, because it is information the user wants
// and several surfaces show: the Transform panel's size row, `node.size()`, the
// Assets page's "Imported size", the metadata block's `extent`, and the
// dialog's own live preview.

#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <functional>
#include <limits>

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace extent
{

/// A model's axis-aligned size in metres. `valid` is false for a file with no
/// geometry to measure (an animation-only or skeleton-only export).
struct Extent
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool valid = false;
    /// WHERE it is, not just how big (lane SPACE-2): the corners of the same
    /// box, in world space, for the callers that need the BOTTOM of a model and
    /// not its height — a drop that must rest an object on a floor, and the
    /// `node.size()` verb that lets a test state that rule. Zero when the
    /// extent came from a metadata block (extentOf) rather than a measurement:
    /// a stored size has no position.
    double minv[3] = { 0.0, 0.0, 0.0 };
    double maxv[3] = { 0.0, 0.0, 0.0 };
    bool located = false;

    double largest() const { return std::max(x, std::max(y, z)); }
    double height() const { return y; }
};

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
    for (int a = 0; a < 3; ++a) { out.minv[a] = minv[a]; out.maxv[a] = maxv[a]; }
    out.located = true;
    out.valid = out.x > 0.0 || out.y > 0.0 || out.z > 0.0;
    return out;
}

/// Writes the INFORMATION a model import records about size: the measured
/// extent in metres and what the FILE declared its unit to be. Nothing here is
/// a policy — `unitScale` is what the import dialog shows the user so they can
/// disagree with the file.
inline void writeBlock(QJsonObject &meta, const Extent &e, double unitScale)
{
    if (e.valid) {
        QJsonObject box;
        box["x"] = e.x;
        box["y"] = e.y;
        box["z"] = e.z;
        meta["extent"] = box;
    } else {
        meta.remove("extent");
    }
    meta["unitScale"] = unitScale > 0.0 ? unitScale : 1.0;
}

} // namespace extent

#endif // EXTENTMEASURE_H
