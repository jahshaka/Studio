/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// THE PROCESS'S TEST TIER ON A SCENE (worldmodes::applyTestTier, declared in services/testtier.h). The tier table
// itself is the document's (irisgl/document/scenegraph/worldmodes.h, moved down by LAB-1); the test tier is a Studio
// PROCESS setting, so the one function that reads it stays here.
#include "services/testtier.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/worldmodes.h"

#include <QJsonObject>

namespace worldmodes {

void applyTestTier(const iris::ScenePtr &scene)
{
    if (!scene || !testtier::active()) return;
    const Mode m = modeFromName(testtier::name());
    // A PINNED ROW STAYS (below) — setMode included. setMode skips every pinned World row, but
    // it drops a giMode pin (a World Mode pick resets the technique — WORLD-MODE-1's identity),
    // and its `photon` row writes through setPhoton, whose OFF drops a giMode pin too (the
    // product's rule: "off, but pinned to VCT" renders nothing). Under a test tier
    // the pin is the document's choice and must survive the bind AND a later save (measured: a
    // giMode pin reopened at Medium/Low came back unpinned, test_needs_boot's pin case), so the
    // Photon fields and their pins are put back exactly as the document had them.
    // A PIN THAT DECIDES WHETHER PHOTON RUNS is one on giMode (the technique field IS the
    // on/off switch; the `photon` row itself is never pinned — WORLD-MODE-1): only it is what
    // setPhoton(false) writes over (giMode, giTier). A pin on a machinery row (giQuality, giDdgi, giBounces, giProbeSize)
    // says how Photon runs WHEN it runs — setPhoton honours it either way — so it never keeps
    // Photon on in a row that did not name it.
    const bool photonPinned = scene->worldOverrides.contains(QStringLiteral("giMode"));
    // The RAW fields, never the Rows' get/set: a giDdgi of -1 (auto) read through its row comes
    // back concrete, and a save would then keep the concrete value.
    // (each field by NAME: an aggregate's positional list followed a member's move silently - R7 moved `bounces`
    // below `tier` and probeSize took giBounces, tier the probe size, bounces bool(giTier): the stage 5 read's A1)
    struct KeptPhoton {
        iris::GiMode mode = {}; iris::GiQuality quality = {};
        int ddgi = 0, probeSize = 0, tier = 0;
        bool bounces = true;
        QJsonObject pins;
    } kept;
    kept.mode = scene->giMode;
    kept.quality = scene->giQuality;
    kept.ddgi = scene->giDdgi;
    kept.probeSize = scene->giProbeCaptureSize;
    kept.tier = scene->giTier;
    kept.bounces = scene->giBounces;
    if (photonPinned) {
        for (const QString &id : photonRowIds())
            if (scene->worldOverrides.contains(id)) kept.pins.insert(id, scene->worldOverrides.value(id));
    }
    setMode(scene, m);
    if (photonPinned) {
        scene->giMode = kept.mode;
        scene->giQuality = kept.quality;
        scene->giDdgi = kept.ddgi;
        scene->giBounces = kept.bounces;
        scene->giProbeCaptureSize = kept.probeSize;
        scene->giTier = kept.tier;
        for (auto it = kept.pins.constBegin(); it != kept.pins.constEnd(); ++it)
            scene->worldOverrides.insert(it.key(), it.value());
    }
    // A TEST PASSES WHAT IT NEEDS (owner rule; WORLD-MODE-1 + TEST-NEEDS-1): a test-tier process
    // boots every switchable feature its JAHSHAKA_TEST_NEEDS does not name OFF, so a row boots
    // exactly the picture its claim names and holds no VRAM for the rest.
    //
    // PHOTON: off unless named; named, it runs at the World Mode's own Photon tier (`TIER medium
    // NEEDS photon` is Photon Medium, on a mode whose column says Off too) — the identity of
    // WORLD-MODE-1, which makes this write the column's own value.
    //
    // A PINNED ROW STAYS (the lane's one rule for the switch): it never writes a row the scene
    // pinned, nor that row's backing field. Photon's switch writes giMode and giTier (and setPhoton
    // OFF drops a giMode pin, which a save would then lose), so a pin on giMode leaves Photon
    // exactly as the document has it; a machinery pin rides setPhoton.
    if (photonPinned) {
        // the document's choice
    } else if (!testtier::needs(QStringLiteral("photon"))) {
        setPhoton(scene, false, photonTier(scene));
    } else if (m != Mode::Custom) {
        setPhoton(scene, true, PhotonTier(int(m)));
    }
    // THE FOUR CHAIN FEATURES: each word switches ONE World row to its Off value (the value
    // every Low column holds). A row the scene PINNED (world.override, an Advanced edit saved
    // with it) is the document's choice and stays — the same rule setMode keeps. SSAO is 0 in
    // every column already (SSAO-DOUBLE-1: every tier is a GI tier), so `ssao` matters only for
    // a scene that pinned it.
    struct Feature { const char *need = nullptr; const char *rowId = nullptr; int off = 0; };
    static const Feature kFeatures[] = {
        { "bloom",  "bloom",        0 },
        { "ssao",   "ssao",         0 },
        { "smaa",   "smaa",        -1 },
        { "planar", "planarBudget", 0 },
    };
    for (const Feature &f : kFeatures) {
        const QString rowId = QLatin1String(f.rowId);
        if (testtier::needs(QLatin1String(f.need)) || scene->worldOverrides.contains(rowId)) continue;
        setRowValue(scene, rowId, f.off, false);
    }
}

}   // namespace worldmodes
