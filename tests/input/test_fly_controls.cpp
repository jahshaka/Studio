/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// input.fly_controls — the two FREE cameras' movement contract (owner requests
// 2026-09-07, fix wave items 3 and 5).
//
//  THE NAVIGATION SPLIT (owner decision 2026-09-09, replacing the 2026-09-07
//  "both spellings everywhere" arrangement). The EDITOR flies on the ARROW
//  CLUSTER ONLY — Up/Down forward and back, Left/Right strafe, PageUp/PageDown
//  up and down — and W/A/S/D/Q/E are FREE there, because the letters are the
//  scarce resource in an editor and every tool shortcut wants one. The PLAYER
//  keeps BOTH spellings, because it is a game surface and a hand arriving from
//  the editor's arrows must not have to change grip to walk.
//
//  So this suite asserts two different things about the two surfaces: in the
//  editor, that the arrows fly and the letters do NOTHING AT ALL (a letter
//  that still flew would silently keep swallowing the shortcut it was freed
//  for); in the player, that both spellings produce the IDENTICAL motion, not
//  merely "some" motion.
//
//  ITEM 5, THE SPEED MULTIPLIER. FlySpeedSettings is one persisted multiplier
//  per surface on a fixed base (8 u/s editor, 25 u/s player), stepped by the
//  toolbar dropdown, the scroll wheel while flying, and editor/player
//  .setFlySpeed. What is pinned here is the ARITHMETIC — distance scales
//  exactly with the multiplier — and the ladder's stepping and clamping.
//
// No document beyond a camera node, no engine, no display.

#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "../support/documentgraph.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "viewport/editorcameracontroller.h"
#include "viewport/flyspeedsettings.h"
#include "viewport/flystep.h"
#include "viewport/keyboardstate.h"
#include "player/playermousecontroller.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } \
} while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

namespace {

iris::CameraNodePtr freshCamera()
{
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(0, 0, 0));
    cam->setLocalRot(iris::Quat());           // looking down -Z, no roll
    cam->update(0.0f);
    return cam;
}

/// One second of flight with `keys` held, and where it ended up.
iris::Vec3 editorFly(const QVector<Qt::Key> &keys, float dt = 1.0f)
{
    EditorCameraController c(nullptr);
    auto cam = freshCamera();
    c.setCamera(cam);
    c.onMouseDown(Qt::RightButton);           // the fly only runs while RMB is held
    for (Qt::Key k : keys) c.onKeyPressed(k);
    c.update(dt);
    return cam->getLocalPos();
}

iris::Vec3 playerFly(const QVector<Qt::Key> &keys, float dt = 1.0f)
{
    KeyboardState::reset();
    PlayerMouseController c;
    auto cam = freshCamera();
    c.setCamera(cam);
    c.setPlayState(false);                    // the free camera (doGodMode)
    for (Qt::Key k : keys) KeyboardState::keyStates[int(k)] = true;
    c.update(dt);
    KeyboardState::reset();
    return cam->getLocalPos();
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // A camera node IS an Ogre scene node since the scene-graph swap, so even a
    // suite that only moves one needs the headless engine underneath it
    // (tests/support/documentgraph.h). Declared FIRST so it dies LAST.
    enginetest::DocumentGraph graph("fly-controls-ogre.log");
    FlySpeedSettings::reset();               // unbound: pure defaults

    // ---- the EDITOR flies on the arrow cluster, and ONLY on it -----------
    {
        const iris::Vec3 up = editorFly({ Qt::Key_Up });
        const iris::Vec3 down = editorFly({ Qt::Key_Down });
        const iris::Vec3 left = editorFly({ Qt::Key_Left });
        const iris::Vec3 right = editorFly({ Qt::Key_Right });
        const iris::Vec3 pgup = editorFly({ Qt::Key_PageUp });
        const iris::Vec3 pgdn = editorFly({ Qt::Key_PageDown });
        std::printf("    editor Up   -> (%.3f %.3f %.3f)\n", up.x(), up.y(), up.z());
        std::printf("    editor Left -> (%.3f %.3f %.3f)\n", left.x(), left.y(), left.z());
        std::printf("    editor PgUp -> (%.3f %.3f %.3f)\n", pgup.x(), pgup.y(), pgup.z());
        CHECK(up.z() < -1.0f, "Up flies FORWARD (down the camera's -Z)");
        CHECK(down.z() > 1.0f, "Down flies backwards");
        CHECK(left.x() < -1.0f, "Left strafes left");
        CHECK(right.x() > 1.0f, "Right strafes right");
        CHECK(pgup.y() > 1.0f, "PageUp rises on the world up axis");
        CHECK(pgdn.y() < -1.0f, "PageDown descends");
        // The four horizontal keys are one movement model: forward and back
        // are the same distance in opposite directions, and so are the strafes.
        CHECK(near(up.z(), -down.z()) && near(left.x(), -right.x()),
              "editor: the opposite keys are exact mirrors of each other");

        // THE LETTERS ARE FREE. This is the point of the change, and the
        // assertion that fails if the old branch is left behind anywhere.
        for (Qt::Key k : { Qt::Key_W, Qt::Key_A, Qt::Key_S, Qt::Key_D,
                           Qt::Key_Q, Qt::Key_E }) {
            const iris::Vec3 p = editorFly({ k });
            CHECK(p.isNull(), "editor: a letter key no longer flies the camera");
        }
    }

    // ---- the PLAYER still answers to BOTH spellings ----------------------
    {
        const iris::Vec3 up = playerFly({ Qt::Key_Up });
        const iris::Vec3 w  = playerFly({ Qt::Key_W });
        std::printf("    player Up   -> (%.3f %.3f %.3f)\n", up.x(), up.y(), up.z());
        std::printf("    player W    -> (%.3f %.3f %.3f)\n", w.x(), w.y(), w.z());
        CHECK(up.z() < -1.0f, "the player's Up flies forward");
        CHECK(near(w.x(), up.x()) && near(w.y(), up.y()) && near(w.z(), up.z()),
              "player: W is Up, to the last float");
        const iris::Vec3 s = playerFly({ Qt::Key_S }), down = playerFly({ Qt::Key_Down });
        CHECK(near(s.z(), down.z()) && s.z() > 1.0f, "player: S is Down");
        const iris::Vec3 a = playerFly({ Qt::Key_A }), left = playerFly({ Qt::Key_Left });
        CHECK(near(a.x(), left.x()) && a.x() < -1.0f, "player: A is Left");
        const iris::Vec3 d = playerFly({ Qt::Key_D }), right = playerFly({ Qt::Key_Right });
        CHECK(near(d.x(), right.x()) && d.x() > 1.0f, "player: D is Right");
    }

    // ---- item 5: the speed multiplier ------------------------------------
    {
        CHECK(near(FlySpeedSettings::multiplier(FlySpeedSettings::Editor), 1.0f),
              "a fresh install flies at 1x");
        CHECK(near(FlySpeedSettings::baseSpeed(FlySpeedSettings::Editor), 8.0f),
              "the editor's base is 8 u/s (measured on the rig, unchanged by this feature)");
        CHECK(near(FlySpeedSettings::baseSpeed(FlySpeedSettings::Player), 25.0f),
              "the player's base is 25 u/s");
        CHECK(near(FlySpeedSettings::speed(FlySpeedSettings::Editor), 8.0f),
              "speed = base * multiplier");

        const float atOne = editorFly({ Qt::Key_W }).z();
        FlySpeedSettings::setMultiplier(FlySpeedSettings::Editor, 4.0f);
        const float atFour = editorFly({ Qt::Key_W }).z();
        std::printf("    one second at 1x -> %.3f, at 4x -> %.3f\n", atOne, atFour);
        CHECK(near(atFour, atOne * 4.0f, 1e-3f),
              "FOUR TIMES the multiplier is FOUR TIMES the distance, exactly");

        // The two surfaces are independent — the whole reason FlySpeedSettings
        // has a Surface argument rather than one global number.
        CHECK(near(FlySpeedSettings::multiplier(FlySpeedSettings::Player), 1.0f),
              "changing the editor's speed left the player's alone");
        FlySpeedSettings::setMultiplier(FlySpeedSettings::Player, 8.0f);
        CHECK(near(FlySpeedSettings::multiplier(FlySpeedSettings::Editor), 4.0f),
              "and the reverse");

        // The ladder: the scroll wheel's and the dropdown's steps.
        FlySpeedSettings::setMultiplier(FlySpeedSettings::Editor, 1.0f);
        CHECK(near(FlySpeedSettings::step(FlySpeedSettings::Editor, +1), 2.0f),
              "one step up from 1x is 2x");
        CHECK(near(FlySpeedSettings::step(FlySpeedSettings::Editor, -1), 1.0f),
              "and one step back down is 1x again");
        // Clamps at the ends rather than wrapping or running off.
        for (int i = 0; i < 20; ++i) FlySpeedSettings::step(FlySpeedSettings::Editor, +1);
        CHECK(near(FlySpeedSettings::multiplier(FlySpeedSettings::Editor),
                   FlySpeedSettings::steps().last()),
              "stepping past the top stops at the top of the ladder");
        for (int i = 0; i < 20; ++i) FlySpeedSettings::step(FlySpeedSettings::Editor, -1);
        CHECK(near(FlySpeedSettings::multiplier(FlySpeedSettings::Editor),
                   FlySpeedSettings::steps().first()),
              "and past the bottom stops at the bottom");

        // A value OFF the ladder is legal (the verb accepts any multiplier);
        // stepping from there moves to the nearest rung in that direction.
        FlySpeedSettings::setMultiplier(FlySpeedSettings::Editor, 1.3f);
        CHECK(near(FlySpeedSettings::step(FlySpeedSettings::Editor, +1), 2.0f),
              "stepping up from an off-ladder 1.3x lands on 2x");
        FlySpeedSettings::setMultiplier(FlySpeedSettings::Editor, 1.3f);
        CHECK(near(FlySpeedSettings::step(FlySpeedSettings::Editor, -1), 1.0f),
              "and stepping down lands on 1x");

        // Out of range, and nonsense, are clamped to something flyable rather
        // than stored: a zero multiplier is a camera that cannot move.
        FlySpeedSettings::setMultiplier(FlySpeedSettings::Editor, 1e6f);
        CHECK(FlySpeedSettings::multiplier(FlySpeedSettings::Editor) <= 32.0f, "clamped at the top");
        FlySpeedSettings::setMultiplier(FlySpeedSettings::Editor, 0.0f);
        CHECK(FlySpeedSettings::multiplier(FlySpeedSettings::Editor) > 0.0f,
              "zero is refused — it is not a speed");
        FlySpeedSettings::setMultiplier(FlySpeedSettings::Editor, -3.0f);
        CHECK(FlySpeedSettings::multiplier(FlySpeedSettings::Editor) > 0.0f, "and so is negative");

        FlySpeedSettings::reset();
    }

    // ---- the fly only runs while the right button is held -----------------
    {
        EditorCameraController c(nullptr);
        auto cam = freshCamera();
        c.setCamera(cam);
        c.onKeyPressed(Qt::Key_Up);        // no RMB
        c.update(1.0f);
        CHECK(near(cam->getLocalPos().z(), 0.0f),
              "an arrow key alone does NOT fly — the Unreal rule is unchanged by the aliases");
    }

    // ---- THE ASSETS PREVIEW'S FLY (smoke S7, 2026-09-11) ------------------
    //
    // "WASD and the arrow keys do not fly in the Assets module." They do now,
    // and they fly through the EDITOR'S OWN step (viewport/flystep.h) rather
    // than a second copy of it — which is what this section pins: the same
    // direction, for both spellings, from the same camera pose.
    {
        auto cam = freshCamera();
        cam->setLocalRot(iris::Quat::fromEulerAngles(-20, 35, 0));   // an arbitrary pose
        cam->update(0);
        const iris::Quat rot = cam->getLocalRot();

        flystep::HeldKeys keys;
        CHECK(keys.press(Qt::Key_W), "W is a fly key in the Assets preview");
        const iris::Vec3 wDir = flystep::direction(rot, keys.state());
        keys.release(Qt::Key_W);
        CHECK(keys.press(Qt::Key_Up), "so is Up");
        const iris::Vec3 upDir = flystep::direction(rot, keys.state());
        keys.release(Qt::Key_Up);
        CHECK(near(wDir.x(), upDir.x()) && near(wDir.y(), upDir.y()) && near(wDir.z(), upDir.z()),
              "W and Up are the SAME motion in the Assets preview (both spellings, one step)");

        // ...and that motion is the camera's forward, which is what the editor
        // controller does with the same key.
        const iris::Vec3 forward = rot.rotatedVector(iris::Vec3(0, 0, -1)).normalized();
        CHECK(near(wDir.x(), forward.x()) && near(wDir.y(), forward.y()) && near(wDir.z(), forward.z()),
              "forward flight is the camera's own forward (camera-relative, S7's ask)");

        // Strafe stays horizontal; Q/E are the vertical pair the editor spells
        // PageDown/PageUp.
        keys.press(Qt::Key_D);
        const iris::Vec3 strafe = flystep::direction(rot, keys.state());
        keys.release(Qt::Key_D);
        CHECK(near(strafe.y(), 0.0f), "strafe stays horizontal");
        keys.press(Qt::Key_E);
        const iris::Vec3 upMove = flystep::direction(rot, keys.state());
        keys.release(Qt::Key_E);
        CHECK(near(upMove.y(), 1.0f), "E rises along the WORLD up");
        keys.press(Qt::Key_PageUp);
        const iris::Vec3 pgUp = flystep::direction(rot, keys.state());
        keys.release(Qt::Key_PageUp);
        CHECK(near(pgUp.y(), 1.0f), "PageUp is the same rise (the editor's spelling)");

        // A key that is not a fly key is not consumed — the preview must not
        // swallow the rest of the keyboard.
        CHECK(!keys.press(Qt::Key_F), "F is not a fly key (the preview does not eat it)");

        // Shift is the editor's boost, exactly.
        keys.press(Qt::Key_W);
        const iris::Vec3 plain = flystep::delta(rot, keys.state(), 4.0f, 0.5f);
        keys.setBoost(true);
        const iris::Vec3 boosted = flystep::delta(rot, keys.state(), 4.0f, 0.5f);
        CHECK(near(boosted.length(), plain.length() * flystep::kBoost, 1e-3f),
              "Shift multiplies the step by the editor's boost");
        keys.setBoost(false);

        // Nothing held is no movement (a preview that drifts is a bug).
        keys.clear();
        CHECK(flystep::direction(rot, keys.state()).isNull(), "no key, no motion");

        // The pole case the editor's fallback exists for: looking straight
        // down, the horizontal strafe is degenerate and must still be defined.
        cam->setLocalRot(iris::Quat::fromEulerAngles(-90, 0, 0));
        cam->update(0);
        keys.press(Qt::Key_A);
        const iris::Vec3 poleStrafe = flystep::direction(cam->getLocalRot(), keys.state());
        CHECK(!poleStrafe.isNull(), "A still moves with the camera looking straight down");
    }

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
