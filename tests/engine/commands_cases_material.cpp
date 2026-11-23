// script.commands - THE MATERIAL FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2; test_script_commands.cpp runs them, in its
// fixed order): per command its declared domain refused by name on the null engine, per table its default and a
// filled value round-trip and inOrder, the effect where the headless engine answers (it answers materials, textures
// made from bytes and from a file, and the decal atlas: measured here, each case prints what it read).
#include "script_commands.h"
#include "script/records_material.h"
#include "jahshaka/engine/Published.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace scriptcommands {

namespace {

Value obj(Value::Object o) { return Value(std::move(o)); }
Value nums(std::initializer_list<double> xs)
{
    Value::Array a;
    for (double x : xs) a.push_back(Value(x));
    return Value(std::move(a));
}

/// A 4x4 TGA (32-bit, top-left origin) at `path`: an image file the engine's loaders read (test_decals.cpp's way).
void writeTga(const char *path, unsigned char grey)
{
    if (FILE *f = std::fopen(path, "wb")) {
        const unsigned char hdr[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 32, 0x28};
        std::fwrite(hdr, 1, sizeof hdr, f);
        const std::vector<unsigned char> bgra(4 * 4 * 4, grey);
        std::fwrite(bgra.data(), 1, bgra.size(), f);
        std::fclose(f);
    }
}

/// A PbrParams with every member off its default (the round trip's filled value).
PbrParams filledPbr()
{
    PbrParams p;
    p.albedo = Colour(0.25f, 0.5f, 0.75f, 0.5f);
    p.metalness = 0.5f;
    p.roughness = 0.25f;
    p.emissive = Colour(2.0f, 1.0f, 0.5f, 1.0f);
    p.alphaMode = PbrAlphaMode::Refractive;
    p.alpha = 0.75f;
    p.alphaCutoff = 0.25f;
    p.normalMapWeight = 2.0f;
    p.uvScale[0] = 2.0f; p.uvScale[1] = 3.0f;
    p.uvOffset[0] = 0.5f; p.uvOffset[1] = -0.5f;
    p.uvRotation = 45.0f;
    p.uvVelocity[0] = 0.125f; p.uvVelocity[1] = -0.25f;
    p.refractionStrength = 0.5f;
    p.clearCoat = 1.0f;
    p.clearCoatRoughness = 0.5f;
    p.brdf = "CookTorranceSeparateDiffuseFresnel";
    p.receiveShadows = false;
    p.emissiveAsLightmap = true;
    p.shadingModel = ShadingModel::Distortion;
    p.workflow = PbrParams::Workflow::SpecularAsFresnel;
    p.detail[1].blend = 12;
    p.detail[1].offsetU = 0.5f; p.detail[1].offsetV = 0.25f;
    p.detail[1].scaleU = 4.0f; p.detail[1].scaleV = 8.0f;
    p.detail[1].weight = 0.5f; p.detail[1].normalWeight = 0.25f;
    p.anisotropy = 16.0f;
    p.address[3] = PbrParams::AddressMode::Mirror;
    p.address[10] = PbrParams::AddressMode::Border;
    p.specularColour = Colour(0.5f, 0.5f, 0.5f, 1.0f);
    p.ior = 2.5f;
    p.fresnelColour = Colour(0.9f, 0.6f, 0.3f, 1.0f);
    p.useFresnelColour = true;
    p.separateFresnel = true;
    return p;
}

}  // namespace

void casesMaterial(Fixture &f)
{
    // 1. THE TABLES: PbrParams (and its detail layer) - the default and a filled value round-trip, inOrder holds
    {
        std::string why, why2;
        const bool ordered = Record<PbrParams>::table().inOrder(why) && Record<PbrParams::DetailLayer>::table().inOrder(why2);
        check(ordered, "PbrParams' and its detail layer's tables name every field in declaration order" +
                           (ordered ? std::string() : " - " + why + why2));
        bool both = true;
        for (const PbrParams &in : {PbrParams(), filledPbr()}) {
            PbrParams back;
            back.brdf = "BlinnPhong";   // (a value the round trip must overwrite)
            std::string w;
            const bool read = Record<PbrParams>::table().fromValue(toValue(in), back, "params", w);
            both = both && read && back == in && back.albedo.a == in.albedo.a && back.emissive.a == in.emissive.a;
        }
        check(both, "PbrParams round-trips its default and a filled value (every member off its default, the colours' "
                    "alpha included: operator== does not read it, the case does)");
    }

    // 2. THE DECLARED DOMAINS, refused by name before the body (the host on no engine: a scene of the headless engine
    //    handed in, a material named - the existence check comes after the arguments)
    if (f.engine) {
        Scene *real = f.engine->createScene("q-domains");
        const Value scene(f.none.registry().hold("scene", std::shared_ptr<Scene>(real, [](Scene *) {})));
        const Value mat(f.none.registry().name(kMaterial, 1)), node(f.none.registry().name(kNode, 1));
        const auto pbr = [&](const char *m, Value v) {
            return f.none.call("engine.material.createPbr", {scene, obj({{m, std::move(v)}})});
        };
        const auto detail = [](double blend) {
            Value::Object layer{{"blend", Value(blend)}};
            return Value(Value::Array{obj({}), obj(std::move(layer))});
        };
        const Result colour3 = pbr("albedo", nums({0.9, 0.5, 0.2}));
        std::printf("Q: THE 3-NUMBER COLOUR's refusal: %s\n", colour3.message.c_str());
        const struct { Result r; const char *text; } refused[] = {
            {colour3, "engine.material.createPbr: params.albedo must hold 4 elements (it holds 3)"},
            {pbr("albedo", nums({1.5, 0, 0, 1})), "params.albedo[0] 1.5 is outside 0..1"},
            {pbr("metalness", Value(2.0)), "params.metalness 2 is outside 0..1"},
            {pbr("roughness", Value(-0.5)), "params.roughness -0.5 is outside 0..1"},
            {pbr("emissive", nums({-1, 0, 0, 1})), "params.emissive[0] -1 is outside >= 0"},
            {pbr("alphaMode", Value("fade")), "params.alphaMode 'fade' is not one of"},
            {pbr("alpha", Value(1.5)), "params.alpha 1.5 is outside 0..1"},
            {pbr("alphaCutoff", Value(-0.5)), "params.alphaCutoff -0.5 is outside 0..1"},
            {pbr("normalMapWeight", Value(-1.0)), "params.normalMapWeight -1 is outside >= 0"},
            {pbr("uvScale", nums({1, 1, 1})), "params.uvScale must hold 2 elements (it holds 3)"},
            {pbr("refractionStrength", Value(-1.0)), "params.refractionStrength -1 is outside >= 0"},
            {pbr("clearCoat", Value(2.0)), "params.clearCoat 2 is outside 0..1"},
            {pbr("clearCoatRoughness", Value(2.0)), "params.clearCoatRoughness 2 is outside 0..1"},
            {pbr("brdf", Value("Phong")), "params.brdf 'Phong' is not one of"},
            {pbr("shadingModel", Value("toon")), "params.shadingModel 'toon' is not one of"},
            {pbr("workflow", Value("gloss")), "params.workflow 'gloss' is not one of"},
            {pbr("detail", detail(13)), "params.detail[1].blend 13 is outside 0..12"},
            {pbr("anisotropy", Value(32.0)), "params.anisotropy 32 is outside 1..16"},
            {pbr("anisotropy", Value(0.5)), "params.anisotropy 0.5 is outside 1..16"},
            {pbr("address", Value(Value::Array{Value("wrap")})), "params.address must hold 11 elements (it holds 1)"},
            {pbr("specularColour", nums({-1, 0, 0, 1})), "params.specularColour[0] -1 is outside >= 0"},
            {pbr("ior", Value(0.5)), "params.ior 0.5 is outside >= 1"},
            {pbr("fresnelColour", nums({1.5, 0, 0, 1})), "params.fresnelColour[0] 1.5 is outside 0..1"},
            {pbr("glow", Value(1.0)), "params has no member 'glow'"},
            {f.none.call("engine.material.setPbr", {scene, mat, obj({{"ior", Value(0.0)}})}), "params.ior 0 is outside >= 1"},
            {f.none.call("engine.material.createUnlit", {scene, obj({{"colour", nums({1, 1, 1})}})}),
             "unlit.colour must hold 4 elements (it holds 3)"},
            {f.none.call("engine.material.createUnlit", {scene, obj({{"colour", nums({1, 1, -2, 1})}})}),
             "unlit.colour[2] -2 is outside >= 0"},
            {f.none.call("engine.material.setShadingModel", {scene, mat, obj({{"model", Value("toon")}})}),
             "shading.model 'toon' is not one of"},
            {f.none.call("engine.material.setTexture", {scene, mat, obj({{"slot", Value("occlusion")}})}),
             "bind.slot 'occlusion' is not one of"},
            {f.none.call("engine.material.assign", {scene, node, obj({})}), "wear.material is required"},
            {f.none.call("engine.texture.create", {scene, obj({{"width", Value(0)}, {"height", Value(1)},
                                                               {"rgba", Value::bytes({})}, {"srgb", Value(true)}})}),
             "image.width 0 is outside 1..16384 px"},
            {f.none.call("engine.texture.create", {scene, obj({{"width", Value(2)}, {"height", Value(2)},
                                                               {"rgba", Value::bytes(Value::Bytes(15, 0))}, {"srgb", Value(true)}})}),
             "image.rgba holds 15 bytes, not width x height x 4 = 16"},
            {f.none.call("engine.texture.create", {scene, obj({{"width", Value(1)}, {"height", Value(1)},
                                                               {"rgba", Value::bytes(Value::Bytes(4, 0))}})}),
             "image.srgb is required"},
            {f.none.call("engine.texture.load", {scene, obj({{"path", Value("x.png")}})}), "file.srgb is required"},
            {f.none.call("engine.texture.loadDecal", {scene, obj({{"path", Value("x.png")}, {"kind", Value("albedo")}})}),
             "file.kind 'albedo' is not one of"},
        };
        std::string missed;
        for (const auto &x : refused)
            if (!failedWith(x.r, x.text)) missed += "\n      " + std::string(x.text) + " <- " + x.r.message;
        check(missed.empty(), "the material and texture commands refuse every declared domain by name (" +
                                  std::to_string(sizeof refused / sizeof refused[0]) + " cases), the 3-number colour "
                                  "among them" + missed);
        // THE CONTROL: the default record passes every bound and reaches the engine's existence check (no engine here)
        const Result ok = pbr("roughness", Value(0.0));
        check(ok.status == Status::Failed && ok.message.find("the engine did not start") != std::string::npos,
              "THE CONTROL: a record inside every bound (roughness 0: the engine's own clamp, by contract) passes them (" +
                  ok.message + ")");
        f.engine->destroyScene(real);
    }

    // 3. ON THE HEADLESS ENGINE: the material commands act, the textures and decal images are made, kinded and
    //    destroyed, a path loaded twice is two ids (the second a refcounted alias), the owner's material is not the
    //    script's, and what a host loaded into a scene its owner handed in is gone at its end - the decal slice too
    if (!f.engine) {
        check(false, "the material family's headless cases need the headless engine");
        return;
    }
    Host &h = f.headless;
    const Value scene = h.call("engine.scene.create", {Value("q-material")}).value;
    Scene *s = h.registry().object<Scene>(scene.asInt());
    const Value cube = h.call("engine.mesh.unitCube", {scene}).value;
    const Value node = h.call("engine.node.create", {scene}).value;
    const Result made = h.call("engine.material.createPbr", {scene, toValue(filledPbr())});
    const Result lit = h.call("engine.material.createPbr", {scene, obj({})});
    const Result unlit = h.call("engine.material.createUnlit", {scene, obj({{"colour", nums({1, 0, 0, 0.5})}})});
    std::printf("Q: the headless engine's materials: createPbr %s, createUnlit %s\n", made.message.c_str(), unlit.message.c_str());
    const bool attached = h.call("engine.node.attachMesh", {scene, node, obj({{"mesh", cube}, {"material", lit.value}})}).value == Value(true);
    const Result set = h.call("engine.material.setPbr", {scene, lit.value, obj({{"albedo", nums({0.1, 0.2, 0.3, 1})}})});
    const Result model = h.call("engine.material.setShadingModel", {scene, lit.value, obj({{"model", Value("unlit")}})});
    const Result back = h.call("engine.material.setShadingModel", {scene, lit.value, obj({{"model", Value("lit")}})});
    const Value lit2 = h.call("engine.material.createPbr", {scene, obj({{"roughness", Value(0.1)}})}).value;
    const Result wear = h.call("engine.material.assign", {scene, node, obj({{"material", lit2}})});
    check(made.status == Status::Ok && !made.value.isNull() && unlit.status == Status::Ok && attached &&
              set.value == Value(true) && model.value == Value(true) && back.value == Value(true) && wear.value == Value(true),
          "createPbr (the filled record), createUnlit, setPbr, setShadingModel there and back, and assign act on the "
          "headless engine (" + set.message + model.message + wear.message + ")");

    // the textures
    const Value::Bytes px(2 * 2 * 4, 200);
    const Result tex = h.call("engine.texture.create", {scene, obj({{"width", Value(2)}, {"height", Value(2)},
                                                                    {"rgba", Value::bytes(px)}, {"srgb", Value(true)}})});
    const Result bind = h.call("engine.material.setTexture", {scene, lit2, obj({{"slot", Value("albedo")}, {"texture", tex.value}})});
    const Result clear = h.call("engine.material.setTexture", {scene, lit2, obj({{"slot", Value("albedo")}})});
    const char *tga = "q_material.tga";
    writeTga(tga, 160);
    const Result load1 = h.call("engine.texture.load", {scene, obj({{"path", Value(tga)}, {"srgb", Value(true)}})});
    const Result load2 = h.call("engine.texture.load", {scene, obj({{"path", Value(tga)}, {"srgb", Value(true)}})});
    std::printf("Q: the headless engine's textures: create %s, load %s / %s (%s)\n", tex.status == Status::Ok ? "answers" : tex.message.c_str(),
                load1.status == Status::Ok ? "answers" : load1.message.c_str(), load2.status == Status::Ok ? "answers" : "-",
                load1.message.c_str());
    check(tex.status == Status::Ok && bind.value == Value(true) && clear.value == Value(true),
          "engine.texture.create makes a texture, engine.material.setTexture binds it to a slot and null clears it (" +
              tex.message + bind.message + clear.message + ")");
    const unsigned id1 = load1.value.isNull() ? 0 : Call::idOf(load1.value), id2 = load2.value.isNull() ? 0 : Call::idOf(load2.value);
    // A FILE THAT IS NOT THERE: what the engine answers today (the lead's question, measured)
    const Result noFile = h.call("engine.texture.load", {scene, obj({{"path", Value("q_no_such_image.tga")}, {"srgb", Value(true)}})});
    const Result noDecal = h.call("engine.texture.loadDecal", {scene, obj({{"path", Value("q_no_such_image.tga")}, {"kind", Value("diffuse")}})});
    std::printf("Q: a missing file: engine.texture.load %s (%s) / loadDecal %s (%s)\n", noFile.status == Status::Refused ? "REFUSED" : "answers",
                noFile.message.c_str(), noDecal.status == Status::Refused ? "REFUSED" : "answers", noDecal.message.c_str());
    check(noFile.status == Status::Refused && noFile.value == Value(0) && noFile.message.find("q_no_such_image.tga") != std::string::npos &&
              noDecal.status == Status::Refused && noDecal.value == Value(0) && noDecal.message.find("q_no_such_image.tga") != std::string::npos,
          "a file that is not there is REFUSED by name by engine.texture.load and loadDecal (" + noFile.message + "; " + noDecal.message + ")");
    const Result drop1 = id1 ? h.call("engine.texture.destroy", {scene, load1.value}) : Result{};
    check(id1 && id2 && id1 != id2 && drop1.value == Value(true) && s->texturePresence(id1) == Scene::Presence::Absent &&
              s->texturePresence(id2) == Scene::Presence::Present,
          "A PATH LOADED TWICE IS TWO IDS (" + std::to_string(id1) + ", " + std::to_string(id2) + "): destroying the first "
          "leaves the second (a refcounted alias, never the first's own id)");
    const Result deadUse = h.call("engine.material.setTexture", {scene, lit2, obj({{"slot", Value("normal")}, {"texture", load1.value}})});
    check(failedWith(deadUse, "texture " + std::to_string(id1) + " is not a texture of scene"),
          "a destroyed texture's handle FAILS by name (" + deadUse.message + ")");

    // the decal images: their own kind
    const auto atlasUsed = [&]() {
        f.engine->renderOneFrame();
        return s->published().decalAtlasUsed[int(DecalMap::Diffuse)];
    };
    const unsigned before = atlasUsed();
    const Result decal = h.call("engine.texture.loadDecal", {scene, obj({{"path", Value(tga)}, {"kind", Value("diffuse")}})});
    const unsigned during = atlasUsed();
    const Result asTex = h.call("engine.material.setTexture", {scene, lit2, obj({{"slot", Value("albedo")}, {"texture", decal.value}})});
    const Result texAsDecal = h.call("engine.texture.destroyDecal", {scene, tex.value});
    const Result dropDecal = h.call("engine.texture.destroyDecal", {scene, decal.value});
    const unsigned after = atlasUsed();
    std::printf("Q: the headless engine's decal atlas: %u -> %u -> %u (%s)\n", before, during, after, decal.message.c_str());
    check(decal.status == Status::Ok && during == before + 1 && after == before &&
              failedWith(asTex, "is a decalTexture handle, not a texture handle") &&
              failedWith(texAsDecal, "is a texture handle, not a decalTexture handle") && dropDecal.value == Value(true),
          "engine.texture.loadDecal answers a decalTexture: never a texture either way, its slice taken (" +
              std::to_string(before) + " -> " + std::to_string(during) + ") and given back by destroyDecal (" +
              std::to_string(after) + ")");

    // THE OWNERSHIP LAW: in a scene handed in, the owner's material is not the script's; what the host loaded there
    // (a texture from a file, a decal image) is destroyed at its end
    Scene *handed = f.engine->createScene("q-handed");
    const MaterialId ownerMat = handed->createPbrMaterial(PbrParams());
    const unsigned handedBefore = [&]() { f.engine->renderOneFrame(); return handed->published().decalAtlasUsed[int(DecalMap::Diffuse)]; }();
    unsigned loadedTex = 0, loadedDecal = 0, handedDuring = 0;
    std::string ownerWhy;
    {
        Host run;
        run.setConsole([](const std::string &) {});
        makeEngineCommands(run, f.engine, "", false);
        const Value hs(run.registry().hold("scene", std::shared_ptr<Scene>(handed, [](Scene *) {})));
        ownerWhy = run.call("engine.material.setPbr", {hs, run.registry().name(kMaterial, ownerMat), obj({})}).message;
        const Result t = run.call("engine.texture.load", {hs, obj({{"path", Value(tga)}, {"srgb", Value(false)}})});
        const Result d = run.call("engine.texture.loadDecal", {hs, obj({{"path", Value(tga)}, {"kind", Value("diffuse")}})});
        loadedTex = t.value.isNull() ? 0 : Call::idOf(t.value);
        loadedDecal = d.value.isNull() ? 0 : Call::idOf(d.value);
        f.engine->renderOneFrame();
        handedDuring = handed->published().decalAtlasUsed[int(DecalMap::Diffuse)];
    }
    f.engine->renderOneFrame();
    const unsigned handedAfter = handed->published().decalAtlasUsed[int(DecalMap::Diffuse)];
    check(ownerWhy.find("material " + std::to_string(ownerMat) + " is not this host's to change") != std::string::npos &&
              handed->materialPresence(ownerMat) == Scene::Presence::Present,
          "the owner's material in a handed-in scene is not the script's to change (" + ownerWhy + ")");
    check(loadedTex && loadedDecal && handedDuring == handedBefore + 1 &&
              handed->texturePresence(loadedTex) == Scene::Presence::Absent &&
              handed->decalTexturePresence(loadedDecal) == Scene::Presence::Absent && handedAfter == handedBefore,
          "WHAT A HOST LOADED IN A HANDED-IN SCENE GOES AT ITS END: the texture (" + std::to_string(loadedTex) +
              ") and the decal image (" + std::to_string(loadedDecal) + ") absent, the atlas " + std::to_string(handedBefore) +
              " -> " + std::to_string(handedDuring) + " -> " + std::to_string(handedAfter));
    f.engine->destroyScene(handed);

    // THE REVERSE ALIAS (the audit's worth-a-look 1): a script loads a path into a handed-in scene FIRST, its owner loads
    // the same path after (id 0: the engine's own reserve) and binds it; the host ends - the owner's texture lives on
    Scene *first = f.engine->createScene("q-reverse");
    const MaterialId ownerMat2 = first->createPbrMaterial(PbrParams());
    TextureId scriptTex = 0, ownerTex = 0, ownerDecal = 0, scriptDecal = 0;
    {
        Host run;
        run.setConsole([](const std::string &) {});
        makeEngineCommands(run, f.engine, "", false);
        const Value hs(run.registry().hold("scene", std::shared_ptr<Scene>(first, [](Scene *) {})));
        const Result t = run.call("engine.texture.load", {hs, obj({{"path", Value(tga)}, {"srgb", Value(true)}})});
        const Result d = run.call("engine.texture.loadDecal", {hs, obj({{"path", Value(tga)}, {"kind", Value("diffuse")}})});
        scriptTex = t.value.isNull() ? 0 : Call::idOf(t.value);
        scriptDecal = d.value.isNull() ? 0 : Call::idOf(d.value);
        ownerTex = first->loadTexture(tga, true);
        ownerDecal = first->loadDecalTexture(tga, DecalMap::Diffuse);
        first->setPbrTexture(ownerMat2, PbrTextureSlot::Albedo, ownerTex);
    }
    std::printf("Q: THE REVERSE ALIAS: the script's texture %u, the owner's %u (after the host: %s); the script's decal image "
                "%u, the owner's %u (after the host: %s)\n", scriptTex, ownerTex,
                first->texturePresence(ownerTex) == Scene::Presence::Present ? "alive" : "GONE", scriptDecal, ownerDecal,
                first->decalTexturePresence(ownerDecal) == Scene::Presence::Present ? "alive" : "GONE");
    check(scriptTex && ownerTex && ownerTex != scriptTex && first->texturePresence(ownerTex) == Scene::Presence::Present &&
              scriptDecal && ownerDecal && ownerDecal != scriptDecal &&
              first->decalTexturePresence(ownerDecal) == Scene::Presence::Present,
          "THE REVERSE ALIAS: a path a script loaded FIRST into a handed-in scene, then its owner, is the owner's own id "
          "(" + std::to_string(ownerTex) + ", the script's " + std::to_string(scriptTex) + ") and outlives the host's end "
          "- the texture and the decal image");
    // (THE MATERIAL's BINDING has no CPU read that sees the unbind - the live datablock's dump does not change on the
    // headless engine: the confirm's C1, the twin deleted - so the texture's PRESENCE above is the law: a destroyed
    // texture is unbound from every material first, OgreMaterials.cpp destroyTexture)
    f.engine->destroyScene(first);
    std::remove(tga);
}

}  // namespace scriptcommands
