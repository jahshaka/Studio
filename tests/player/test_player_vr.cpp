// player.vr — THE RIG'S ARITHMETIC (SPECS/VR_SPEC.md §4.5, phase 3).
//
// THE HEADLESS HALF of the Player's VR mode, and the one that runs on every box
// in every gate: no engine, no display, no OpenXR runtime, no Monado, no
// headset. What it asserts is everything about where a wearer STANDS and where
// the fly keys take them — vrorigin.h — because every one of those decisions is
// a sign or an axis that can only be seen to be wrong ON A HEADSET, hours
// later, by a person feeling sick.
//
// The session half (a real runtime, real frames, the mirror, the pacing) is
// `vr.player_session` in tests/vr, which needs monado-service; the "no runtime
// at all" refusals are `scripting.e2e.player_vr`, which needs nothing.
//
// THE TWO CONVENTIONS UNDER TEST, from vrorigin.h: yaw is degrees about +Y with
// 0 looking down -Z, and a positive yaw is the right-handed rotation the engine
// applies (Ogre::Quaternion(Degree(yaw), UNIT_Y)) — so this file is also where
// a pin bump that changed that rotation's handedness would be caught, in a
// second, instead of in a headset.

#include <cmath>
#include <cstdio>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "player/vrorigin.h"
#include "viewport/flystep.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
                              else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
static bool nearVec(const iris::Vec3 &a, const iris::Vec3 &b, float eps = 1e-3f)
{
    return near(a.x(), b.x(), eps) && near(a.y(), b.y(), eps) && near(a.z(), b.z(), eps);
}
static void show(const char *tag, const iris::Vec3 &v)
{
    std::printf("      %s = (%.3f, %.3f, %.3f)\n", tag, double(v.x()), double(v.y()), double(v.z()));
}

/// A yaw-only rotation, in the convention vrorigin declares.
static iris::Quat yaw(float degrees)
{
    return iris::Quat::fromAxisAndAngle(iris::Vec3(0, 1, 0), degrees);
}

int main()
{
    // ---- 1. THE CONVENTION ITSELF ----------------------------------------
    // yaw 0 looks down -Z, and yawDegrees is the exact inverse of the rotation
    // that produced the forward. If this pair ever disagrees, every other
    // assertion in the file is measuring the same wrong thing twice.
    {
        CHECK(nearVec(vrorigin::levelForward(iris::Quat()), iris::Vec3(0, 0, -1)),
              "an unrotated head looks down -Z");
        CHECK(near(vrorigin::yawDegrees(iris::Quat()), 0.0f), "...which is yaw 0");
        for (float deg : { -179.0f, -90.0f, -33.0f, 0.0f, 45.0f, 90.0f, 179.0f }) {
            const float back = vrorigin::yawDegrees(yaw(deg));
            if (!near(back, deg, 1e-2f))
                std::printf("      yaw %.1f came back as %.3f\n", double(deg), double(back));
            CHECK(near(back, deg, 1e-2f), "a yaw round trips through levelForward/yawDegrees");
        }
        // The ROTATION the engine will apply, reproduced: rotateY must agree
        // with the quaternion the arithmetic assumes.
        const iris::Vec3 byQuat = yaw(37.0f).rotatedVector(iris::Vec3(1, 0, 2));
        const iris::Vec3 byMath = vrorigin::rotateY(iris::Vec3(1, 0, 2), 37.0f);
        show("by quaternion", byQuat);
        show("by rotateY   ", byMath);
        CHECK(nearVec(byQuat, byMath), "rotateY IS the +Y quaternion rotation, sign included");
    }

    // ---- 2. LOOKING STRAIGHT UP AND STRAIGHT DOWN ------------------------
    // The one place a wearer actually goes and a naive flatten gives a random
    // direction (flystep.h's pole case, at eye height).
    {
        const iris::Quat lookDown =
            yaw(25.0f) * iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), -90.0f);
        const iris::Quat lookUp =
            yaw(25.0f) * iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), 90.0f);
        show("heading, looking down", vrorigin::levelForward(lookDown));
        show("heading, looking up  ", vrorigin::levelForward(lookUp));
        CHECK(near(vrorigin::yawDegrees(lookDown), 25.0f, 0.5f),
              "looking at your feet keeps the heading you were facing");
        CHECK(near(vrorigin::yawDegrees(lookUp), 25.0f, 0.5f),
              "...and so does looking at the ceiling");
    }

    // ---- 3. THE PLACEMENT: where the camera stands is where the head is ---
    {
        // A wearer standing a metre to the left of their room's origin, turned
        // 90 degrees inside it, with their head at 1.6 m and a bowed neck.
        const iris::Vec3 headPos(-1.0f, 1.6f, 0.4f);
        const iris::Quat headRot =
            yaw(90.0f) * iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), -20.0f);
        const iris::Vec3 camPos(12.0f, 2.0f, -5.0f);
        const iris::Quat camRot = yaw(-140.0f);

        const vrorigin::Rig rig =
            vrorigin::placedOn(vrorigin::Rig(), headPos, headRot, camPos, camRot);
        show("rig position", rig.position);
        std::printf("      rig yaw = %.3f\n", double(rig.yaw));

        // NOW COMPOSE IT THE WAY THE ENGINE WILL (OgreVrSession: world = origin
        // translation * origin yaw * runtime pose) and check the head lands on
        // the camera. This is the assertion that would have caught a sign.
        const iris::Vec3 worldHead = rig.position + vrorigin::rotateY(headPos, rig.yaw);
        const iris::Quat worldHeadRot = yaw(rig.yaw) * headRot;
        show("head in the world", worldHead);
        CHECK(nearVec(worldHead, camPos), "the wearer's head lands exactly on the play camera");
        CHECK(near(vrorigin::yawDegrees(worldHeadRot), -140.0f, 1e-2f),
              "...facing exactly the way it faces");
        // AND THE WEARER KEEPS THEIR OWN PITCH: the camera is level, the head
        // is bowed, and the head stays bowed.
        const iris::Vec3 f = worldHeadRot.rotatedVector(iris::Vec3(0, 0, -1));
        CHECK(f.y() < -0.3f, "and keeps their own pitch (the bowed neck is still bowed)");

        // THE OFFSET FROM THE ROOM'S MIDDLE SURVIVES — it is a recentre, not a
        // teleport: walking back to the middle of the room must move you in the
        // world by exactly what you walked.
        const iris::Vec3 middle = rig.position + vrorigin::rotateY(iris::Vec3(0, 1.6f, 0), rig.yaw);
        const float walked = (worldHead - middle).length();
        std::printf("      distance from the room's middle = %.3f m\n", double(walked));
        CHECK(near(walked, iris::Vec3(-1.0f, 0.0f, 0.4f).length(), 1e-2f),
              "the wearer is still standing where they stood in their room");
    }

    // ---- 4. A SECOND PLACEMENT COMPOSES ON THE FIRST ---------------------
    // (player.vrRecenter over a rig that is already somewhere.)
    {
        vrorigin::Rig rig;
        rig.position = iris::Vec3(3.0f, 0.0f, 7.0f);
        rig.yaw = 63.0f;
        const iris::Vec3 headPos(0.2f, 1.7f, -0.3f);
        const iris::Quat headRot = yaw(10.0f);
        // The head, as the engine composes it under the CURRENT rig:
        const iris::Vec3 worldHead = rig.position + vrorigin::rotateY(headPos, rig.yaw);
        const iris::Quat worldHeadRot = yaw(rig.yaw) * headRot;

        const iris::Vec3 camPos(-8.0f, 1.0f, 2.0f);
        const iris::Quat camRot = yaw(175.0f);
        const vrorigin::Rig moved =
            vrorigin::placedOn(rig, worldHead, worldHeadRot, camPos, camRot);
        const iris::Vec3 landed = moved.position + vrorigin::rotateY(headPos, moved.yaw);
        show("head after the recentre", landed);
        CHECK(nearVec(landed, camPos), "a recentre from anywhere lands the head on the camera");
        CHECK(near(vrorigin::yawDegrees(yaw(moved.yaw) * headRot), 175.0f, 1e-2f),
              "...facing the camera's way");
    }

    // ---- 4b. THE RUNTIME RECENTRES THE ROOM UNDER THE WEARER -------------
    //
    // The invariant, and it is the whole point of the handling: after a
    // recentre the wearer's WORLD POSE IS UNCHANGED. A runtime that re-origins
    // its space (the Quest's long-press) reports every later pose in a new
    // frame; absorbed into the rig, a wearer standing still stays standing
    // still, and un-absorbed they are thrown across the world by whatever the
    // runtime moved. Asserted here rather than on the formula, because the
    // engine performs the same arithmetic in its own expression (it cannot
    // include this file) and an invariant catches either one drifting.
    {
        vrorigin::Rig rig;
        rig.position = iris::Vec3(-4.0f, 0.0f, 11.0f);
        rig.yaw = 47.0f;
        // Where the wearer is, before: the runtime's pose composed with the rig.
        const iris::Vec3 p(0.7f, 1.65f, -0.2f);
        const iris::Quat q = yaw(20.0f) * iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), -15.0f);
        const iris::Vec3 worldBefore = rig.position + vrorigin::rotateY(p, rig.yaw);
        const iris::Quat worldRotBefore = yaw(rig.yaw) * q;

        // THE RECENTRE: the new space's origin, in the old space's coordinates.
        const iris::Vec3 t(1.5f, 0.0f, -2.25f);
        const float tYaw = -63.0f;
        const iris::Quat u = yaw(tYaw);
        // What the runtime will report afterwards: T^-1 * P.
        const iris::Vec3 pAfter = vrorigin::rotateY(p - t, -tYaw);
        const iris::Quat qAfter = u.conjugated() * q;

        const vrorigin::Rig moved = vrorigin::rigAfterSpaceChange(rig, t, u);
        const iris::Vec3 worldAfter = moved.position + vrorigin::rotateY(pAfter, moved.yaw);
        const iris::Quat worldRotAfter = yaw(moved.yaw) * qAfter;
        show("world before the recentre", worldBefore);
        show("world after  the recentre", worldAfter);
        CHECK(nearVec(worldAfter, worldBefore, 1e-3f),
              "A RUNTIME RECENTRE DOES NOT MOVE THE WEARER: same world position");
        CHECK(near(vrorigin::yawDegrees(worldRotAfter), vrorigin::yawDegrees(worldRotBefore), 1e-2f),
              "...and the same world heading");
        // AND THE RIG IS STILL A RIG: position and heading, no tilt anywhere.
        const iris::Vec3 up = yaw(moved.yaw).rotatedVector(iris::Vec3(0, 1, 0));
        CHECK(nearVec(up, iris::Vec3(0, 1, 0), 1e-5f),
              "the rig absorbed it without tilting the horizon");
    }

    // ---- 4c. THE ABSORB SURVIVES THE NEXT FLY ----------------------------
    //
    // THE DEFECT THIS CASE EXISTS FOR (lead review, second read): the ENGINE
    // moves the rig by itself when a runtime recentres, and a host that kept
    // its own copy would push that copy plus a fly delta on the very next held
    // key — undoing the absorb and throwing the wearer back by exactly what the
    // runtime moved. The rule is that the ENGINE'S ORIGIN IS THE ONE TRUTH and
    // the host reads it back before every delta; this is that rule as
    // arithmetic, with the engine's expression on one side and the host's on
    // the other.
    {
        // The engine holds O; the wearer stands still inside their room.
        vrorigin::Rig engineRig;
        engineRig.position = iris::Vec3(8.0f, 0.0f, -3.0f);
        engineRig.yaw = 15.0f;
        const iris::Vec3 stage(0.35f, 1.7f, -0.15f);      // the runtime's pose
        const iris::Quat stageRot = yaw(5.0f);
        const auto worldOf = [](const vrorigin::Rig &r, const iris::Vec3 &p) {
            return r.position + vrorigin::rotateY(p, r.yaw);
        };
        const iris::Vec3 before = worldOf(engineRig, stage);

        // THE RUNTIME RECENTRES. The engine absorbs it (this is the engine's
        // side of the rule) and every pose it reports afterwards is in the new
        // space.
        const iris::Vec3 t(2.0f, 0.0f, 1.0f);
        const float tYaw = 40.0f;
        engineRig = vrorigin::rigAfterSpaceChange(engineRig, t, yaw(tYaw));
        const iris::Vec3 stageAfter = vrorigin::rotateY(stage - t, -tYaw);
        const iris::Quat stageRotAfter = yaw(tYaw).conjugated() * stageRot;
        CHECK(nearVec(worldOf(engineRig, stageAfter), before, 1e-3f),
              "the absorb left the wearer where they were standing");

        // NOW THE HOST FLIES, reading the rig back from the engine (rigOf's
        // job) rather than from a copy of its own.
        flystep::Keys forward;
        forward.forward = true;
        const iris::Vec3 step = vrorigin::flyDelta(yaw(engineRig.yaw) * stageRotAfter,
                                                   forward, 12.0f, 0.25f);
        vrorigin::Rig flown = engineRig;      // == what vrStatus reports
        flown.position += step;
        const iris::Vec3 after = worldOf(flown, stageAfter);
        show("moved by", after - before);
        CHECK(nearVec(after - before, step, 1e-3f),
              "AND THE FLY MOVED THEM BY THE FLY, AND BY NOTHING ELSE");

        // ...and the counter-case, so the assertion above is known to be able
        // to fail: a host that kept a STALE rig (the pre-absorb O) and pushed
        // O + step throws the wearer back by the recentre.
        vrorigin::Rig stale;
        stale.position = iris::Vec3(8.0f, 0.0f, -3.0f);
        stale.yaw = 15.0f;
        stale.position += step;
        const iris::Vec3 wrong = worldOf(stale, stageAfter);
        show("a stale host rig would move them by", wrong - before);
        CHECK(!nearVec(wrong - before, step, 0.5f),
              "(and a stale host rig would have thrown them by the recentre instead)");
    }

    // ---- 5. THE FLY: the HEAD's heading, LEVEL ---------------------------
    {
        flystep::Keys forward;
        forward.forward = true;
        // A head bowed 40 degrees, facing +X (yaw -90 looks down... check it):
        const iris::Quat bowed =
            yaw(-90.0f) * iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), -40.0f);
        const iris::Vec3 heading = vrorigin::levelForward(bowed);
        show("heading", heading);
        const iris::Vec3 step = vrorigin::flyDelta(bowed, forward, 10.0f, 0.05f);
        show("one 50 ms step at 10 u/s", step);
        CHECK(near(step.y(), 0.0f, 1e-5f),
              "pressing forward with a bowed head does NOT dive into the floor");
        CHECK(near(step.length(), 0.5f, 1e-3f), "it moves speed * dt (10 * 0.05 = 0.5)");
        CHECK(nearVec(step.normalized(), heading), "along the head's own level heading");

        // STRAFE is horizontal and to the RIGHT of that heading (flystep's own
        // definition: right = forward x worldUp).
        flystep::Keys right;
        right.right = true;
        const iris::Vec3 strafe = vrorigin::flyDelta(bowed, right, 10.0f, 0.05f);
        show("strafe", strafe);
        CHECK(near(strafe.y(), 0.0f, 1e-5f), "strafe stays level too");
        CHECK(near(iris::Vec3::dotProduct(strafe.normalized(), heading), 0.0f, 1e-3f),
              "and is square to the heading");
        CHECK(nearVec(strafe.normalized(),
                      iris::Vec3::crossProduct(heading, iris::Vec3(0, 1, 0)).normalized()),
              "on the side flystep calls right, so the two spaces agree");

        // UP/DOWN is the WORLD's up — an explicit request, never a function of
        // where the wearer happened to be looking.
        flystep::Keys up;
        up.up = true;
        CHECK(nearVec(vrorigin::flyDelta(bowed, up, 10.0f, 0.05f), iris::Vec3(0, 0.5f, 0)),
              "Q/E move along the world's up, whatever the head is doing");

        // NOTHING HELD IS NOTHING MOVED.
        CHECK(vrorigin::flyDelta(bowed, flystep::Keys(), 10.0f, 0.1f).isNull(),
              "no keys, no motion");

        // A REQUEST IS A DISTANCE: player.vrMove({seconds: 2}) walks the wearer
        // for two seconds, whatever a frame would have done.
        CHECK(near(vrorigin::flyDelta(bowed, forward, 10.0f, 2.0f).length(), 20.0f, 1e-3f),
              "an explicit two seconds moves two seconds' worth (the verb's contract)");

        // A FRAME IS CAPPED. A UI-thread block hands the frame an enormous dt;
        // a desktop camera being teleported is an annoyance, a wearer being
        // teleported is an assault. The clamp lives with the caller that HAS a
        // frame, exactly as it does for the desktop fly.
        const iris::Vec3 huge =
            vrorigin::flyDelta(bowed, forward, 10.0f, vrorigin::frameSeconds(13.0f));
        std::printf("      a 13 s frame moves %.3f m\n", double(huge.length()));
        CHECK(near(huge.length(), 10.0f * flystep::kMaxFlyStep, 1e-3f),
              "a 13 second frame moves one capped step, not 130 metres");
        CHECK(near(vrorigin::frameSeconds(0.016f), 0.016f, 1e-6f),
              "an ordinary frame is not clamped at all");

        // BOOST is the same multiplier the desktop fly uses.
        flystep::Keys boosted = forward;
        boosted.boost = true;
        CHECK(near(vrorigin::flyDelta(bowed, boosted, 10.0f, 0.05f).length(),
                   0.5f * flystep::kBoost, 1e-3f),
              "Shift boosts by flystep::kBoost, as everywhere else");
    }

    // ---- 6. THE FLY IS FLYSTEP'S OWN STEP, over a level forward ----------
    // A level head must produce EXACTLY what the desktop player produces, or
    // the two surfaces have drifted apart again (owner smoke S13).
    {
        flystep::Keys diag;
        diag.forward = true;
        diag.right = true;
        const iris::Quat level = yaw(33.0f);
        // BELOW THE CAP on both sides: flystep::delta does NOT clamp its own dt
        // (the editor's camera controller clamps before calling it, so the
        // invariant lives with the CONTROLLER there) while vrorigin::flyDelta
        // does clamp — a difference the case above measures deliberately, and
        // one that would make this comparison about the clamp instead of about
        // the direction if the step were longer than 1/15 s.
        CHECK(nearVec(vrorigin::flyDelta(level, diag, 7.0f, 0.05f),
                      flystep::delta(level, diag, 7.0f, 0.05f)),
              "with a level head, VR flight IS the desktop player's flight");
        CHECK(near(vrorigin::flyDelta(level, diag, 7.0f, 0.05f).length(), 7.0f * 0.05f, 1e-3f),
              "and a diagonal does not go faster");
    }

    std::printf(failures ? "\nplayer.vr: %d FAILURES\n" : "\nplayer.vr: PASS\n", failures);
    return failures ? 1 : 0;
}
