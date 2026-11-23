// script.commands - THE SCENE FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2, builder P; test_script_commands.cpp runs them,
// in its fixed order): the scene's settings (the sky, the Sky Light's gain, the ambient SH, the fog, the shadow request,
// the LOD bias) and engine.mesh.create - each declared domain refused by name on the null engine, every table in order
// and round-tripped (SkyDesc's texture handles minted and taken back on the headless engine), a handed-in scene's
// settings not this host's, the stale / foreign / wrong-kind texture handle, and the effect on the headless engine.
#include "script_commands.h"
#include "script/records_scene.h"
#include "jahshaka/engine/Materials.h"

namespace scriptcommands {

namespace {

/// A record of T through a registry's check and back (C++ -> value -> the registry's conformance -> C++ -> value):
/// equal to the value it started from, or `why` says the first fault.
template <class T> bool roundTrips(const T &record, std::string &why)
{
    Registry reg;
    T seen{};
    const Value sent = Record<T>::table().toValue(record);
    reg.add({"test.record", {Access::Reads, {Record<T>::table().param("r")}, Kind::Object}, "the record, through C++"},
            [&seen](Call &c) -> Value {
                std::string w;
                if (!Record<T>::table().fromValue(c[0], seen, "r", w)) return c.fail(w);
                return Record<T>::table().toValue(seen);
            });
    const Result r = reg.call("test.record", {sent});
    if (!reg.validate().empty()) why = reg.validate()[0];
    else if (r.status != Status::Ok) why = r.message;
    else if (r.value != sent) why = "the value came back changed";
    else return true;
    return false;
}

template <class T> void inOrder(const char *name, std::string &missed)
{
    std::string why;
    if (!Record<T>::table().inOrder(why)) missed += std::string("\n      ") + name + ": " + why;
}

Value nums(std::initializer_list<double> v)
{
    Value::Array a;
    for (const double x : v) a.push_back(Value(x));
    return Value(std::move(a));
}

}  // namespace

void casesScene(Fixture &f)
{
    if (!f.engine) {
        check(false, "THE SCENE FAMILY: the headless engine did not boot - its cases are skipped");
        return;
    }
    const Value scene = f.headless.call("engine.scene.create", {Value("p-scene")}).value;
    Scene *raw = f.headless.registry().object<Scene>(scene.asInt());
    const std::int64_t noneScene = f.none.registry().hold<Scene>(kScene, std::shared_ptr<Scene>(raw, [](Scene *) {}));
    const Value ns(noneScene);
    // a 2x2 texture made in C++ (createTexture is the material family's command), named as a handle of each host
    const unsigned char texels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const TextureId tex = raw->createTexture(2, 2, texels, true);
    f.headless.registry().add({"test.p.name", {Access::Test, {object("id", {arg("kind", Kind::String), arg("id", Kind::Int)})},
                                               Kind::Int},
                               "an engine id made in C++ as this host's handle of its kind"},
                              [](Call &c) { return c.name(c[0]["kind"].asString(), unsigned(c[0]["id"].asInt())); });
    const auto named = [&f](const char *kind, unsigned id) {
        f.headless.registry().openTestDoors(true);
        const Value h = f.headless.call("test.p.name", {Value(Value::Object{{"kind", Value(kind)}, {"id", Value(id)}})}).value;
        f.headless.registry().openTestDoors(false);
        return h;
    };
    const auto texture = [&named](TextureId id) { return named(kTexture, id); };
    const Value texH = texture(tex);

    // 1. THE DECLARED DOMAIN on the null engine, each FAILS by name before the body; THE CONTROL: arguments in their
    //    domain reach the body (no engine: FAILS so)
    {
        const struct { const char *command; Value arg; const char *want; } cases[] = {
            {"engine.scene.setSky", Value(Value::Object{{"mode", Value("galaxy")}}),
             "sky.mode 'galaxy' is not one of \"noSky\"|\"equirectangular\"|\"cubemap\"|\"atmosphere\""},
            {"engine.scene.setSky", Value(Value::Object{{"faces", nums({0, 0, 0, 0, 0})}}), "sky.faces must hold 6 elements (it holds 5)"},
            {"engine.scene.setSky", Value(Value::Object{{"atmosphere", Value(Value::Object{{"groundAlbedo", Value(2.0)}})}}),
             "sky.atmosphere.groundAlbedo 2 is outside 0..1"},
            {"engine.scene.setSky", Value(Value::Object{{"atmosphere", Value(Value::Object{{"planetRadiusKm", Value(0.0)}})}}),
             "sky.atmosphere.planetRadiusKm 0 is outside > 0 km"},
            {"engine.scene.setSky", Value(Value::Object{{"sun", Value(Value::Object{{"dir", nums({0, 2, 0})}})}}),
             "sky.sun.dir[1] 2 is outside -1..1"},
            {"engine.scene.setSky", Value(Value::Object{{"clouds", Value(Value::Object{{"coverage", Value(1.5)}})}}),
             "sky.clouds.coverage 1.5 is outside 0..1"},
            {"engine.scene.setEnvironmentLight", nums({1, 1, 1}), "gain must hold 4 elements (it holds 3)"},
            {"engine.scene.setEnvironmentLight", nums({1, -1, 1, 1}), "gain[1] -1 is outside >= 0"},
            {"engine.scene.setAmbientSh", nums({1, 1, 1}), "sh must hold 27 elements (it holds 3)"},
            // A COLOUR IS FOUR NUMBERS (the tail: E0's setAmbient took 3) - three are refused by the registry's count check
            {"engine.scene.setAmbient", Value(Value::Object{{"upper", nums({1, 1, 1})}, {"lower", nums({0, 0, 0, 1})}}),
             "ambient.upper must hold 4 elements (it holds 3)"},
            {"engine.scene.setAmbient", Value(Value::Object{{"upper", nums({1, 1, 1, 1})}, {"lower", nums({0, 0, 0})}}),
             "ambient.lower must hold 4 elements (it holds 3)"},
            {"engine.scene.setFog", Value(Value::Object{{"density", Value(-0.5)}}), "fog.density -0.5 is outside >= 0"},
            {"engine.scene.setFog", Value(Value::Object{{"colour", nums({1, 1, 1})}}), "fog.colour must hold 4 elements (it holds 3)"},
            {"engine.scene.setShadowSettings", Value(Value::Object{{"filter", Value("blurry")}}),
             "shadows.filter 'blurry' is not one of \"hard\"|\"soft\"|\"verySoft\""},
            {"engine.scene.setShadowSettings", Value(Value::Object{{"resolution", Value(9000)}}), "shadows.resolution 9000 is outside 0..8192 px"},
            {"engine.scene.setShadowSettings", Value(Value::Object{{"resolution", Value(100)}}),
             "shadows.resolution 100 is neither 0 (no opinion) nor 256..8192 px"},
            {"engine.scene.setLodBias", Value(-1.0), "bias -1 is outside >= 0"},
            {"engine.mesh.create", Value(Value::Object{{"blendWeights", nums({0.5, 2})}}), "mesh.blendWeights[1] 2 is outside 0..1"},
            {"engine.mesh.create", Value(Value::Object{{"lodIndices", Value(Value::Array{nums({0, 1, 2})})}}),
             "mesh.lodIndices[0] must be bytes (it is array)"},
            {"engine.mesh.create", Value(Value::Object{{"cards", Value(Value::Array{Value(Value::Object{{"axis", Value(6)}})})}}),
             "mesh.cards[0].axis 6 is outside 0..5"},
        };
        std::string missed;
        for (const auto &d : cases) {
            const Result r = f.none.call(d.command, {ns, d.arg});
            if (!failedWith(r, std::string(d.command) + ": " + d.want))
                missed += std::string("\n      ") + d.command + ": want '" + d.want + "' <- " + r.message;
        }
        std::string reached;
        for (const auto &[command, arg] : std::initializer_list<std::pair<const char *, Value>>{
                 {"engine.scene.setSky", Value(Value::Object{{"mode", Value("atmosphere")}})},
                 {"engine.scene.setEnvironmentLight", nums({1, 1, 1, 1})},
                 {"engine.scene.setAmbient", Value(Value::Object{{"upper", nums({1, 1, 1, 1})}, {"lower", nums({0, 0, 0, 1})}})},
                 {"engine.scene.setFog", Value(Value::Object{{"enabled", Value(true)}})},
                 {"engine.mesh.create", Value(Value::Object{})}}) {
            const Result r = f.none.call(command, {ns, arg});
            if (!failedWith(r, "the engine did not start")) reached += std::string("\n      ") + command + ": " + r.message;
        }
        check(missed.empty() && reached.empty(),
              "THE SCENE FAMILY's DECLARED DOMAIN, each refused by name on the null engine (" + std::to_string(std::size(cases)) +
                  " cases); THE CONTROL: arguments in their domain reach the body (no engine there)" + missed + reached);
    }

    // 1b. THE SHADOW REQUEST IS ENGINE-WIDE (the audit's finding 3: OgreScene::setShadowSettings writes the engine's ONE
    //     filter and atlas - "the last scene to ask owns them"): a host for an outside client refuses it by name, before
    //     anything; THE CONTROL: a host that serves no outside client reaches the engine (absent here: FAILS so)
    {
        const std::int64_t outsideScene = f.outside.registry().hold<Scene>(kScene, std::shared_ptr<Scene>(raw, [](Scene *) {}));
        const Value request(Value::Object{{"hasFilter", Value(true)}, {"filter", Value("hard")}, {"resolution", Value(8192)}});
        const Result outside = f.outside.call("engine.scene.setShadowSettings", {Value(outsideScene), request});
        const Result inside = f.none.call("engine.scene.setShadowSettings", {ns, request});
        check(failedWith(outside, "engine.scene.setShadowSettings: engine-wide: this host serves an outside client") &&
                  failedWith(inside, "the engine did not start"),
              "engine.scene.setShadowSettings is ENGINE-WIDE: an outside client's host refuses it (" + outside.message +
                  "); THE CONTROL: a host for no outside client reaches the engine (" + inside.message + ")");
        f.outside.registry().release(outsideScene);
    }

    // 2. THE TABLES: every record in declaration order; the default and a record with every field changed round-trip
    {
        std::string missed;
        inOrder<AtmosphereSky>("AtmosphereSky", missed);
        inOrder<SunDisc>("SunDisc", missed);
        inOrder<CloudLayerDesc>("CloudLayerDesc", missed);
        inOrder<SkyDesc>("SkyDesc", missed);
        inOrder<FogDesc>("FogDesc", missed);
        inOrder<ShadowDesc>("ShadowDesc", missed);
        inOrder<MeshCardDesc>("MeshCardDesc", missed);
        inOrder<MeshCluster>("MeshCluster", missed);
        inOrder<MeshClusterGroup>("MeshClusterGroup", missed);
        inOrder<MeshData>("MeshData", missed);
        SkyDesc sky;
        sky.mode = SkyMode::Atmosphere;
        sky.atmosphere.sunHaze = 20.0f; sky.atmosphere.rayleighScale = 2.0f; sky.atmosphere.ozone = false;
        sky.atmosphere.groundAlbedo = 0.5f; sky.atmosphere.aerialScale = 0.5f; sky.atmosphere.skyBrightness = 2.0f;
        sky.atmosphere.planetRadiusKm = 6000.0f; sky.atmosphere.atmosphereHeightKm = 80.0f;
        sky.atmosphere.sunDir[0] = 0.5f; sky.atmosphere.hasSun = true; sky.atmosphere.sunIlluminance = Colour(1, 2, 3, 1);
        sky.sun.enabled = true; sky.sun.dir[2] = 0.5f; sky.sun.angularDiameterDeg = 1.0f; sky.sun.colour = Colour(4, 5, 6, 1);
        sky.sun.inProbes = true;
        sky.clouds.enabled = true; sky.clouds.coverage = 0.25f; sky.clouds.density = 2.0f; sky.clouds.altitude = 1500.0f;
        sky.clouds.shadow = 0.5f; sky.clouds.wind[0] = 3.0f; sky.clouds.wind[1] = -2.0f; sky.clouds.hasSun = true;
        sky.clouds.sunDir[1] = -0.5f; sky.clouds.sunIrradiance = Colour(0.5f, 0.5f, 0.5f, 1);
        FogDesc fog{true, Colour(0.1f, 0.2f, 0.3f, 1), 0.5f, 0.25f, 0.5f, 2.0f};
        ShadowDesc shadows{true, ShadowFilter::VerySoft, 2048};
        MeshData mesh;
        mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        mesh.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
        mesh.uvs = {0, 0, 1, 0, 0, 1};
        mesh.tangents = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
        mesh.indices = {0, 1, 2};
        mesh.blendIndices = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
        mesh.blendWeights = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
        mesh.dynamic = true;
        mesh.lodIndices = {{0, 1, 2}, {0, 0, 0}};
        mesh.lodErrors = {0.5f, 1.0f};
        mesh.lodBounds = {0.25f, 0.75f};
        mesh.cards = {MeshCardDesc{3, 1, Vec3(1, 2, 3), 0.5f, 0.25f, 0.125f}};
        mesh.cardCoverage = 0.75f;
        mesh.clusterIndices = {0, 1, 2};
        mesh.clusters = {MeshCluster{0, 3, 0, -1, {1, 2, 3}, 4}};
        mesh.clusterGroups = {MeshClusterGroup{1, {1, 2, 3}, 4, 0.5f, 0.25f}};
        std::string why;
        const auto trip = [&](bool ok, const char *name) { if (!ok) missed += std::string("\n      ") + name + ": " + why; };
        trip(roundTrips(SkyDesc{}, why), "SkyDesc default");
        trip(roundTrips(sky, why), "SkyDesc filled");
        trip(roundTrips(FogDesc{}, why), "FogDesc default");
        trip(roundTrips(fog, why), "FogDesc filled");
        trip(roundTrips(ShadowDesc{}, why), "ShadowDesc default");
        trip(roundTrips(shadows, why), "ShadowDesc filled");
        trip(roundTrips(MeshData{}, why), "MeshData default");
        trip(roundTrips(mesh, why), "MeshData filled");
        check(missed.empty(), "THE SCENE FAMILY's TABLES: AtmosphereSky, SunDisc, CloudLayerDesc, SkyDesc, FogDesc, "
                              "ShadowDesc, MeshCardDesc, MeshCluster, MeshClusterGroup and MeshData in declaration order; "
                              "SkyDesc, FogDesc, ShadowDesc and MeshData - default and every field changed, their nested "
                              "records with them - round-trip through the registry" + missed);
    }

    // 3. SKYDESC's TEXTURE HANDLES on the headless engine: a sky's faces, equirect and weather map given as this host's
    //    texture handles come back as the same handles (minted through the answering call), a none face as null
    {
        f.headless.registry().add({"test.p.sky", {Access::Test, {target(handle("scene", kScene)), Record<SkyDesc>::table().param("sky")},
                                                  Kind::Object}, "a sky through C++, answered with its handles"},
                                  [](Call &c) -> Value {
                                      SkyDesc d;
                                      std::string w;
                                      if (!Record<SkyDesc>::table().fromValue(c[1], d, "sky", w)) return c.fail(w);
                                      return Record<SkyDesc>::table().toValue(d, &c);
                                  });
        const Value sent(Value::Object{{"mode", Value("cubemap")}, {"equirect", texH},
                                       {"faces", Value(Value::Array{texH, Value(), texH, Value(0), texH, texH})},
                                       {"clouds", Value(Value::Object{{"weatherMap", texH}})}});
        f.headless.registry().openTestDoors(true);
        const Result back = f.headless.call("test.p.sky", {scene, sent});
        f.headless.registry().openTestDoors(false);
        const bool same = back.status == Status::Ok && back.value["equirect"] == texH &&
                          back.value["faces"] == Value(Value::Array{texH, Value(), texH, Value(), texH, texH}) &&
                          back.value["clouds"]["weatherMap"] == texH;
        check(same, "SKYDESC's TEXTURE HANDLES round-trip on the headless engine: the equirect, five faces and the weather "
                    "map come back as the same handle, a none face (null or 0) as null" +
                        (back.status == Status::Ok ? std::string() : " - " + back.message));
    }

    // 4. ON THE HEADLESS ENGINE every setting answers on a scene this host made; a mesh made from flat arrays draws at a
    //    node; arrays that do not agree are REFUSED by the engine (its words); a frame drains the fog and the shadow
    //    request with no refusal
    {
        std::string missed;
        const auto ok = [&](const char *command, Value arg, Value want = Value()) {
            const Result r = f.headless.call(command, {scene, std::move(arg)});
            if (r.status != Status::Ok || r.value != want) missed += std::string("\n      ") + command + ": " + r.message;
        };
        // A DRAWN SKY DOES NOT ANSWER HEADLESS (measured 2026-11-23: the NULL engine stages no sky material - Ogre's
        // "Unable to derive resource group for Jahshaka/AtmosphereSky", "the sky bundle ... Samples/Media/Common"): the
        // command REFUSES with the engine's own words, false; no sky answers true. A drawn sky is the train's card case.
        const Result atmosphere = f.headless.call("engine.scene.setSky", {scene, Value(Value::Object{{"mode", Value("atmosphere")}})});
        const Result equirect =
            f.headless.call("engine.scene.setSky", {scene, Value(Value::Object{{"mode", Value("equirectangular")}, {"equirect", texH}})});
        for (const Result *r : {&atmosphere, &equirect})
            if (r->status != Status::Refused || r->value != Value(false) || r->message.find("engine.scene.setSky: ") != 0 ||
                r->message.size() < 30)
                missed += "\n      a drawn sky not refused by the engine's words: " + r->message;
        ok("engine.scene.setSky", Value(Value::Object{}), Value(true));
        ok("engine.scene.setEnvironmentLight", nums({0.5, 0.5, 0.5, 1}));
        Value::Array sh(27, Value(0.0));
        sh[0] = sh[1] = sh[2] = Value(0.25);
        ok("engine.scene.setAmbientSh", Value(std::move(sh)));
        ok("engine.scene.setFog", Value(Value::Object{{"enabled", Value(true)}, {"density", Value(0.05)}}));
        ok("engine.scene.setShadowSettings", Value(Value::Object{{"hasFilter", Value(true)}, {"resolution", Value(1024)}}));
        ok("engine.scene.setLodBias", Value(2.0));
        // (the fog and the shadow request are a frame record's fields: what they draw has no headless read - no shadow
        // atlas, no fog pass - the train's card case; here the frame that takes them answers)
        const Result frame = f.headless.call("engine.frames", {Value(1)});
        const Value tri(Value::Object{{"positions", nums({0, 0, 0, 1, 0, 0, 0, 1, 0})}, {"normals", nums({0, 0, 1, 0, 0, 1, 0, 0, 1})},
                                      {"indices", nums({0, 1, 2})}});
        const Result mesh = f.headless.call("engine.mesh.create", {scene, tri});
        const Value node = f.headless.call("engine.node.create", {scene}).value;
        const MaterialId mat = raw->createPbrMaterial(PbrParams{});   // (C++: the material family's command is not this case's)
        const Value material = named(kMaterial, mat);
        const Result drawn =
            f.headless.call("engine.node.attachMesh", {scene, node, Value(Value::Object{{"mesh", mesh.value}, {"material", material}})});
        const Result bad = f.headless.call("engine.mesh.create",
                                           {scene, Value(Value::Object{{"positions", nums({0, 0, 0, 1, 0, 0, 0, 1, 0})},
                                                                       {"indices", nums({0, 1, 7})}})});
        check(missed.empty() && frame.status == Status::Ok && mesh.status == Status::Ok &&
                  !mesh.value.isNull() && drawn.value == Value(true) && bad.status == Status::Refused && bad.value == Value(0),
              "ON THE HEADLESS ENGINE the scene's settings answer (no sky; the Sky Light; the ambient SH; the LOD bias), a "
              "drawn sky is REFUSED in the engine's words (" + atmosphere.message.substr(0, 90) + "...), the fog and the shadow request are taken by a frame (" + frame.message + "); a mesh from flat arrays draws at a node (" +
                  drawn.message + "); an index past the vertices is REFUSED by the engine (" + bad.message + ")" + missed);
        f.headless.call("engine.node.destroy", {scene, node});
        f.headless.call("engine.mesh.destroy", {scene, mesh.value});
        raw->destroyMaterial(mat);
    }

    // 5. A HANDED-IN SCENE's SETTINGS are its owner's: each setting FAILS by name there - the direct setters and the
    //    frame record's alike; THE CONTROL: a mesh may still be MADE there (the script's own object)
    {
        Scene *owners = f.engine->createScene("p-scene-owners");
        f.headless.registry().add({"test.p.handScene", {Access::Test, {}, Kind::Int, kScene}, "the owner's scene, handed in"},
                                  [owners](Call &c) { return c.hold(kScene, std::shared_ptr<Scene>(owners, [](Scene *) {})); });
        f.headless.registry().openTestDoors(true);
        const Value handed = f.headless.call("test.p.handScene", {}).value;
        f.headless.registry().openTestDoors(false);
        std::string missed;
        for (const auto &[command, arg] : std::initializer_list<std::pair<const char *, Value>>{
                 {"engine.scene.setSky", Value(Value::Object{})},
                 {"engine.scene.setEnvironmentLight", nums({1, 1, 1, 1})},
                 {"engine.scene.setAmbientSh", Value(Value::Array(27, Value(0.0)))},
                 {"engine.scene.setFog", Value(Value::Object{})},
                 {"engine.scene.setShadowSettings", Value(Value::Object{})},
                 {"engine.scene.setLodBias", Value(1.0)}}) {
            const Result r = f.headless.call(command, {handed, arg});
            if (!failedWith(r, "the scene 'p-scene-owners' was handed in by the host's owner: its settings are not this host's to change"))
                missed += std::string("\n      ") + command + ": " + r.message;
        }
        const Result made = f.headless.call("engine.mesh.create",
                                            {handed, Value(Value::Object{{"positions", nums({0, 0, 0, 1, 0, 0, 0, 1, 0})},
                                                                         {"indices", nums({0, 1, 2})}})});
        check(missed.empty() && made.status == Status::Ok,
              "A HANDED-IN SCENE's SETTINGS (six commands: the sky, the Sky Light, the ambient SH, the fog, the shadow "
              "request, the LOD bias) each FAIL by name; THE CONTROL: the script makes its own mesh there (" + made.message +
                  ")" + missed);
        f.headless.call("engine.mesh.destroy", {handed, made.value});
        f.headless.registry().release(handed.asInt());
        f.engine->destroyScene(owners);
    }

    // 6. A SKY's TEXTURE HANDLE: a destroyed texture, another host's texture handle and a mesh given as a texture each
    //    FAIL by name, naming the member
    {
        const TextureId dead = raw->createTexture(2, 2, texels, true);
        const Value deadH = texture(dead);
        raw->destroyTexture(dead);
        const Value mesh = f.headless.call("engine.mesh.unitCube", {scene}).value;
        const Value foreignH(f.none.registry().name(kTexture, tex));
        const auto sky = [&](Value h) {
            return f.headless.call("engine.scene.setSky",
                                   {scene, Value(Value::Object{{"mode", Value("cubemap")}, {"faces", Value(Value::Array{texH, texH, h, texH, texH, texH})}})});
        };
        const Result stale = sky(deadH), foreign = sky(foreignH), kind = sky(mesh);
        // (the stale handle's failure carries its RECORD PATH, sky.faces[2], as the kind check's does: E0's worth a look 1)
        check(failedWith(stale, "engine.scene.setSky: sky.faces[2]: texture " + std::to_string(dead) +
                                    " is not a texture of scene 'p-scene'") &&
                  failedWith(foreign, "faces[2]: " + std::to_string(foreignH.asInt()) + " is no texture handle of this host") &&
                  failedWith(kind, "is a mesh handle, not a texture handle"),
              "A SKY's TEXTURE HANDLE: a destroyed texture (" + stale.message + "), another host's (" + foreign.message +
                  "), a mesh as a texture (" + kind.message + ") each FAIL by name");
        f.headless.call("engine.mesh.destroy", {scene, mesh});
    }
    f.headless.call("engine.scene.setSky", {scene, Value(Value::Object{})});
    raw->destroyTexture(tex);
    f.none.registry().release(noneScene);
    f.headless.call("engine.scene.destroy", {scene});
}

}  // namespace scriptcommands
