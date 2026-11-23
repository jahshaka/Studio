// script.commands - THE VIEW FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2 B1; test_script_commands.cpp runs them, in its
// fixed order). THE TABLES (records_view.h: CameraDesc, PostFxDesc with ImageGrade and LookDesc): each in order, its
// default and a value with EVERY field moved round-tripping. THE DOMAINS: each command's declared domain refused by name
// on the host with no engine, before its body (a view there is a stand-in the body never reads: the engine is absent);
// THE CONTROL: the same call, in its domain, reaches the body ("the engine did not start"). THE HANDLES: a stale, a
// foreign and a wrong-kind view handle FAIL. ON THE HEADLESS ENGINE: it makes no view (its refusal, by name) - so every
// view command's EFFECT is a card case of the train (named to the lead), never asserted here.
#include "script_commands.h"
#include "script/records_view.h"

#include <cstring>

namespace scriptcommands {

namespace {

template <class R> bool roundTrips(const R &r, std::string &why)
{
    R back;
    if (!Record<R>::table().fromValue(toValue(r), back, "", why)) return false;
    if (!(back == r)) why = "the value read back differs";
    return back == r;
}
template <class R> void tableLaws(const char *name, const R &filled)
{
    std::string order, def, full;
    const bool inOrder = Record<R>::table().inOrder(order);
    const bool d = roundTrips(R{}, def), fl = roundTrips(filled, full);
    check(inOrder && d && fl && !(filled == R{}),
          std::string("THE TABLE ") + name + ": in declaration order (" + order + "), its default and a value with every "
              "field moved round-trip (" + def + full + ")");
}
Value obj(Value::Object o) { return Value(std::move(o)); }

}  // namespace

void casesView(Fixture &f)
{
    // 1. THE TABLES
    CameraDesc cam;
    cam.position = Vec3(1, 2, 3);
    cam.orientation = Quat(0.5f, 0.5f, 0.5f, 0.5f);
    cam.fovDegrees = 60;
    cam.nearClip = 0.25f;
    cam.farClip = 500;
    cam.orthographic = true;
    cam.orthoSize = 4;
    cam.constrainAspect = true;
    cam.aspect = 2;
    cam.framingAspect = 1.5f;
    cam.lensShiftX = 0.25f;
    cam.lensShiftY = -0.125f;
    tableLaws("CameraDesc", cam);
    LookDesc look;
    look.kind = LookKind::Posterize;
    for (int i = 0; i < 8; ++i) look.p[i] = 0.5f + float(i);
    tableLaws("LookDesc", look);
    ImageGrade grade;
    grade.contrast = 1.5f; grade.saturation = 0.5f; grade.shadows = 1; grade.highlights = -1;
    grade.whiteTemperature = 5000; grade.whiteTint = 0.25f; grade.vignette = 0.5f; grade.filmSlope = 0.75f;
    grade.filmToe = 0.5f; grade.filmShoulder = 0.5f; grade.filmBlackClip = 0.125f; grade.filmWhiteClip = 0.25f;
    tableLaws("ImageGrade", grade);
    PostFxDesc fx;
    fx.hdr = true; fx.exposure = 1.5f; fx.exposureMin = -1; fx.exposureMax = 1; fx.meterPattern = ExposureMeterPattern::Spot;
    fx.meterLowPercent = 5; fx.meterHighPercent = 95; fx.bloom = true; fx.bloomThreshold = 3; fx.bloomKnee = 1;
    fx.bloomAmount = 1.5f; fx.ssao = true; fx.ssaoScale = 0.5f; fx.ssaoPower = 2; fx.ssaoRadius = 1; fx.ditherOff = true;
    fx.smaaPreset = 2; fx.ssr = 2; fx.ssrMaxDistance = 10; fx.ssrSteps = 64; fx.ssrThickness = 0.25f; fx.ssrIntensity = 0.5f;
    fx.ssrMarchPhase = 1; fx.reflectionRoughnessCutoff = 0.5f; fx.refractions = true; fx.distortion = true;
    fx.distortionStrength = 2; fx.tonemapFixed = true; fx.exposureScale = 0.75f; fx.looks = {look, LookDesc{}};
    fx.hzb = true; fx.hzbFarthest = false; fx.allowOffscreen = true; fx.hdrReadback = true; fx.image = grade;
    tableLaws("PostFxDesc", fx);

    // 2. THE DOMAINS, on a host with no engine: a stand-in view (never read: the body FAILS on the absent engine
    //    first) carries each call to its arguments' laws. `view` is held OWNED by this host's command state (as
    //    engine.view.createOffscreen holds what it makes), `lent` unowned (what an owner hands in: lab.view())
    // (its identity read at the hold - View::id, Scene::serial: plain members - reads zeros; nothing else reads it)
    alignas(std::max_align_t) static unsigned char standIn[4096];
    Host &h = f.none;
    Host mine;
    mine.setConsole([](const std::string &) {});
    const Commands mk = makeEngineCommands(mine, nullptr, "no engine: the arguments' laws", false);
    View *const sv = reinterpret_cast<View *>(standIn);
    const Value view(mine.registry().hold(kView, ownedView(mk.st, sv)));
    alignas(std::max_align_t) static unsigned char lentIn[4096];   // (another address: the owned test reads the pointer)
    const Value lent(mine.registry().hold(kView, std::shared_ptr<View>(reinterpret_cast<View *>(lentIn), [](View *) {})));
    const auto call = [&](const char *name, Value::Array args) { return mine.call(name, std::move(args)); };
    const auto reached = [](const Result &r) { return failedWith(r, "the engine did not start"); };
    PostFxDesc fxGood = fx;   // (the filled chain without its two measuring arms: setPostFx's own domain)
    fxGood.ditherOff = false;
    fxGood.ssrSteps = 0;
    const auto looks = [](std::initializer_list<const char *> kinds) {
        Value::Array a;
        for (const char *k : kinds) a.push_back(Value(Value::Object{{"kind", Value(k)}}));
        return Value(std::move(a));
    };
    struct Domain { const char *name; Value::Array bad; std::string says; Value::Array good; };
    const Domain domains[] = {
        // THE CAMERA: the engine's own ranges (OgreView.cpp:1401, :1429, :1476), far past near (:1402), a rotation
        {"engine.view.setCamera", {view, obj({{"fovDegrees", Value(0.5)}})}, "camera.fovDegrees 0.5 is outside 1..179 deg",
         {view, Value(toValue(cam))}},
        {"engine.view.setCamera", {view, obj({{"fovDegrees", Value(179.5)}})}, "camera.fovDegrees 179.5 is outside 1..179 deg",
         {view, obj({{"fovDegrees", Value(179)}})}},
        {"engine.view.setCamera", {view, obj({{"nearClip", Value(0.0005)}})}, "camera.nearClip 0.0005 is outside",
         {view, obj({{"nearClip", Value(0.001)}})}},
        {"engine.view.setCamera", {view, obj({{"nearClip", Value(1)}, {"farClip", Value(1.005)}})},
         "camera.farClip 1.005000 is not 0.01 m past camera.nearClip 1.000000", {view, obj({{"nearClip", Value(1)}, {"farClip", Value(1.02)}})}},
        {"engine.view.setCamera", {view, obj({{"orientation", Value(Value::Array{Value(0), Value(0), Value(0), Value(0)})}})},
         "camera.orientation is a zero quaternion", {view, obj({{"orientation", Value(Value::Array{Value(0), Value(0), Value(0), Value(2)})}})}},
        // THE POST CHAIN: ranges, the laws between members, the measuring arms
        {"engine.view.setPostFx", {view, obj({{"ssr", Value(3)}})}, "desc.ssr 3 is outside 0..2", {view, Value(toValue(fxGood))}},
        {"engine.view.setPostFx", {view, obj({{"meterPattern", Value("matrix")}})}, "desc.meterPattern 'matrix' is not one of \"average\"|\"centreWeighted\"|\"spot\"",
         {view, obj({{"allowOffscreen", Value(true)}, {"ssr", Value(1)}})}},
        {"engine.view.setPostFx", {view, obj({{"looks", looks({"count"})}})}, "desc.looks[0].kind 'count' is not one of",
         {view, obj({{"looks", looks({"sharpen", "posterize"})}})}},
        {"engine.view.setPostFx", {view, obj({{"looks", Value(Value::Array(7, obj({})))}})},
         "desc.looks holds 7 elements, at most 6", {view, obj({})}},
        {"engine.view.setPostFx", {view, obj({{"looks", looks({"sharpen", "posterize", "sharpen"})}})},
         "desc.looks[2].kind 'sharpen' is already desc.looks[0]", {view, obj({{"looks", looks({"sharpen"})}})}},
        {"engine.view.setPostFx", {view, obj({{"exposureMin", Value(3)}})}, "desc.exposureMin 3.000000 is above desc.exposureMax 2.500000",
         {view, obj({{"exposureMin", Value(2.5)}})}},
        {"engine.view.setPostFx", {view, obj({{"meterLowPercent", Value(95)}})},
         "desc.meterLowPercent 95.000000 is above desc.meterHighPercent 90.000000", {view, obj({{"meterLowPercent", Value(90)}})}},
        {"engine.view.setPostFx", {view, obj({{"ditherOff", Value(true)}})}, "desc.ditherOff is a measuring arm", {view, obj({})}},
        {"engine.view.setPostFx", {view, obj({{"ssrSteps", Value(64)}})}, "desc.ssrSteps is a measuring arm", {view, obj({})}},
        {"engine.view.setPostFx", {view, obj({{"image", obj({{"vignette", Value(2)}})}})}, "desc.image.vignette 2 is outside 0..1",
         {view, obj({})}},
        {"engine.view.setPostFx", {view, obj({{"image", obj({{"whiteTemperature", Value(900)}})}})},
         "desc.image.whiteTemperature 900 is outside 1000..40000 K", {view, obj({{"image", obj({{"whiteTemperature", Value(1000)}})}})}},
        {"engine.view.setPostFx", {view, obj({{"image", obj({{"filmSlope", Value(0.005)}})}})}, "desc.image.filmSlope 0.005 is outside",
         {view, obj({{"image", obj({{"filmSlope", Value(0.01)}})}})}},
        // SIZE, SAMPLES, ENABLE, SHADERS: their domains on the view this host made
        {"engine.view.resize", {view, obj({{"width", Value(0)}, {"height", Value(8)}})}, "size.width 0 is outside 1..16384 px",
         {view, obj({{"width", Value(8)}, {"height", Value(8)}})}},
        {"engine.view.setSampleCount", {view, Value(17)}, "samples 17 is outside 1..16", {view, Value(4)}},
        {"engine.view.setSampleCount", {view, Value(3)}, "samples 3 is not a power of two", {view, Value(16)}},
        {"engine.view.setShadows", {view, Value(1)}, "on must be bool (it is int)", {view, Value(true)}},
        {"engine.view.setEnabled", {view, Value("yes")}, "on must be bool (it is string)", {view, Value(false)}},
        {"engine.view.setAsyncShaders", {view, Value(0)}, "on must be bool (it is int)", {view, Value(true)}},
        // A VIEW LENT TO THE SCRIPT (the audit, 3): its size, samples, enable and shaders are its owner's
        {"engine.view.resize", {lent, obj({{"width", Value(8)}, {"height", Value(8)}})},
         "this view was handed in by the host's owner: its size is the owner's to set", {view, obj({{"width", Value(8)}, {"height", Value(8)}})}},
        {"engine.view.setSampleCount", {lent, Value(4)}, "this view was handed in by the host's owner: its sample count is the owner's to set",
         {view, Value(4)}},
        {"engine.view.setEnabled", {lent, Value(false)}, "this view was handed in by the host's owner: its enable is the owner's to set",
         {view, Value(false)}},
        {"engine.view.setAsyncShaders", {lent, Value(true)},
         "this view was handed in by the host's owner: its asynchronous shaders is the owner's to set", {view, Value(true)}},
        {"engine.view.requestFrameCapture", {view, obj({{"presented", Value(true)}})}, "capture has no member 'presented'",
         {view, obj({{"alsoPresented", Value(true)}})}},
        {"engine.view.takeFrameCapture", {view, obj({{"wait", Value(2)}})}, "take.wait must be bool (it is int)", {view, obj({})}},
        {"engine.view.readPixels", {obj({{"view", view}, {"content", Value("depth")}})}, "request.content 'depth' is not one of",
         {obj({{"view", view}, {"content", Value("radiance")}})}},
        // ONE SHAPE FOR A COLOUR (the lead, 2026-11-23): four numbers, r g b a - three are refused by the count check
        {"engine.view.createOffscreen",
         {obj({{"name", Value("v")}, {"width", Value(8)}, {"height", Value(8)},
               {"background", Value(Value::Array{Value(0.1), Value(0.1), Value(0.1)})}})},
         "desc.background must hold 4 elements (it holds 3)",
         {obj({{"name", Value("v")}, {"width", Value(8)}, {"height", Value(8)},
               {"background", Value(Value::Array{Value(0.1), Value(0.1), Value(0.1), Value(1.0)})}})}},
    };
    for (const Domain &d : domains) {
        const Result bad = call(d.name, d.bad), good = call(d.name, d.good);
        check(failedWith(bad, std::string(d.name) + ": ") && bad.message.find(d.says) != std::string::npos && reached(good),
              std::string("THE DOMAIN of ") + d.name + " refused by name (" + bad.message + "); THE CONTROL: in its domain "
                  "the call reaches the body (" + good.message + ")");
    }
    // the reads and the take carry no domain beyond the view - and a lent view is the script's to drive (Script.h)
    check(reached(call("engine.view.isOffscreen", {view})) && reached(call("engine.view.frameCaptureState", {view})) &&
              reached(call("engine.view.setShadows", {lent, Value(true)})) &&
              reached(call("engine.view.setPostFx", {lent, obj({})})),
          "engine.view.isOffscreen and frameCaptureState reach their body; a LENT view's shadows and post chain are the "
          "script's to set (they reach the body)");

    // THE MEASURING ARMS' TEST DOOR: closed without --test-doors; with them, 1..7 steps FAIL by name, 64 reaches the body
    {
        Host doors;
        doors.setConsole([](const std::string &) {});
        const Commands dk = makeEngineCommands(doors, nullptr, "no engine: the arms' laws", true);
        const Value dv(doors.registry().hold(kView, ownedView(dk.st, sv)));
        const Result closed = call("engine.view.setPostFxArms", {view, obj({{"ditherOff", Value(true)}})});
        const Result few = doors.call("engine.view.setPostFxArms", {dv, obj({{"ssrSteps", Value(4)}})});
        const Result ok = doors.call("engine.view.setPostFxArms", {dv, obj({{"ditherOff", Value(true)}, {"ssrSteps", Value(64)}})});
        check(failedWith(closed, "engine.view.setPostFxArms: a test door") &&
                  failedWith(few, "arms.ssrSteps 4 is neither 0 nor in 8..128") && reached(ok),
              "engine.view.setPostFxArms: a test door, refused without --test-doors (" + closed.message + "); with them "
              "ssrSteps 4 FAILS by name (" + few.message + "); THE CONTROL: ditherOff on and 64 steps reach the body");
    }

    // 3. THE HANDLES: a stale view (released), a foreign one (another host's), a wrong kind (a scene's) FAIL by name
    const auto hcall = [&](const char *name, Value::Array args) { return h.call(name, std::move(args)); };
    const Value stale(h.registry().hold(kView, std::shared_ptr<View>(reinterpret_cast<View *>(standIn), [](View *) {})));
    h.registry().release(stale.asInt());
    const Value foreign(f.outside.registry().hold(kView, std::shared_ptr<View>(reinterpret_cast<View *>(standIn), [](View *) {})));
    const Value sceneHandle(h.registry().hold(kScene, std::shared_ptr<Scene>(reinterpret_cast<Scene *>(standIn), [](Scene *) {})));
    const Result rs = hcall("engine.view.setShadows", {stale, Value(true)});
    const Result rf = hcall("engine.view.setShadows", {foreign, Value(true)});
    const Result rk = hcall("engine.view.setShadows", {sceneHandle, Value(true)});
    check(failedWith(rs, "engine.view.setShadows: view:") && failedWith(rf, "engine.view.setShadows: view:") &&
              failedWith(rk, "is a scene handle, not a view handle"),
          "A STALE, A FOREIGN AND A WRONG-KIND VIEW HANDLE FAIL by name (" + rs.message + " / " + rf.message + " / " +
              rk.message + ")");
    h.registry().release(sceneHandle.asInt());
    f.outside.registry().release(foreign.asInt());

    // 4. THE HEADLESS ENGINE MAKES NO VIEW: every view command's effect is the train's card case
    if (f.engine) {
        const Result v = f.headless.call("engine.view.createOffscreen",
                                         {obj({{"name", Value("r-headless")}, {"width", Value(8)}, {"height", Value(8)}})});
        check(v.status == Status::Refused && v.message.find("headless") != std::string::npos,
              "the headless (NULL) engine refuses a view by name (" + v.message + "): the view commands' effects are "
              "card cases of the train");
    }
}

}  // namespace scriptcommands
