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
//  THE CAMERA SPEED (owner R15, lane FLYSPEED-1, replacing item 5's two
//  multipliers). CameraSpeed is ONE persisted integer 1..32 — the preference
//  camera/speed — applied as the factor n/10 on each surface's own base (8 u/s
//  the editor's fly, 25 the Player's free camera, 15 its play-mode fly, and a
//  project's `world.vr.flySpeed` in m/s for a wearer). What is pinned here is
//  the ARITHMETIC, against a number measured on main before the change:
//  ONE SECOND OF THE EDITOR'S RMB FLY AT THE DEFAULT COVERS 8.000 UNITS, and
//  n = 10 must still cover exactly that, n = 20 twice it and n = 1 a tenth of
//  it. Plus the wheel's stepping and clamping, and the persistence.
//
// No document beyond a camera node, no engine, no display.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "../support/documentgraph.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "viewport/editorcameracontroller.h"
#include "viewport/cameraspeed.h"
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
///
/// IN FRAMES, not in one step: no single fly step may be longer than
/// flystep::kMaxFlyStep (ledger §356 — a UI-thread block used to arrive as one
/// enormous dt and move the camera 110 units in a frame), so "one second of
/// flight" is the seconds the editor would really have flown them in. The total
/// distance is the same to the last float, which is what every assertion below
/// measures.
iris::Vec3 editorFly(const QVector<Qt::Key> &keys, float dt = 1.0f)
{
    EditorCameraController c(nullptr);
    auto cam = freshCamera();
    c.setCamera(cam);
    c.onMouseDown(Qt::RightButton);           // the fly only runs while RMB is held
    for (Qt::Key k : keys) c.onKeyPressed(k);
    for (float remaining = dt; remaining > 0.0f; ) {
        const float step = qMin(remaining, flystep::kMaxFlyStep);
        c.update(step);
        remaining -= step;
    }
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

/// One second of PLAY-MODE flight from a camera pitched `pitchDegrees`
/// (negative looks down), and where it ended up. The play branch is the other
/// half of update(); it is the one smoke S13 reported.
iris::Vec3 playerPlayFly(const QVector<Qt::Key> &keys, float pitchDegrees, float dt = 1.0f)
{
    KeyboardState::reset();
    PlayerMouseController c;
    auto cam = freshCamera();
    cam->setLocalRot(iris::Quat::fromEulerAngles(pitchDegrees, 0, 0));
    cam->update(0);
    c.setCamera(cam);
    c.setPlayState(true);                     // PLAYING: update()'s own branch
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
    CameraSpeed::reset();                    // unbound: pure defaults

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

    // ---- S13: PLAY MODE FLIES WHERE THE CAMERA LOOKS ---------------------
    //
    // Owner smoke, 2026-09-11: "WASD/arrows in the player are locked to XY and
    // ignore the camera's facing; they should fly in the camera direction like
    // the editor." The play branch built its forward as
    // cross(worldUp, cross(forward, worldUp)) — the forward FLATTENED onto the
    // ground plane — so a camera looking 45 degrees down and holding W walked
    // over the floor at constant height. It flies through viewport/flystep.h
    // now, the same step the editor and the Assets preview use.
    {
        const iris::Vec3 level = playerPlayFly({ Qt::Key_W }, 0.0f);
        CHECK(level.z() < -1.0f && near(level.y(), 0.0f),
              "play: W flies forward, and a LEVEL camera still holds its height");

        const iris::Vec3 down45 = playerPlayFly({ Qt::Key_W }, -45.0f);
        std::printf("    play W pitched -45 -> (%.3f %.3f %.3f)\n",
                    down45.x(), down45.y(), down45.z());
        CHECK(down45.y() < -1.0f, "play: pitched down 45 degrees, W DESCENDS (S13)");
        CHECK(down45.z() < -1.0f, "play: ...and still travels forward");
        CHECK(near(down45.y(), down45.z(), 1e-2f),
              "play: at 45 degrees the descent and the forward run are equal — the "
              "camera's TRUE forward, not a flattened one");

        const iris::Vec3 up30 = playerPlayFly({ Qt::Key_Up }, 30.0f);
        CHECK(up30.y() > 1.0f, "play: pitched UP, the arrow key climbs (both spellings, one step)");

        // The strafe is the flystep horizontal one on this surface too: a
        // pitched camera must not push the player into the floor with D.
        const iris::Vec3 strafe = playerPlayFly({ Qt::Key_D }, -60.0f);
        CHECK(near(strafe.y(), 0.0f), "play: strafing stays horizontal whatever the pitch");
        CHECK(strafe.x() > 1.0f, "play: D strafes right");

        // Q/E are the vertical pair the editor spells PageDown/PageUp.
        const iris::Vec3 rise = playerPlayFly({ Qt::Key_E }, -45.0f);
        CHECK(near(rise.y(), rise.length(), 1e-3f) && rise.y() > 1.0f,
              "play: E rises along the WORLD up, whatever the camera does");

        // ...and the free camera answers the same way (one definition).
        const iris::Vec3 godDown = playerFly({ Qt::Key_W });   // level camera
        CHECK(godDown.z() < -1.0f, "the free camera still flies forward");
    }

    // ---- THE CAMERA SPEED: one dial, measured against main -------------
    {
        CHECK(CameraSpeed::value() == 10, "a fresh install sits at 10 — the dial's middle");
        CHECK(near(CameraSpeed::factor(), 1.0f), "and 10 is a factor of exactly 1");
        CHECK(near(CameraSpeed::editorSpeed(), 8.0f),
              "the editor's base is 8 u/s (measured on the rig, unchanged by this feature)");
        CHECK(near(CameraSpeed::playerSpeed(), 25.0f), "the Player's free camera is 25 u/s");

        // THE PIN. Measured on main at babb90647, before this lane existed:
        // one second of the editor's RMB fly with Up held covers 8.000 units
        // (z goes to -8.000). n = 10 must reproduce it to the last float, or
        // the "10 is today" promise is not kept.
        //
        // AN ARROW, not W: the editor's fly moved to the arrow cluster on
        // 2026-09-09 and the letters do nothing, so this pair used to measure
        // 0.000 against 0.000 and pass vacuously (found by GIZMO-1,
        // 2026-09-15).
        const float atTen = editorFly({ Qt::Key_Up }).z();
        std::printf("    one second at n=10 -> %.4f (main measured -8.0000)\n", atTen);
        CHECK(near(atTen, -8.0f, 1e-4f),
              "n = 10 IS TODAY: one second of the RMB fly covers the same 8.000 units it "
              "covered before this dial existed");

        CameraSpeed::setValue(20);
        const float atTwenty = editorFly({ Qt::Key_Up }).z();
        std::printf("    one second at n=20 -> %.4f\n", atTwenty);
        CHECK(near(atTwenty, atTen * 2.0f, 1e-3f), "n = 20 is TWICE the distance, exactly");
        CHECK(near(CameraSpeed::editorSpeed(), 16.0f), "...which is 16 u/s");

        CameraSpeed::setValue(1);
        const float atOne = editorFly({ Qt::Key_Up }).z();
        std::printf("    one second at n=1  -> %.4f\n", atOne);
        CHECK(near(atOne, atTen * 0.1f, 1e-4f), "n = 1 is a TENTH of it, exactly");
        CHECK(near(CameraSpeed::editorSpeed(), 0.8f), "...which is 0.8 u/s");

        CameraSpeed::setValue(32);
        CHECK(near(CameraSpeed::editorSpeed(), 25.6f), "and the top of the dial is 3.2x: 25.6 u/s");

        // ONE VALUE, EVERY SURFACE — the whole point of the lane. The Player
        // had a second multiplier of its own until this; now a person who
        // slows down slows down everywhere, including in the headset.
        CameraSpeed::setValue(20);
        CHECK(near(CameraSpeed::playerSpeed(), 50.0f), "the Player's free camera follows the dial");
        CHECK(near(CameraSpeed::applyTo(15.0f), 30.0f),
              "and so does a VR wearer's project speed (world.vr.flySpeed m/s x the factor)");

        // The PLAYER's own fly, measured the same way the editor's was: its
        // 25 u/s base times the same factor, through the same one dial.
        CameraSpeed::setValue(10);
        const float playerAtTen = playerFly({ Qt::Key_Up }).z();
        CameraSpeed::setValue(20);
        const float playerAtTwenty = playerFly({ Qt::Key_Up }).z();
        std::printf("    the Player: one second at n=10 -> %.4f, at n=20 -> %.4f\n",
                    playerAtTen, playerAtTwenty);
        CHECK(near(playerAtTen, -25.0f, 1e-3f), "the Player flies 25.000 units at n = 10");
        CHECK(near(playerAtTwenty, playerAtTen * 2.0f, 1e-3f),
              "and twice that at n = 20 — one dial, both surfaces");

        // THE WHEEL WHILE FLYING steps by one, and Shift by five. Driven
        // through the controller's own gesture, not the setter, so what is
        // pinned is what a hand on the wheel gets.
        {
            CameraSpeed::setValue(10);
            EditorCameraController c(nullptr);
            auto cam = freshCamera();
            c.setCamera(cam);
            c.onMouseWheel(120);
            CHECK(CameraSpeed::value() == 10,
                  "the wheel does NOT touch the speed while the camera is not flying (it dollies)");
            c.onMouseDown(Qt::RightButton);
            c.onMouseWheel(120);
            CHECK(CameraSpeed::value() == 11, "flying, one notch up is +1");
            c.onMouseWheel(-120);
            CHECK(CameraSpeed::value() == 10, "and one notch down is -1");
            c.onKeyPressed(Qt::Key_Shift);
            c.onMouseWheel(120);
            CHECK(CameraSpeed::value() == 15, "with Shift held a notch is +5");
            c.onMouseWheel(-120);
            CHECK(CameraSpeed::value() == 10, "and -5 the other way");
            // The ends hold rather than wrap or run off.
            for (int i = 0; i < 20; ++i) c.onMouseWheel(120);
            CHECK(CameraSpeed::value() == 32, "stepping past the top stops at 32");
            for (int i = 0; i < 20; ++i) c.onMouseWheel(-120);
            CHECK(CameraSpeed::value() == 1, "and past the bottom stops at 1");
        }

        // Out of range is clamped rather than stored: a zero dial is a camera
        // that cannot move, and there is no "off".
        CameraSpeed::setValue(1000);
        CHECK(CameraSpeed::value() == 32, "clamped at the top");
        CameraSpeed::setValue(0);
        CHECK(CameraSpeed::value() == 1, "and at the bottom — zero is not a speed");
        CameraSpeed::setValue(-3);
        CHECK(CameraSpeed::value() == 1, "and so is negative");

        CameraSpeed::reset();
        CHECK(CameraSpeed::value() == 10, "reset() puts the dial back at 10");
    }

    // ---- IT SURVIVES A RELAUNCH (it is a preference) ---------------------
    //
    // Two QSettings over one file, the second one bound after the first is
    // gone: that is what the next launch of the app does with the shared
    // settings file, and the only way to prove a preference is persisted
    // without spawning a process.
    {
        QTemporaryDir dir;
        CHECK(dir.isValid(), "a scratch directory for the settings file");
        const QString path = dir.filePath("jahsettings.ini");
        {
            QSettings first(path, QSettings::IniFormat);
            CameraSpeed::bindSettings(&first);
            CHECK(CameraSpeed::value() == 10, "an empty settings file leaves the default standing");
            CameraSpeed::setValue(23);
            CameraSpeed::flush();
            first.sync();
            CameraSpeed::bindSettings(nullptr);
        }
        CameraSpeed::setValue(4);                // whatever the process did after
        {
            QSettings second(path, QSettings::IniFormat);
            CHECK(second.value("camera/speed").toInt() == 23,
                  "the dial is written as the preference camera/speed, as an INTEGER");
            CameraSpeed::bindSettings(&second);
            CHECK(CameraSpeed::value() == 23, "and the next launch comes up at 23");

            // BACK AT THE DEFAULT THE KEY GOES AWAY, so a file that has never
            // been touched and one that was set back to normal are the same
            // file (the house rule for every persisted default).
            CameraSpeed::setValue(10);
            CameraSpeed::flush();
            second.sync();
            CHECK(!second.contains("camera/speed"), "setting it back to 10 removes the key");

            // A KEY THAT IS NOT A WHOLE NUMBER leaves the default standing
            // rather than flying at nothing (the reader-defaults law).
            second.setValue("camera/speed", "quickly");
            CameraSpeed::bindSettings(&second);
            CHECK(CameraSpeed::value() == 10, "an unreadable value reads as the default");
            second.remove("camera/speed");

            // THE TWO RETIRED KEYS (camera/flySpeedEditor, camera/flySpeedPlayer)
            // are REMOVED on bind, not read: nothing is owed to old settings.
            second.setValue("camera/flySpeedEditor", 4.0);
            second.setValue("camera/flySpeedPlayer", 8.0);
            CameraSpeed::bindSettings(&second);
            CHECK(!second.contains("camera/flySpeedEditor")
                      && !second.contains("camera/flySpeedPlayer"),
                  "the two retired per-surface multipliers are swept out of the file");
            CHECK(CameraSpeed::value() == 10, "...and the dial pays no attention to them");
            CameraSpeed::bindSettings(nullptr);
        }
        CameraSpeed::reset();
    }

    // ---- A BURST OF SETS IS ONE WRITE (the fix round's item 2) -----------
    //
    // MEASURED on Qt 6.10.2: a QSettings::setValue makes the NEXT pass of the
    // event loop rewrite the whole ini through a QSaveFile — two fdatasyncs
    // and a rename, ON THE UI THREAD. One notch of the wheel mid-fly is one of
    // those, and a slider drag is one per mouse-move; the house law since
    // FSYNC-2 is that the thread that draws never waits for a disk.
    //
    // So a set moves the value at once and only ARMS the write, and a burst
    // inside one gesture writes ONCE, at the end of it.
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("jahsettings.ini");
        QSettings store(path, QSettings::IniFormat);
        CameraSpeed::bindSettings(&store);

        const int before = CameraSpeed::storeWrites();
        for (int n = 11; n <= 20; ++n) CameraSpeed::setValue(n);   // a drag, or ten notches
        CHECK(CameraSpeed::value() == 20, "the dial moved to 20 at once — the VALUE is immediate");
        CHECK(CameraSpeed::storeWrites() == before,
              "...and not one of the ten sets has touched the disk yet");

        // The gesture ends (an RMB release, a slider release, the popover
        // closing, the window shutting down) — or, failing all of those, the
        // half-second timer this armed.
        CameraSpeed::flush();
        CHECK(CameraSpeed::storeWrites() == before + 1,
              "ONE write for the whole burst, and it is the value the dial ended on");
        store.sync();
        CHECK(store.value("camera/speed").toInt() == 20, "...which is 20, in the file");
        CHECK(CameraSpeed::storeWrites() == before + 1,
              "a second flush with nothing pending writes nothing");
        CameraSpeed::flush();
        CHECK(CameraSpeed::storeWrites() == before + 1, "...still nothing");

        // AND NOBODY HAS TO CALL flush(): the arm is a half-second single shot
        // on the application's own event loop, which is what makes a wheel
        // notch in a window nobody closes durable anyway.
        const int armed = CameraSpeed::storeWrites();
        CameraSpeed::setValue(7);
        CHECK(CameraSpeed::storeWrites() == armed, "the set is still not a write");
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 2000 && CameraSpeed::storeWrites() == armed)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        std::printf("    the deferred write landed after %lld ms\n", clock.elapsed());
        CHECK(CameraSpeed::storeWrites() == armed + 1, "the armed write lands on its own");
        store.sync();
        CHECK(store.value("camera/speed").toInt() == 7, "...carrying 7");

        CameraSpeed::bindSettings(nullptr);
        CameraSpeed::reset();
    }

    // ---- THE DIAL ANNOUNCES ITSELF, whoever moved it ---------------------
    //
    // The toolbar's speed button is a VIEW of this value and there is exactly
    // one of it, on the editor's toolbar — but the PLAYER's wheel writes the
    // same dial from a page that has no toolbar. It used to call a
    // per-controller `onSpeedChanged` hook that was ASSIGNED NOWHERE, so
    // stepping the speed in the Player left the editor's button stale.
    {
        int announced = 0;
        int lastSeen = 0;
        CameraSpeed::setOnChanged([&] { ++announced; lastSeen = CameraSpeed::value(); });

        CameraSpeed::setValue(17);
        CHECK(announced == 1 && lastSeen == 17, "a set announces the new value");
        CameraSpeed::setValue(17);
        CHECK(announced == 1, "...and setting it to what it already is announces nothing");
        CameraSpeed::step(+1);
        CHECK(announced == 2 && lastSeen == 18, "a step announces");

        // THE PLAYER'S WHEEL, at the gesture level.
        {
            KeyboardState::reset();
            PlayerMouseController c;
            auto cam = freshCamera();
            c.setCamera(cam);
            const int was = announced;
            c.onMouseWheel(120);
            CHECK(announced == was, "the Player's wheel does nothing with no button held");
            c.onMouseDown(Qt::RightButton);
            c.onMouseWheel(120);
            CHECK(announced == was + 1 && lastSeen == 19,
                  "THE PLAYER'S WHEEL MOVES THE ONE DIAL AND SAYS SO — the editor's toolbar "
                  "button cannot go stale behind it");

            // ...with the same Shift stride the editor has and the button's
            // tooltip promises on both surfaces (fix round item 5).
            KeyboardState::keyStates[int(Qt::Key_Shift)] = true;
            c.onMouseWheel(120);
            CHECK(CameraSpeed::value() == 24, "Shift steps the Player's wheel by five too");
            c.onMouseWheel(-120);
            CHECK(CameraSpeed::value() == 19, "and -5 the other way");
            KeyboardState::reset();
        }

        CameraSpeed::setOnChanged({});
        CameraSpeed::setValue(3);
        CHECK(announced == 5, "a cleared handler is not called (nor a dangling one)");
        CameraSpeed::reset();
    }

    // ---- A NOTCH, NOT AN EVENT (the fix round's item 4) ------------------
    //
    // A wheel event carries angleDelta in EIGHTHS OF A DEGREE and a mouse
    // notch is 120 of them — but a high-resolution wheel or a trackpad sends
    // FRACTIONS of 120 per event, so "one event is one step" turns a gentle
    // swipe into a dozen steps. Accumulate, spend whole notches, keep the
    // remainder; and drop the remainder when the gesture ends.
    {
        CameraSpeed::reset();
        EditorCameraController c(nullptr);
        auto cam = freshCamera();
        c.setCamera(cam);
        c.onMouseDown(Qt::RightButton);

        for (int i = 0; i < 7; ++i) c.onMouseWheel(15);   // a trackpad: 7 x 1/8 notch
        CHECK(CameraSpeed::value() == 10,
              "seven eighths of a notch is not a step — a trackpad swipe no longer runs the "
              "dial away");
        c.onMouseWheel(15);                                // the eighth eighth
        CHECK(CameraSpeed::value() == 11, "...and the eighth completes ONE step");
        for (int i = 0; i < 8; ++i) c.onMouseWheel(-15);
        CHECK(CameraSpeed::value() == 10, "the same swipe back is one step down");

        // A BIG EVENT IS ITS OWN NUMBER OF NOTCHES, not one: a fling that
        // carries three notches moves three.
        c.onMouseWheel(360);
        CHECK(CameraSpeed::value() == 13, "a three-notch event steps three");

        // THE REMAINDER DIES WITH THE GESTURE: half a notch of one fly must
        // not finish a step in the next one.
        c.onMouseWheel(60);
        CHECK(CameraSpeed::value() == 13, "half a notch is still no step");
        c.onMouseUp(Qt::RightButton);
        c.onMouseDown(Qt::RightButton);
        c.onMouseWheel(60);
        CHECK(CameraSpeed::value() == 13,
              "...and half a notch in a NEW fly does not cash in the last one's half");
        c.onMouseWheel(60);
        CHECK(CameraSpeed::value() == 14, "two halves inside one fly are a step");
        c.onMouseUp(Qt::RightButton);
        CameraSpeed::reset();
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

    // ---- THE STUCK FLY KEY (owner report 2026-09-15, ledger §356) ---------
    //
    // "The arrows stop flying after a console script run." Diagnosed on the rig:
    // a key goes into the held set and never comes out, because Qt's XCB
    // auto-repeat classification is a lookahead heuristic over the X queue and
    // misfires in both directions once the UI thread stalls long enough to back
    // the queue up — a console script run of 3 to 20 seconds is exactly that
    // (1 stuck key in ~30 attempts). The set was cleared in ONE place, the
    // viewport's focusOutEvent, so a click INSIDE the viewport cured nothing.
    //
    // The symptom is SILENT, which is what made it expensive: with both Left
    // and Right in the set the movement cancels to nothing at all.
    {
        EditorCameraController c(nullptr);
        auto cam = freshCamera();
        c.setCamera(cam);
        c.onMouseDown(Qt::RightButton);
        c.onKeyPressed(Qt::Key_Left);
        c.onKeyPressed(Qt::Key_Right);
        c.update(flystep::kMaxFlyStep);
        std::printf("    Left and Right both held -> (%.3f %.3f %.3f)\n",
                    cam->getLocalPos().x(), cam->getLocalPos().y(), cam->getLocalPos().z());
        CHECK(cam->getLocalPos().isNull(),
              "the symptom, reproduced: a stuck Left cancels a real Right and the camera does "
              "not move at all — no drift, nothing in any log");
    }
    {
        EditorCameraController c(nullptr);
        auto cam = freshCamera();
        c.setCamera(cam);
        // A key that went down and never came up, during some earlier gesture.
        c.onKeyPressed(Qt::Key_Left);
        CHECK(c.heldKeyCodes().contains(int(Qt::Key_Left)),
              "a press really does put the key in the held set (heldKeyCodes reports it, which "
              "is what editor.viewportState() now shows)");
        c.onMouseDown(Qt::RightButton);
        CHECK(c.heldKeyCodes().isEmpty(),
              "…and the RIGHT BUTTON GOING DOWN drops the whole set: a stuck key cannot outlive "
              "the gesture that reads it");
        c.onKeyPressed(Qt::Key_Right);
        for (int i = 0; i < 15; ++i) c.update(flystep::kMaxFlyStep);
        std::printf("    after the clear, one second of Right -> (%.3f %.3f %.3f)\n",
                    cam->getLocalPos().x(), cam->getLocalPos().y(), cam->getLocalPos().z());
        CHECK(cam->getLocalPos().x() > 7.0f,
              "and the fly works: a full second of Right strafes the full distance");
        c.onKeyPressed(Qt::Key_Up);
        c.onMouseUp(Qt::RightButton);
        CHECK(c.heldKeyCodes().isEmpty(), "the button going UP drops it too — there is no window "
                                          "left in which a key can be stranded");
        CHECK(!c.isFlying(), "and isFlying() follows the button (editor.viewportState().flying)");
    }

    // ---- THE FLY BANKS NO MORE THAN ONE FRAME (§356's collateral defect) ---
    //
    // Measured on the rig: a fly key held across a ~13 second UI-thread block
    // charged the whole wall clock as ONE dt and moved the camera 110 units in
    // a single frame (x 22.48 -> 132.07).
    {
        EditorCameraController c(nullptr);
        auto cam = freshCamera();
        c.setCamera(cam);
        c.onMouseDown(Qt::RightButton);
        c.onKeyPressed(Qt::Key_Up);
        c.update(13.0f);                       // the stall, as one frame
        const float travelled = cam->getLocalPos().length();
        const float cap = CameraSpeed::editorSpeed() * flystep::kMaxFlyStep;
        std::printf("    a 13-second frame moved the camera %.3f units (the cap is %.3f; "
                    "unclamped it was 104)\n", travelled, cap);
        CHECK(travelled > 0.0f, "a long frame still flies");
        CHECK(travelled <= cap + 1e-3f,
              "…but by at most ONE clamped step — a 13-second block can no longer throw the "
              "camera across the scene");
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
