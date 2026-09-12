/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "player/api/playerapi.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>

#include "scripting/modules/moduleshared.h"
#include "irisgl/document/scenegraph/simulationclock.h"
#include "services/playbackservice.h"
#include "services/playerservice.h"
#include "viewport/ieditorviewport.h"
#include "services/services.h"
#include "scripting/modules/flyspeedverb.h"

using namespace scriptmod;

QVector<VerbInfo> PlayerApi::verbs() const
{
    return {
        { "play", "player.play() -> bool",
          "Starts the PLAYER space's scene — the second engine Scene the Player page renders, "
          "with its own mirror, its own PlayBack and the document's scene camera. This is NOT "
          "editor.play(), which runs the scene in place inside the editor viewport; the two "
          "spaces have independent state and both can be running. Idempotent: already playing "
          "answers true. The Player page's play button follows this, whoever calls it.",
          Needs::Engine },
        { "stop", "player.stop() -> bool",
          "Stops the player's scene and puts the pre-play transforms back (PlayBack's stop). "
          "Safe to call when already stopped.",
          Needs::Engine },
        { "playing", "player.playing() -> bool",
          "Whether the PLAYER's scene is running right now. Independent of editor.playing().",
          Needs::Engine },
        { "restart", "player.restart() -> bool",
          "Stop then play, in one call: the run begins again from the scene's pre-play "
          "transforms. A restart of a stopped player is simply a play.",
          Needs::Engine },
        { "state", "player.state() -> {available, playing, active}",
          "The player space in one read. `available` is false in sessions with no player backend "
          "(headless runs) — the honest answer to \"can I even ask\"; `playing` is whether its "
          "scene is running; `active` is whether the Player page is the space on screen, which is "
          "the difference between playing and playing where anyone can see it (the player only "
          "steps and renders while its page is shown).",
          Needs::Document },
        { "flySpeed", "player.flySpeed() -> {multiplier, base, speed, steps:[...]}",
          "THE PLAYER'S FREE-CAMERA SPEED — editor.flySpeed for the other space, and a "
          "SEPARATE value: the player's base is 25 world units per second (it flies through "
          "finished worlds, not around a model on a turntable), so the same multiplier means a "
          "different speed here. Persisted as camera/flySpeedPlayer.",
          Needs::Document },
        { "setFlySpeed", "player.setFlySpeed(multiplier | \"faster\" | \"slower\") -> {multiplier, base, speed, steps:[...]}",
          "Sets the player's free-camera speed multiplier (clamped 0.05..32) or steps it along "
          "`steps`, exactly like editor.setFlySpeed. Does not touch the editor's.",
          Needs::Document },
        { "frame", "player.frame(count = 1, dt = -1) -> bool",
          "Steps and renders exactly `count` PLAYER frames synchronously — editor.frame for the "
          "other space, on the SAME document simulation clock: with `dt` >= 0 each frame hands "
          "the clock exactly that many seconds instead of the wall time since the previous "
          "step, the clock turns them into whole 1/60 s grid steps for physics, avatars and "
          "animation, and the engine's particles are told to advance by the same amount. "
          "REFUSES a dt above scene.clock().maxAdvance (step more frames instead) and "
          "REFUSES when the Player page has never been shown — the "
          "player's on-screen view is created by its show event, and answering true while "
          "drawing nothing would be the worse answer. player.screenshot needs no view.",
          Needs::Engine },
        { "screenshot", "player.screenshot(path, {width?, height?, probes?, grade?, postFx?}) -> {path, width, height, center:{r,g,b}, probes:[...]}",
          "What the PLAYER sees, written to `path` as a PNG. Rendered through a throwaway "
          "offscreen view over the player's OWN engine Scene and the document's scene camera — "
          "the same mechanism camera.screenshot uses, and the reason this is not just "
          "editor.screenshot with an argument: the player is a second Scene with a second mirror, "
          "so a shot taken through the editor viewport would photograph the editor's world state "
          "and call it the player. Works before the Player page has ever been shown. `probes` are "
          "the same 5x5 averages editor.screenshot returns, in normalized 0..1 image coordinates; "
          "`grade` develops the shot exactly as editor.screenshot does — \"plain\" (also \"raw\"; the default: no post-processing at all, the neutral exactly-reproducible readback the pixel suites assert), \"tonemap\" (the thumbnail picture: the deterministic filmic grade only, so a bright scene does not clip to white), \"scene\" (the player's own picture: the whole post chain as the world has it, at the exposure the on-screen player view has converged on) or \"viewport\" (the whole chain with its own adaptive exposure re-seeded from the scene's value). `postFx` is the older boolean spelling of plain/viewport and still works.",
          Needs::Engine },
    };
}

PlayerService *PlayerApi::serviceOrFail(const char *verb)
{
    PlayerService *service = host.services ? host.services->player : nullptr;
    if (!service || !service->isAvailable()) {
        fail(QStringLiteral("%1: this session has no player (headless runs have no player "
                            "backend) — player.state().available reports it")
                 .arg(QString::fromLatin1(verb)));
        return nullptr;
    }
    return service;
}

bool PlayerApi::play()
{
    auto *service = serviceOrFail("player.play");
    if (!service) return false;
    if (!requireEngine()) return false;
    return service->play();
}

bool PlayerApi::stop()
{
    auto *service = serviceOrFail("player.stop");
    if (!service) return false;
    return service->stop();
}

bool PlayerApi::playing()
{
    auto *service = serviceOrFail("player.playing");
    if (!service) return false;
    return service->isPlaying();
}

bool PlayerApi::restart()
{
    auto *service = serviceOrFail("player.restart");
    if (!service) return false;
    if (!requireEngine()) return false;
    return service->restart();
}

QVariantMap PlayerApi::state()
{
    // The ONE verb that never refuses: "is there a player at all" has to be
    // answerable in a session that has none.
    PlayerService *service = host.services ? host.services->player : nullptr;
    const bool available = service && service->isAvailable();
    return QVariantMap{ { "available", available },
                        { "playing", available && service->isPlaying() },
                        { "active", available && service->isActive() } };
}

bool PlayerApi::frame(int count, double dt)
{
    auto *service = serviceOrFail("player.frame");
    if (!service) return false;
    if (!requireEngine()) return false;
    if (dt > iris::SimulationClock::kMaxAdvanceSeconds + 1e-9)
        return fail(QStringLiteral("player.frame: dt %1 s is above the clock's per-frame bound of "
                                   "%2 s (%3 steps of 1/%4); step more frames instead")
                        .arg(dt).arg(iris::SimulationClock::kMaxAdvanceSeconds)
                        .arg(iris::SimulationClock::kMaxStepsPerAdvance - 1)
                        .arg(iris::SimulationClock::kStepHz));
    if (!service->stepFrames(qBound(1, count, 4096), float(dt)))
        return fail("player.frame: the player has no view to render into yet — its on-screen "
                    "view is created when the Player page is first shown (player.screenshot "
                    "does not need one)");
    return true;
}

QVariantMap PlayerApi::screenshot(const QString &path, const QVariantMap &options)
{
    QVariantMap out;
    auto *service = serviceOrFail("player.screenshot");
    if (!service) return out;
    if (!requireEngine()) return out;
    if (path.isEmpty()) { fail("player.screenshot: a file path is required"); return out; }

    static const QStringList known = { "width", "height", "probes", "postFx", "grade" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            fail(QStringLiteral("player.screenshot: unknown option '%1' — known options are %2")
                     .arg(it.key(), known.join(QStringLiteral(", "))));
            return out;
        }
    }

    const int width = qBound(16, options.value(QStringLiteral("width"), 256).toInt(), 4096);
    const int height = qBound(16, options.value(QStringLiteral("height"), 256).toInt(), 4096);
    // THE GRADE (fix wave 2026-09-07 item 6), editor.screenshot's argument on
    // the other space. `postFx` survives as the boolean spelling it always was
    // (false = raw, true = viewport) because the pixel suites pass it; `grade`
    // is the three-way form and adds "tonemap" — the deterministic filmic
    // grade only, which is what a shot of the player should usually look like.
    IEditorViewport::ScreenshotGrade grade =
        options.value(QStringLiteral("postFx"), false).toBool()
            ? IEditorViewport::ScreenshotGrade::Viewport
            : IEditorViewport::ScreenshotGrade::Plain;
    if (options.contains(QStringLiteral("grade"))) {
        const QString word = options.value(QStringLiteral("grade")).toString().trimmed().toLower();
        if (word == QLatin1String("raw"))           grade = IEditorViewport::ScreenshotGrade::Plain;
        else if (word == QLatin1String("tonemap"))  grade = IEditorViewport::ScreenshotGrade::Tonemap;
        else if (word == QLatin1String("viewport")) grade = IEditorViewport::ScreenshotGrade::Viewport;
        else {
            fail(QStringLiteral("player.screenshot: unknown grade '%1' "
                                "(raw | tonemap | viewport)").arg(word));
            return out;
        }
    }

    const QImage img = service->screenshot(width, height, int(grade));
    if (img.isNull()) {
        fail("player.screenshot: the player produced no image — the scene has no camera, or the "
             "engine is not up");
        return out;
    }

    QFileInfo info(path);
    if (!info.dir().exists()) info.dir().mkpath(".");
    if (!img.save(path, "PNG")) {
        fail(QStringLiteral("player.screenshot: could not save '%1'").arg(path));
        return out;
    }

    const QColor center = img.pixelColor(img.width() / 2, img.height() / 2);
    out["path"] = info.absoluteFilePath();
    out["width"] = img.width();
    out["height"] = img.height();
    out["center"] = QVariantMap{ { "r", center.red() }, { "g", center.green() },
                                 { "b", center.blue() } };

    // The same 5x5 average editor.screenshot and camera.screenshot return, so
    // an assertion written against one reads identically against all three.
    QVariantList probeResults;
    for (const QVariant &p : options.value(QStringLiteral("probes")).toList()) {
        const QVariantMap pm = normalizeJs(p).toMap();
        const double px = qBound(0.0, pm.value("x").toDouble(), 1.0);
        const double py = qBound(0.0, pm.value("y").toDouble(), 1.0);
        const int ix = qMin(int(px * img.width()), img.width() - 1);
        const int iy = qMin(int(py * img.height()), img.height() - 1);
        int r = 0, g = 0, b = 0, n = 0;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int x = ix + dx, y = iy + dy;
                if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) continue;
                const QColor c = img.pixelColor(x, y);
                r += c.red(); g += c.green(); b += c.blue(); ++n;
            }
        }
        if (n > 0) { r /= n; g /= n; b /= n; }
        probeResults.append(QVariantMap{ { "x", px }, { "y", py },
                                         { "r", r }, { "g", g }, { "b", b } });
    }
    if (!probeResults.isEmpty()) out["probes"] = probeResults;
    return out;
}

// The player's half of the fly-speed control (owner request 2026-09-07) — the
// same verb over the OTHER FlySpeedSettings surface, sharing editor.setFlySpeed's
// argument grammar through flyspeedverb.h so the two cannot drift apart.
QVariantMap PlayerApi::flySpeed()
{
    return flyspeedverb::state(FlySpeedSettings::Player);
}

QVariantMap PlayerApi::setFlySpeed(const QVariant &multiplier)
{
    QString error;
    if (!flyspeedverb::apply(FlySpeedSettings::Player, multiplier, error)) {
        fail(QStringLiteral("player.setFlySpeed: %1").arg(error));
        return QVariantMap();
    }
    return flyspeedverb::state(FlySpeedSettings::Player);
}
