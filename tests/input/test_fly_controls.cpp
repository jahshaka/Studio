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
//  ITEM 3, THE ARROW ALIASES. The editor's fly shipped with W/A/S/D only and
//  the player's with the ARROW KEYS only, so moving between the two spaces
//  meant changing hands — and the arrows were what the 2016 editor flew with,
//  which is the muscle memory that outlived it. Both spellings now drive both
//  controllers, and this suite asserts they produce the IDENTICAL motion, not
//  merely "some" motion: two movement models that drift apart is the defect
//  this replaces, not the fix.
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

    // ---- item 3: the arrows ARE W/A/S/D, in the editor -------------------
    {
        const iris::Vec3 w = editorFly({ Qt::Key_W });
        const iris::Vec3 up = editorFly({ Qt::Key_Up });
        std::printf("    editor W    -> (%.3f %.3f %.3f)\n", w.x(), w.y(), w.z());
        std::printf("    editor Up   -> (%.3f %.3f %.3f)\n", up.x(), up.y(), up.z());
        CHECK(w.z() < -1.0f, "W flies FORWARD (down the camera's -Z)");
        CHECK(near(w.x(), up.x()) && near(w.y(), up.y()) && near(w.z(), up.z()),
              "editor: Up is W, to the last float");

        const iris::Vec3 s = editorFly({ Qt::Key_S }), down = editorFly({ Qt::Key_Down });
        CHECK(near(s.z(), down.z()) && s.z() > 1.0f, "editor: Down is S (backwards)");
        const iris::Vec3 a = editorFly({ Qt::Key_A }), left = editorFly({ Qt::Key_Left });
        CHECK(near(a.x(), left.x()) && a.x() < -1.0f, "editor: Left is A (strafe left)");
        const iris::Vec3 d = editorFly({ Qt::Key_D }), right = editorFly({ Qt::Key_Right });
        CHECK(near(d.x(), right.x()) && d.x() > 1.0f, "editor: Right is D (strafe right)");

        // Holding BOTH spellings of one direction must not move twice as far:
        // they are aliases, not two inputs that sum.
        const iris::Vec3 both = editorFly({ Qt::Key_W, Qt::Key_Up });
        CHECK(near(both.z(), w.z()), "editor: W and Up held together move exactly as far as W");

        // Q/E are unchanged and are NOT aliased to anything.
        const iris::Vec3 e = editorFly({ Qt::Key_E });
        CHECK(e.y() > 1.0f, "editor: E still rises on the world up axis");
    }

    // ---- item 3: and W/A/S/D ARE the arrows, in the player ----------------
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

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
