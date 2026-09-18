// vr.grab_maths — WHAT A HAND DOES TO AN OBJECT IT IS HOLDING
// (SPECS/VR_INPUT_SPEC.md §5.1, §6; stage 1).
//
// THE HEADLESS HALF of the VR interaction, and the one that runs on every box in
// every gate: no engine, no display, no OpenXR runtime, no Monado, no headset,
// no controller. It is the sibling of `player.vr` (the rig's arithmetic) and it
// exists for the same reason that one does — every decision in vrgrab.h is a
// sign, an axis or an order of multiplication that can only be seen to be wrong
// ON A HEADSET, minutes later, by a person who has just watched an object fly
// off or snap to somewhere nobody chose.
//
// The gesture half (the picker, the selection, the undo macro) is
// `scripting.e2e.vr_input_headless`, which needs an engine but no runtime; the
// rig half (locomotion actually moving a wearer) is `vr.input_session`, which
// needs Monado.
//
// EVERY EXPECTATION BELOW IS HAND COMPUTED and written in the assertion, so a
// failure says what the answer should have been rather than that two
// expressions disagree.

#include <cmath>
#include <cstdio>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "modules/vr/vrgrab.h"
#include "services/vrorigin.h"
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
    std::printf("      %s = (%.4f, %.4f, %.4f)\n", tag, double(v.x()), double(v.y()),
                double(v.z()));
}
static iris::Quat yaw(float degrees)
{
    return iris::Quat::fromAxisAndAngle(iris::Vec3(0, 1, 0), degrees);
}
static iris::Quat pitch(float degrees)
{
    return iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), degrees);
}

int main()
{
    // ---- 1. THE AIM RAY'S CONVENTION -------------------------------------
    // -Z of the rotation, which is OpenXR's aim pose AND this document model's
    // camera. If this is wrong every pick in the headset points backwards.
    {
        CHECK(nearVec(vrgrab::aimDirection(iris::Quat()), iris::Vec3(0, 0, -1)),
              "an unrotated aim pose points down -Z");
        CHECK(nearVec(vrgrab::aimDirection(yaw(90.0f)), iris::Vec3(-1, 0, 0)),
              "yawed 90 degrees it points down -X (the right-handed turn about +Y)");
        const vrgrab::Pose aim{ iris::Vec3(1, 2, 3), yaw(180.0f) };
        show("a point 5 m along a ray turned about", vrgrab::rayPoint(aim, 5.0f));
        CHECK(nearVec(vrgrab::rayPoint(aim, 5.0f), iris::Vec3(1, 2, 8)),
              "and rayPoint walks along it: (1,2,3) + 5*(0,0,+1) = (1,2,8)");
    }

    // ---- 2. RIGID ATTACH: THE IDENTITY CASE ------------------------------
    // Zero hand delta must be a bit-for-bit hold. A formula that drifts here
    // makes a held object crawl while the wearer stands still.
    {
        const vrgrab::Pose hand{ iris::Vec3(0.3f, 1.4f, -0.2f), yaw(21.0f) * pitch(-9.0f) };
        const vrgrab::Pose node{ iris::Vec3(2.0f, 0.5f, -4.0f), yaw(-70.0f) };
        const vrgrab::Pose out = vrgrab::rigidFollow(hand, hand, node);
        CHECK(nearVec(out.position, node.position, 1e-5f),
              "a hand that has not moved leaves the object exactly where it was");
        CHECK(near(vrorigin::yawDegrees(out.rotation), vrorigin::yawDegrees(node.rotation), 1e-3f),
              "...facing exactly the way it faced");
    }

    // ---- 3. A 10 CM HAND MOVE IS A 10 CM OBJECT MOVE ---------------------
    {
        const vrgrab::Pose hand0{ iris::Vec3(0, 1, 0), iris::Quat() };
        const vrgrab::Pose hand1{ iris::Vec3(0, 1.1f, 0), iris::Quat() };   // 10 cm up
        const vrgrab::Pose node0{ iris::Vec3(3, 0, -7), yaw(15.0f) };
        const vrgrab::Pose out = vrgrab::rigidFollow(hand0, hand1, node0);
        show("the object after a 10 cm hand lift", out.position);
        CHECK(nearVec(out.position, iris::Vec3(3, 0.1f, -7)),
              "a pure translation of the hand translates the object by the same vector");
        CHECK(near(vrorigin::yawDegrees(out.rotation), 15.0f, 1e-2f),
              "...and does not rotate it at all");
    }

    // ---- 4. A WRIST TURN TURNS THE OBJECT ABOUT THE HAND -----------------
    //
    // THE ASSERTION THAT WOULD CATCH THE COMMON BUG. Hand at the origin, object
    // a metre along +X. A yaw of +90 degrees about +Y carries (1,0,0) to
    // (0,0,-1) — the object swings ROUND the hand. Spinning it in place instead
    // (the wrong formula, `node.rot = turn * node0.rot` with the position left
    // alone) would leave it at (1,0,0), which looks almost right in a still
    // picture and completely wrong in the hand.
    {
        const vrgrab::Pose hand0{ iris::Vec3(0, 0, 0), iris::Quat() };
        const vrgrab::Pose hand1{ iris::Vec3(0, 0, 0), yaw(90.0f) };
        const vrgrab::Pose node0{ iris::Vec3(1, 0, 0), iris::Quat() };
        const vrgrab::Pose out = vrgrab::rigidFollow(hand0, hand1, node0);
        show("the object after a 90 degree wrist turn", out.position);
        CHECK(nearVec(out.position, iris::Vec3(0, 0, -1)),
              "a 90 degree wrist turn swings the object round the HAND: (1,0,0) -> (0,0,-1)");
        CHECK(near(vrorigin::yawDegrees(out.rotation), 90.0f, 1e-2f),
              "...and turns it by the same 90 degrees");
        // ...and the distance from the hand is preserved, which is the whole
        // meaning of "rigid".
        CHECK(near((out.position - hand1.position).length(),
                   (node0.position - hand0.position).length()),
              "the object's distance from the hand never changes during a grab");
    }

    // ---- 5. THE FAR GRAB KEEPS ITS DISTANCE ALONG THE RAY ----------------
    {
        const vrgrab::Pose aim0{ iris::Vec3(0, 1.5f, 0), iris::Quat() };
        const float d = 10.0f;
        const vrgrab::Pose hand0 = vrgrab::virtualFarHand(aim0, d);
        CHECK(nearVec(hand0.position, iris::Vec3(0, 1.5f, -10.0f)),
              "the virtual hand of a far grab sits ON the ray, at the hit's distance");
        // The object was exactly at the hit; the wearer turns 30 degrees.
        const vrgrab::Pose node0{ hand0.position, iris::Quat() };
        const vrgrab::Pose aim1{ aim0.position, yaw(30.0f) };
        const vrgrab::Pose hand1 = vrgrab::virtualFarHand(aim1, d);
        const vrgrab::Pose out = vrgrab::rigidFollow(hand0, hand1, node0);
        show("the object after a 30 degree sweep at 10 m", out.position);
        CHECK(near((out.position - aim1.position).length(), d, 1e-3f),
              "after sweeping the ray the object is still 10 m away");
        const iris::Vec3 along = vrgrab::rayPoint(aim1, d);
        CHECK(nearVec(out.position, along),
              "...and still exactly ON the ray (it rode the end of the line)");
    }

    // ---- 6. PUSH / PULL IS MULTIPLICATIVE --------------------------------
    {
        // 10 m, full stick forward, half a second, rate 1.5: 10 * e^0.75 =
        // 21.170 m. The same gesture at 20 cm: 0.2 * e^0.75 = 42.3 cm — the
        // POINT of multiplying rather than adding, over three orders of
        // magnitude of scene. (Below kMinGrabDistance the clamp takes over,
        // which the two clamp cases below assert.)
        CHECK(near(vrgrab::pushPulled(10.0f, 1.0f, 0.5f), 21.170f, 1e-2f),
              "a half second of full stick at 10 m pushes to 21.17 m (10 * e^0.75)");
        CHECK(near(vrgrab::pushPulled(0.2f, 1.0f, 0.5f), 0.42340f, 1e-4f),
              "...and the same gesture at 20 cm pushes to 42.3 cm: one dial, every scale");
        CHECK(near(vrgrab::pushPulled(10.0f, -1.0f, 0.5f), 4.724f, 1e-2f),
              "pulling is the same factor the other way (10 * e^-0.75 = 4.724 m)");
        CHECK(near(vrgrab::pushPulled(10.0f, 0.2f, 0.5f), 10.0f),
              "a stick inside the dead zone moves nothing");
        CHECK(near(vrgrab::pushPulled(0.05f, -1.0f, 5.0f), vrgrab::kMinGrabDistance),
              "and it cannot be pulled inside the eye (the near clamp holds)");
        CHECK(near(vrgrab::pushPulled(400.0f, 1.0f, 5.0f), vrgrab::kMaxGrabDistance),
              "nor pushed past the far clamp");
    }

    // ---- 7. THE TURNTABLE SPINS IN PLACE ---------------------------------
    {
        const vrgrab::Pose p{ iris::Vec3(4, 1, -9), yaw(10.0f) };
        const vrgrab::Pose out = vrgrab::spunAboutUp(p, p.position, 35.0f);
        CHECK(nearVec(out.position, p.position, 1e-5f),
              "spun about its own position, an object does not move");
        CHECK(near(vrorigin::yawDegrees(out.rotation), 45.0f, 1e-2f),
              "...and its heading turns by exactly the angle asked for (10 + 35 = 45)");
        // About somebody ELSE's pivot it orbits — which is what a group turn
        // needs (one object spinning in place beside another spinning in place
        // is not a rotation of the pair).
        const vrgrab::Pose orbit = vrgrab::spunAboutUp(
            vrgrab::Pose{ iris::Vec3(1, 0, 0), iris::Quat() }, iris::Vec3(0, 0, 0), 90.0f);
        CHECK(nearVec(orbit.position, iris::Vec3(0, 0, -1)),
              "about another pivot it ORBITS: (1,0,0) about the origin -> (0,0,-1)");
        CHECK(near(vrgrab::turntableDegrees(1.0f, 0.25f), 22.5f),
              "a quarter second of full stick turns the turntable 22.5 degrees, in the "
              "STICK's own sign (the call site negates it, exactly as the wearer's turn does)");
        CHECK(near(vrgrab::turntableDegrees(0.1f, 0.25f), 0.0f),
              "...and the dead zone applies to it too");
        // WHICH WAY IS "RIGHT"? The convention, pinned in the algebra rather
        // than in a comment (the Fable read of stage 1, finding 6: the turn and
        // the turntable were spinning opposite ways from the same stick). The
        // tree's yaw is the RIGHT-HANDED rotation about +Y, so a positive angle
        // carries +Z toward +X — counter-clockwise seen from above — and a
        // flick right therefore has to arrive here NEGATED to spin the held
        // object CLOCKWISE from above, which is the direction the same stick
        // turns the wearer.
        const vrgrab::Pose spunRight = vrgrab::spunAboutUp(
            vrgrab::Pose{ iris::Vec3(0, 0, 1), iris::Quat() }, iris::Vec3(0, 0, 0),
            -vrgrab::turntableDegrees(1.0f, 1.0f));   // ONE SECOND of full stick right = 90 deg
        CHECK(near(spunRight.position.x(), -1.0f, 1e-5f) &&
                  near(spunRight.position.z(), 0.0f, 1e-5f),
              "one second of full stick RIGHT, negated as the call site negates it, carries a "
              "point at +Z to -X: CLOCKWISE from above, the way the wearer's own turn goes");
    }

    // ---- 8. SNAPPING QUANTISES THE DELTA, NEVER THE ABSOLUTE -------------
    //
    // THE ASSERTION THE WHOLE RULE EXISTS FOR. An object at x = 0.37 is moved
    // 0.62 by the hand. With a 1-unit grid the SNAPPED gesture is "one unit",
    // so the object lands at 1.37 — it keeps whatever off-grid position it was
    // authored at. Quantising the absolute would teleport it to 1.00 the
    // instant the modifier came down, which is not what Ctrl does anywhere
    // else in this editor.
    {
        const vrgrab::Pose start{ iris::Vec3(0.37f, 0.0f, 0.0f), iris::Quat() };
        const vrgrab::Pose live{ iris::Vec3(0.99f, 0.0f, 0.0f), iris::Quat() };
        const vrgrab::Pose out = vrgrab::snappedFrom(start, live, 1.0f, 0.0f);
        show("snapped to a 1-unit grid after a 0.62 move", out.position);
        CHECK(nearVec(out.position, iris::Vec3(1.37f, 0, 0)),
              "a 0.62 move on a 1.0 grid snaps to 1.0: 0.37 -> 1.37, not 1.00");
        const vrgrab::Pose small{ iris::Vec3(0.77f, 0.0f, 0.0f), iris::Quat() };
        CHECK(nearVec(vrgrab::snappedFrom(start, small, 1.0f, 0.0f).position, start.position),
              "a 0.40 move snaps to zero — the object does not budge");
        // Rotation: a 37 degree delta on a 10 degree grid is 40 degrees.
        const vrgrab::Pose r0{ iris::Vec3(), yaw(12.0f) };
        const vrgrab::Pose r1{ iris::Vec3(), yaw(49.0f) };
        const vrgrab::Pose rs = vrgrab::snappedFrom(r0, r1, 0.0f, 10.0f);
        std::printf("      snapped heading = %.3f\n", double(vrorigin::yawDegrees(rs.rotation)));
        CHECK(near(vrorigin::yawDegrees(rs.rotation), 52.0f, 1e-2f),
              "a 37 degree turn on a 10 degree grid becomes 40: 12 -> 52, keeping the 12");
        CHECK(near(vrorigin::yawDegrees(vrgrab::snappedFrom(r0, r1, 0.0f, 0.0f).rotation), 49.0f,
                   1e-2f),
              "a zero step is no snap at all");
    }

    // ---- 9. THE FAR-GRAB FILTER ------------------------------------------
    {
        CHECK(near(vrgrab::leverTau(0.5f), 0.0f),
              "inside arm's reach there is NO filter (a near grab must feel direct)");
        CHECK(near(vrgrab::onePoleAlpha(1.0f / 90.0f, vrgrab::leverTau(0.5f)), 1.0f),
              "...so the virtual hand follows exactly");
        const float tau20 = vrgrab::leverTau(20.0f);
        std::printf("      tau at 20 m = %.4f s\n", double(tau20));
        CHECK(near(tau20, 0.382f, 1e-3f), "at 20 m the lever's time constant is 382 ms");
        const float a = vrgrab::onePoleAlpha(1.0f / 90.0f, tau20);
        CHECK(a > 0.0f && a < 0.05f, "so one 90 Hz frame moves it under 5 % of the way");
        // A LONG FRAME CANNOT OVERSHOOT — the exponential form's whole point.
        CHECK(vrgrab::onePoleAlpha(10.0f, tau20) <= 1.0f,
              "and a 10 second hitch still blends at most all the way, never past");
        const vrgrab::Pose from{ iris::Vec3(0, 0, 0), iris::Quat() };
        const vrgrab::Pose to{ iris::Vec3(2, 0, 0), yaw(90.0f) };
        const vrgrab::Pose half = vrgrab::smoothedTowards(from, to, 0.5f);
        CHECK(nearVec(half.position, iris::Vec3(1, 0, 0)),
              "a half blend is halfway there in position");
        CHECK(near(vrorigin::yawDegrees(half.rotation), 45.0f, 0.5f),
              "...and halfway in heading");
    }

    // ---- 10. THE SNAP TURN KEEPS THE HEAD WHERE IT IS --------------------
    //
    // The assertion this function exists for. A wearer standing a metre to the
    // side of their room's origin turns 30 degrees: their head must not move at
    // all, and their heading must move by exactly 30.
    {
        const iris::Vec3 pose(-1.0f, 1.6f, 0.4f);        // the runtime's own pose
        vrorigin::Rig rig;
        rig.position = iris::Vec3(12.0f, 0.0f, -5.0f);
        rig.yaw = 40.0f;
        // Compose the head the way the engine does (OgreVrSession: origin
        // translation * origin yaw * runtime pose).
        const iris::Vec3 head = rig.position + vrorigin::rotateY(pose, rig.yaw);
        show("the head in the world", head);
        const vrorigin::Rig out = vrgrab::turnedAboutHead(rig, head, 30.0f);
        const iris::Vec3 headAfter = out.position + vrorigin::rotateY(pose, out.yaw);
        show("the head after a 30 degree snap turn", headAfter);
        CHECK(nearVec(headAfter, head, 1e-3f),
              "a snap turn about the head leaves the head EXACTLY where it stood");
        CHECK(near(out.yaw, 70.0f, 1e-3f), "...and turns the room by exactly 30 degrees");
        // The cheap version, for the record: turning the rig's own origin moves
        // this wearer by more than a metre.
        vrorigin::Rig naive = rig;
        naive.yaw += 30.0f;
        const iris::Vec3 naiveHead = naive.position + vrorigin::rotateY(pose, naive.yaw);
        show("...where turning the RIG's origin would have put them", naiveHead);
        CHECK((naiveHead - head).length() > 0.5f,
              "(turning the rig's origin instead would have swung them 0.5 m+ sideways)");
        CHECK(!vrgrab::turnedAboutHead(rig, head, 0.0f).position.isNull(),
              "a zero turn is still a well-formed rig");
    }

    // ---- 11. SNAP AND SMOOTH TURN, AND THE RE-ARM ------------------------
    //
    // THE STEP AND THE RATE ARE PASSED (lane VR-WORLD-1): they are the
    // PROJECT's settings now (`world.vr`), so the pure functions carry no
    // default of their own and these cases state the numbers they assert.
    {
        // THE DOCUMENT'S OWN DEFAULTS, from the header that defines them
        // (they are `inline constexpr`, so naming them costs this target
        // nothing) — a re-literalised 30 and 90 here would be the second
        // definition VR-WORLD-1 deleted.
        const float snapStep = iris::kDefaultVrSnapTurnDegrees;                 // 30
        const float smoothRate = iris::kDefaultVrSmoothTurnDegreesPerSecond;    // 90/s
        CHECK(near(vrgrab::snapTurnDegrees(1.0f, true, snapStep), 30.0f),
              "a full stick right, armed, asks for +30 in the stick's own sign (the caller negates it into the tree's right-handed yaw, so the wearer turns RIGHT)");
        CHECK(near(vrgrab::snapTurnDegrees(-0.8f, true, snapStep), -30.0f),
              "...and left for -30, whatever the deflection past the dead zone");
        CHECK(near(vrgrab::snapTurnDegrees(1.0f, false, snapStep), 0.0f),
              "a stick held over from the last turn asks for nothing (one flick, one turn)");
        CHECK(near(vrgrab::snapTurnDegrees(0.3f, true, snapStep), 0.0f), "inside the dead zone: nothing");
        CHECK(!vrgrab::snapTurnRearmed(0.6f), "a stick still pushed does not re-arm");
        CHECK(vrgrab::snapTurnRearmed(0.1f), "...and one returned near centre does");
        CHECK(near(vrgrab::smoothTurnDegrees(1.0f, 0.25f, smoothRate), 22.5f),
              "smooth turn: a quarter second of full stick is 22.5 degrees");
        CHECK(near(vrgrab::smoothTurnDegrees(0.2f, 0.25f, smoothRate), 0.0f),
              "...with the same dead zone");
    }

    // ---- 12. THE STICK'S FLIGHT IS THE FLY KEYS' FLIGHT ------------------
    //
    // The equality that keeps the analog stick and the boolean keys from
    // drifting apart: at FULL cardinal deflection the two must agree exactly.
    {
        const iris::Quat headRot = yaw(37.0f) * pitch(-15.0f);
        const float speed = 8.0f, seconds = 1.0f / 60.0f;
        flystep::Keys fwd;
        fwd.forward = true;
        const iris::Vec3 byKeys = vrorigin::flyDelta(headRot, fwd, speed, seconds);
        const iris::Vec3 byStick = vrgrab::stickFlyDelta(headRot, 0.0f, 1.0f, speed, seconds);
        show("one frame by the fly key ", byKeys);
        show("one frame by the stick   ", byStick);
        // 1e-5, AND THE WORDS SAY SO (the Fable read of stage 1, finding 5).
        // The two are the SAME vector by construction and not the same
        // expression — the stick's goes through a deflection and a normalise —
        // so they legitimately differ in the last bits of a float. The right
        // fix is the claim, not the tolerance: a tolerance tightened to zero
        // here would be a suite that reds on a compiler's FMA.
        CHECK(nearVec(byKeys, byStick, 1e-5f),
              "a full-forward stick IS the forward fly key, to a hundredth of a millimetre");
        flystep::Keys right;
        right.right = true;
        CHECK(nearVec(vrorigin::flyDelta(headRot, right, speed, seconds),
                      vrgrab::stickFlyDelta(headRot, 1.0f, 0.0f, speed, seconds), 1e-5f),
              "...and a full-right stick IS the strafe key, to the same hundredth of a "
              "millimetre");
        // ANALOG: half deflection is half the distance, and the DIRECTION is
        // the stick's own rather than one of eight.
        const iris::Vec3 half = vrgrab::stickFlyDelta(headRot, 0.0f, 0.75f, speed, seconds);
        CHECK(near(half.length(), byStick.length() * 0.5f, 1e-4f),
              "0.75 of the stick past a 0.5 dead zone is half speed");
        const iris::Vec3 mostly = vrgrab::stickFlyDelta(headRot, 0.3f, 0.9f, speed, seconds);
        const iris::Vec3 level = vrorigin::levelForward(headRot);
        const float alongForward = iris::Vec3::dotProduct(mostly.normalized(), level);
        std::printf("      (0.3, 0.9) lies %.3f along the heading\n", double(alongForward));
        CHECK(alongForward > 0.93f && alongForward < 0.96f,
              "a (0.3, 0.9) stick flies MOSTLY forward, not at 45 degrees");
        CHECK(vrgrab::stickFlyDelta(headRot, 0.2f, 0.2f, speed, seconds).isNull(),
              "a stick inside the dead zone flies nowhere");
        // AND IT IS LEVEL: the head is pitched 15 degrees down and the wearer
        // must not dive into the floor (the rule vrorigin::levelForward exists
        // for; a wearer glancing at their feet walks forward, not down).
        CHECK(near(byStick.y(), 0.0f, 1e-6f),
              "and a pitched head still walks LEVEL (no y in the step)");
        // The eight-way reduction is still there for the callers that want it.
        const flystep::Keys k = vrgrab::keysFromStick(-0.9f, 0.7f);
        CHECK(k.left && k.forward && !k.right && !k.back,
              "keysFromStick reduces (-0.9, 0.7) to left+forward");
        CHECK(near(vrgrab::stickMagnitude(1.0f, 0.0f), 1.0f), "full deflection is magnitude 1");
        CHECK(near(vrgrab::stickMagnitude(1.0f, 1.0f), 1.0f),
              "...and a square-gated diagonal is clamped to 1, never 1.41");
    }


    // ---- 12. TWO HANDS: THE SPAN IS THE SIZE -----------------------------
    //
    // Hands 0.5 m apart about (0,1,0), object one metre in front of the
    // midpoint. Spread the palms to a metre and everything about the object
    // doubles AWAY FROM THE MIDPOINT: the offset (0,0,1) becomes (0,0,2), so
    // the object stands at (0,1,2), and the factor handed back is exactly 2.
    {
        const vrgrab::Pose l0{ iris::Vec3(-0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose r0{ iris::Vec3(0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose l1{ iris::Vec3(-0.5f, 1, 0), iris::Quat() };
        const vrgrab::Pose r1{ iris::Vec3(0.5f, 1, 0), iris::Quat() };
        const vrgrab::Pose node0{ iris::Vec3(0, 1, 1), iris::Quat() };
        float scale = 0.0f;
        const vrgrab::Pose out = vrgrab::twoHandFollow(l0, r0, l1, r1, node0, &scale);
        show("the object after the palms spread 0.5 -> 1.0 m", out.position);
        CHECK(near(scale, 2.0f), "the span's ratio IS the scale: 1.0 / 0.5 = 2");
        CHECK(nearVec(out.position, iris::Vec3(0, 1, 2)),
              "...and it scales about the midpoint AT THE CAPTURE: (0,1,1) -> (0,1,2)");
        CHECK(near(vrorigin::yawDegrees(out.rotation), 0.0f, 1e-2f),
              "a pure spread does not turn it at all");
        // A HOLD IS A HOLD: the identity case, which is what a wearer standing
        // still with both hands on something must see.
        float hold = 0.0f;
        const vrgrab::Pose still = vrgrab::twoHandFollow(l0, r0, l0, r0, node0, &hold);
        CHECK(near(hold, 1.0f, 1e-6f) && nearVec(still.position, node0.position, 1e-5f),
              "hands that have not moved leave it exactly where it was, at scale 1");
    }

    // ---- 13. TWO HANDS: THE MIDPOINT CARRIES IT --------------------------
    {
        const vrgrab::Pose l0{ iris::Vec3(-0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose r0{ iris::Vec3(0.25f, 1, 0), iris::Quat() };
        // Both palms 40 cm to the right and 10 cm up, span unchanged.
        const vrgrab::Pose l1{ iris::Vec3(0.15f, 1.1f, 0), iris::Quat() };
        const vrgrab::Pose r1{ iris::Vec3(0.65f, 1.1f, 0), iris::Quat() };
        const vrgrab::Pose node0{ iris::Vec3(0, 1, 1), iris::Quat() };
        float scale = 0.0f;
        const vrgrab::Pose out = vrgrab::twoHandFollow(l0, r0, l1, r1, node0, &scale);
        show("the object after the pair moved (0.4, 0.1, 0)", out.position);
        CHECK(near(scale, 1.0f, 1e-5f), "a pure translation of the pair is no scale");
        CHECK(nearVec(out.position, iris::Vec3(0.4f, 1.1f, 1)),
              "the translation is the MIDPOINT'S own move: M - M0");
    }

    // ---- 14. TWO HANDS: THE AXIS' MINIMAL ROTATION -----------------------
    //
    // The pair starts side by side along +X and ends fore-and-aft along -Z
    // (left hand forward). The minimal rotation carrying +X onto -Z is +90
    // degrees about +Y, under which the object's offset (0,0,1) goes to
    // (1,0,0) — so an object one metre in FRONT of the hands ends one metre to
    // their LEFT, having travelled round the midpoint rather than turned in
    // place.
    {
        const vrgrab::Pose l0{ iris::Vec3(-0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose r0{ iris::Vec3(0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose l1{ iris::Vec3(0, 1, 0.25f), iris::Quat() };
        const vrgrab::Pose r1{ iris::Vec3(0, 1, -0.25f), iris::Quat() };
        const vrgrab::Pose node0{ iris::Vec3(0, 1, 1), iris::Quat() };
        float scale = 0.0f;
        const vrgrab::Pose out = vrgrab::twoHandFollow(l0, r0, l1, r1, node0, &scale);
        show("the object after the pair turned 90 degrees", out.position);
        CHECK(near(scale, 1.0f, 1e-5f), "turning the pair is not a scale");
        CHECK(nearVec(out.position, iris::Vec3(1, 1, 0)),
              "the axis' 90 degree turn swings it round the midpoint: (0,0,1) -> (1,0,0)");
        CHECK(near(vrorigin::yawDegrees(out.rotation), 90.0f, 1e-2f),
              "...and turns the object itself by the same 90 degrees");
        // AND THE ROTATION IS THE MINIMAL ONE: no roll came out of a gesture
        // in which neither wrist rolled.
        const vrgrab::TwoHandStart start = vrgrab::twoHandStart(l0, r0);
        const vrgrab::TwoHandDelta d = vrgrab::twoHandDelta(start, l1, r1);
        CHECK(near(d.rollDegrees, 0.0f, 1e-3f),
              "a pair that turned without rolling reports zero roll");
    }

    // ---- 15. TWO HANDS: THE ROLL (owner answer 7, "with roll") -----------
    //
    // THE HALF A PAIR OF POINTS CANNOT SEE. Both palms stay exactly where they
    // are and both wrists roll 30 degrees about the line between them (+X), so
    // the span, the midpoint and the axis are all unchanged and the minimal
    // rotation is the identity. The object must roll 30 degrees about +X
    // THROUGH THE MIDPOINT: its offset (0,0,1) goes to (0,-sin30, cos30).
    {
        const vrgrab::Pose l0{ iris::Vec3(-0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose r0{ iris::Vec3(0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose l1{ iris::Vec3(-0.25f, 1, 0), pitch(30.0f) };
        const vrgrab::Pose r1{ iris::Vec3(0.25f, 1, 0), pitch(30.0f) };
        const vrgrab::Pose node0{ iris::Vec3(0, 1, 1), iris::Quat() };
        const vrgrab::TwoHandStart start = vrgrab::twoHandStart(l0, r0);
        const vrgrab::TwoHandDelta d = vrgrab::twoHandDelta(start, l1, r1);
        std::printf("      the pair's roll = %.3f degrees\n", double(d.rollDegrees));
        CHECK(near(d.rollDegrees, 30.0f, 1e-2f),
              "both wrists rolled 30 degrees about the axis -> the pair's roll is 30");
        const vrgrab::Pose out = vrgrab::twoHandFollow(start, d, node0);
        show("the object after a 30 degree roll of the pair", out.position);
        CHECK(nearVec(out.position, iris::Vec3(0, 1.0f - 0.5f, 0.8660254f)),
              "...and the object rolls about the midpoint: (0,0,1) -> (0,-0.5,0.866)");
        // ONE WRIST ROLLING IS HALF A ROLL — the gesture belongs to the pair.
        const vrgrab::TwoHandDelta halfRoll =
            vrgrab::twoHandDelta(start, l0, vrgrab::Pose{ r0.position, pitch(30.0f) });
        CHECK(near(halfRoll.rollDegrees, 15.0f, 1e-2f),
              "one wrist rolling 30 with the other still is a 15 degree roll (the average)");
        // THE AVERAGE IS TAKEN THE SHORT WAY ROUND: +179 and -179 are 180
        // apart from zero, not zero.
        CHECK(near(vrgrab::averageDegrees(179.0f, -179.0f), 180.0f, 1e-3f),
              "averageDegrees(+179, -179) is 180, not 0 (the wrap)");
        CHECK(near(vrgrab::twistDegreesAbout(pitch(30.0f), iris::Vec3(1, 0, 0)), 30.0f, 1e-3f),
              "twistDegreesAbout reads a 30 degree turn about its own axis as 30");
        CHECK(near(vrgrab::twistDegreesAbout(pitch(30.0f), iris::Vec3(0, 1, 0)), 0.0f, 1e-3f),
              "...and reads no twist at all about a perpendicular one");
    }

    // ---- 16. TWO HANDS: THE FAR PIVOT AND THE SNAPPED FACTOR -------------
    {
        const vrgrab::Pose l0{ iris::Vec3(-0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose r0{ iris::Vec3(0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose l1{ iris::Vec3(-0.5f, 1, 0), iris::Quat() };
        const vrgrab::Pose r1{ iris::Vec3(0.5f, 1, 0), iris::Quat() };
        // TEN METRES AWAY, GRABBED ON ITS NEAR FACE. About the hands' own
        // midpoint a doubling would throw it to twenty metres — the lever arm
        // again. The far arrangement pivots about the VIRTUAL HAND instead: the
        // point on the ray where the grab took hold, which is the very point
        // the ONE-hand far gesture already rotates the object about
        // (rigidFollow's hand). The lead's read, item 7: the two arrangements
        // pivot about one point, so a second hand joining a far grab changes
        // what the gesture can DO and not where it happens. (The first cut used
        // the node's ORIGIN, which is that point only when the ray happened to
        // hit the origin — i.e. nearly never.)
        const vrgrab::Pose node0{ iris::Vec3(0, 1, -10), iris::Quat() };
        const iris::Vec3 virtualHand(0, 1, -9);      // the hit on its near face
        const vrgrab::TwoHandStart start = vrgrab::twoHandStart(l0, r0);
        const vrgrab::TwoHandDelta d = vrgrab::twoHandDelta(start, l1, r1);
        const vrgrab::Pose about0 = vrgrab::twoHandFollow(start, d, node0);
        const vrgrab::Pose aboutHand = vrgrab::twoHandFollow(start, d, node0, virtualHand);
        show("a 10 m object doubled about the HANDS", about0.position);
        show("...and about the virtual hand at the hit", aboutHand.position);
        CHECK(nearVec(about0.position, iris::Vec3(0, 1, -20)),
              "about the hands, doubling a 10 m object moves it to 20 m (the lever)");
        CHECK(nearVec(aboutHand.position, iris::Vec3(0, 1, -11), 1e-5f),
              "about the hit, doubling moves its CENTRE by its own depth: -10 -> -11 (it grows "
              "away from the point the hand is holding it by)");
        // ...AND A TURN OF THE PAIR CARRIES IT ROUND THAT SAME POINT — the axis
        // +X onto -Z is +90 degrees about +Y (test 14), under which the offset
        // (0,0,-1) from the hit goes to (-1,0,0).
        const vrgrab::Pose l2{ iris::Vec3(0, 1, 0.25f), iris::Quat() };
        const vrgrab::Pose r2{ iris::Vec3(0, 1, -0.25f), iris::Quat() };
        const vrgrab::TwoHandDelta turned = vrgrab::twoHandDelta(start, l2, r2);
        const vrgrab::Pose spun = vrgrab::twoHandFollow(start, turned, node0, virtualHand);
        show("...and turned 90 degrees about the hit", spun.position);
        CHECK(nearVec(spun.position, iris::Vec3(-1, 1, -9), 1e-4f),
              "a 90 degree turn of the pair carries it round the HIT: (0,1,-10) -> (-1,1,-9)");
        // THE SNAP QUANTISES THE FACTOR, and never to nothing.
        CHECK(near(vrgrab::snappedScale(2.13f, 0.25f), 2.25f),
              "a factor of 2.13 snaps to 2.25 at a quarter step");
        CHECK(near(vrgrab::snappedScale(0.02f, 0.25f), 0.25f),
              "...and a factor below one step is ONE step, never zero");
        CHECK(near(vrgrab::snappedScale(2.13f, 0.0f), 2.13f), "step 0 is no snap");
    }

    // ---- 17. TWO HANDS: THE HAND-OFF DOES NOT MOVE IT --------------------
    //
    // THE ASSERTION BEHIND "no jump" (§5.1). A gesture runs on two hands, one
    // lets go, and the remaining hand's rigid follow is RE-CAPTURED from the
    // object exactly where the pair left it. The first frame after the hand-off
    // must therefore be a bit-for-bit hold — which is what this computes: the
    // pair's answer becomes the new node0, the remaining hand's pose becomes
    // the new hand0, and rigidFollow of an unmoved hand is the identity.
    {
        const vrgrab::Pose l0{ iris::Vec3(-0.25f, 1, 0), iris::Quat() };
        const vrgrab::Pose r0{ iris::Vec3(0.25f, 1, 0), yaw(10.0f) };
        const vrgrab::Pose l1{ iris::Vec3(-0.4f, 1.2f, -0.1f), pitch(12.0f) };
        const vrgrab::Pose r1{ iris::Vec3(0.55f, 1.05f, 0.2f), yaw(25.0f) };
        const vrgrab::Pose node0{ iris::Vec3(0, 1, 1), yaw(-30.0f) };
        const vrgrab::Pose afterPair = vrgrab::twoHandFollow(l0, r0, l1, r1, node0);
        // The left hand lets go; the right keeps holding, re-captured NOW.
        const vrgrab::Pose held = vrgrab::rigidFollow(r1, r1, afterPair);
        CHECK(nearVec(held.position, afterPair.position, 1e-5f),
              "the frame after one hand lets go is a bit-for-bit hold (no jump)");
        // ...and the hand then moves 10 cm, carrying it 10 cm, from THERE.
        const vrgrab::Pose r2{ r1.position + iris::Vec3(0, 0.1f, 0), r1.rotation };
        const vrgrab::Pose moved = vrgrab::rigidFollow(r1, r2, afterPair);
        CHECK(nearVec(moved.position, afterPair.position + iris::Vec3(0, 0.1f, 0), 1e-5f),
              "...and the one-hand follow carries on from where the pair left it");
    }

    // ---- 18. THE TELEPORT ARC: A THROWN MARKER ---------------------------
    //
    // THE HAND AT 1.4 M, HELD LEVEL, 10 m/s. Gravity brings the throw down to
    // the floor in sqrt(2*1.4/9.81) = 0.5343 s, by which time it has flown
    // 5.343 m along the aim — which is the number a wearer is judging when they
    // point at the floor in front of them.
    {
        const vrgrab::Pose aim{ iris::Vec3(0, 1.4f, 0), iris::Quat() };   // down -Z
        CHECK(nearVec(vrgrab::arcPoint(aim, 0.0f), aim.position, 1e-6f),
              "the arc starts at the hand");
        const float t = vrgrab::arcPlaneTime(aim, 0.0f);
        std::printf("      a level throw reaches the floor at t = %.4f s\n", double(t));
        CHECK(near(t, 0.53425f, 1e-4f), "a level throw from 1.4 m reaches y=0 at 0.5343 s");
        const iris::Vec3 landing = vrgrab::arcPoint(aim, t);
        show("the landing of a level throw", landing);
        CHECK(near(landing.y(), 0.0f, 1e-3f), "...on the plane, to the millimetre");
        CHECK(near(landing.z(), -5.3425f, 1e-3f), "...5.343 m along the aim");
        // AIMED UP 45 DEGREES it reaches further, and the root taken is the
        // DESCENDING one: an arc thrown upward crosses y=0 once, coming down.
        const vrgrab::Pose up{ iris::Vec3(0, 1.4f, 0), pitch(45.0f) };
        const float tUp = vrgrab::arcPlaneTime(up, 0.0f);
        const iris::Vec3 far = vrgrab::arcPoint(up, tUp);
        show("the landing of a 45 degree throw", far);
        CHECK(tUp > t, "aimed up, the throw stays in the air longer");
        CHECK(near(far.y(), 0.0f, 1e-3f), "...and still lands on the plane");
        CHECK(near(far.z(), -11.44f, 0.01f), "...11.44 m away (v^2 sin(2*45)/g + the height)");
        // AIMED AT THE SKY it never comes down inside the plane's own solve
        // only when there is no plane below: from BELOW a plane with no upward
        // speed there is no crossing at all.
        const vrgrab::Pose down{ iris::Vec3(0, 1.4f, 0), pitch(-80.0f) };
        CHECK(vrgrab::arcPlaneTime(down, 0.0f) > 0.0f, "aimed steeply down it lands at once");
        CHECK(vrgrab::arcPlaneTime(aim, 5.0f) < 0.0f,
              "a floor ABOVE a level throw is never reached (-1, not a guess)");
    }

    // ---- 19. THE LANDING'S SLOPE, AND THE RIG THAT ARRIVES ---------------
    {
        CHECK(vrgrab::landingAllowed(iris::Vec3(0, 1, 0)), "level ground is standable");
        CHECK(!vrgrab::landingAllowed(iris::Vec3(1, 0, 0)), "a vertical wall is not");
        // 44 degrees is a ramp and 46 is a wall — the line is 45 (every
        // engine's character controller uses it).
        const float c44 = std::cos(44.0f * 3.14159265f / 180.0f);
        const float s44 = std::sin(44.0f * 3.14159265f / 180.0f);
        CHECK(vrgrab::landingAllowed(iris::Vec3(s44, c44, 0)), "a 44 degree ramp is standable");
        const float c46 = std::cos(46.0f * 3.14159265f / 180.0f);
        const float s46 = std::sin(46.0f * 3.14159265f / 180.0f);
        CHECK(!vrgrab::landingAllowed(iris::Vec3(s46, c46, 0)), "a 46 degree face is not");
        CHECK(vrgrab::landingAllowed(iris::Vec3()),
              "a face whose normal we could not measure is NOT refused");

        // THE ARRIVAL. The wearer's room is at the origin, their head is 1.7 m
        // up and half a metre to the right of it, and they teleport to
        // (10, 0, -20). They must arrive standing on that point, facing exactly
        // the way they already faced: the rig moves by the head's own offset,
        // the yaw does not move at all, and the head ends over the landing.
        vrorigin::Rig rig;
        rig.position = iris::Vec3(0, 0, 0);
        rig.yaw = 33.0f;
        const iris::Vec3 head(0.5f, 1.7f, -1.0f);
        const iris::Quat headRot = yaw(33.0f);
        const vrorigin::Rig out =
            vrgrab::teleportedTo(rig, head, headRot, iris::Vec3(10, 0, -20));
        show("the rig after a teleport to (10, 0, -20)", out.position);
        CHECK(near(out.yaw, rig.yaw, 1e-4f), "a teleport does not turn the wearer");
        const iris::Vec3 newHead = out.position + (head - rig.position);
        show("...and the head then stands at", newHead);
        CHECK(nearVec(newHead, iris::Vec3(10, 1.7f, -20), 1e-4f),
              "the head ends over the landing point, at the same height above the floor");
        // A CROUCHING WEARER ARRIVES CROUCHING (the height is carried, not set).
        const iris::Vec3 low(0.5f, 0.9f, -1.0f);
        const vrorigin::Rig crouched =
            vrgrab::teleportedTo(rig, low, headRot, iris::Vec3(10, 0, -20));
        CHECK(near((crouched.position + (low - rig.position)).y(), 0.9f, 1e-4f),
              "...and a crouching one arrives at 0.9 m, not stood up");
    }

    std::printf(failures ? "vr.grab_maths: FAILED (%d)\n" : "vr.grab_maths: PASS\n", failures);
    return failures ? 1 : 0;
}
