// document.standalone (NOQT-DOCUMENT-1 stage 6; the lead's ruling H2): THE DOCUMENT WITH NOTHING ELSE. This program links
// IrisDocument and nothing else of ours (tests/document/CMakeLists.txt): the proof that the document is a library a tool
// can use with no Qt, no IrisGL (the importer, the bake, the mirror) and no engine. Its laws:
//  (a) THE PROCESS's OWN LIBRARIES: every object loaded into it (dl_iterate_phdr, the whole closure, not one NEEDED list)
//      is libIrisDocument, libIrisCore or the C / C++ runtime: a link that drifts (a Qt, IrisGL or OgreNext library) reds;
//      the control: libIrisDocument and libIrisCore are among them;
//  (b) A DOCUMENT IS BUILT AND EDITED: Scene::create; a MeshNode under the root and a LightNode under it; a light property
//      set through its row and read back; a material set on the mesh; a World row pinned as an override;
//  (c) THE ONE COUNTER PER KIND OF ID (IrisCore's, core/ids.h): a node's id drawn by the library, then reserveNodeId()
//      called by this executable, then a node's id again: consecutive (n1, n1 + 1, n1 + 2) - one counter in the process;
//      and two draws of each of the engine's mesh, material and texture ids consecutive, interleaved;
//  (d) THE DOCUMENT's VALUE-SHAPED PARTS through IrisCore's JSON: the input map, an avatar definition and the World
//      overrides, each written by iris::json::write, parsed back and read back equal.
#include "irisgl/core/json.h"
#include "irisgl/core/value.h"
#include "irisgl/document/assets/avatardefinition.h"
#include "irisgl/document/input/inputmap.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/worldmodes.h"
#include "jahshaka/engine/Ids.h"

#include <link.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { std::printf((cond) ? "ok:   " : "FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); \
                              if (!(cond)) ++failures; } while (0)

static int collect(dl_phdr_info *info, size_t, void *data)
{
    static_cast<std::vector<std::string> *>(data)->push_back(info->dlpi_name ? info->dlpi_name : "");
    return 0;
}

static bool allowed(const std::string &path, bool *ours)
{
    const std::string base = path.substr(path.rfind('/') + 1);
    const char *own[] = {"libIrisDocument", "libIrisCore"};
    const char *runtime[] = {"libstdc++.so", "libm.so", "libgcc_s.so", "libc.so", "ld-linux", "linux-vdso"};
    for (const char *p : own)
        if (base.rfind(p, 0) == 0) { *ours = true; return true; }
    for (const char *p : runtime)
        if (base.rfind(p, 0) == 0) return true;
    return base.empty();   // the executable itself
}

static iris::Value roundTrip(const iris::Value &v, bool *ok)
{
    std::string text, why;
    iris::Value back;
    *ok = iris::json::write(v, text, why) && iris::json::parse(text, back, why);
    return back;
}

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // a killed row keeps every line it printed (CODING_RULES § 5)

    std::printf("== (a) the process's own libraries ==\n");
    std::vector<std::string> loaded;
    dl_iterate_phdr(collect, &loaded);
    int ours = 0;
    for (const std::string &l : loaded) {
        bool own = false;
        const bool ok = allowed(l, &own);
        ours += own;
        if (!ok) CHECK(false, "a library this program must not load: %s", l.c_str());
    }
    CHECK(ours == 2, "libIrisDocument and libIrisCore are loaded (the control: %d of %zu objects are ours)", ours,
          loaded.size());

    std::printf("== (b) a document built and edited ==\n");
    const iris::ScenePtr scene = iris::Scene::create();
    CHECK(scene && scene->getRootNode(), "Scene::create gives a scene with its root");
    const iris::MeshNodePtr mesh = iris::MeshNode::create();
    const iris::LightNodePtr light = iris::LightNode::create();
    scene->getRootNode()->addChild(mesh);
    mesh->addChild(light);
    CHECK(light->getParent() == mesh && mesh->getParent() == scene->getRootNode() && light->getScene() == scene,
          "a MeshNode under the root and a LightNode under it, both in the scene");
    const bool set = light->setPropertyValue("intensity", iris::Value(2.5));
    CHECK(set && light->getPropertyValue("intensity").asNumber() == 2.5, "the light's intensity row set and read back");
    const iris::PbrMaterialPtr material = iris::PbrMaterial::create();
    CHECK(material->setValue("roughness", iris::Value(0.25)), "a material row set");
    mesh->setMaterial(material);
    CHECK(mesh->getMaterial() == material, "the material set on the mesh");
    worldmodes::pinRowValue(scene, "bloom", 0);
    const iris::Value *pin = scene->worldOverrides.find("bloom");
    CHECK(pin && pin->asInt() == 0, "a World row (bloom) pinned as an override");

    std::printf("== (c) one node-id counter in the process ==\n");
    const std::int64_t n1 = iris::MeshNode::create()->getNodeId();
    const std::int64_t b = std::int64_t(jahshaka::engine::reserveNodeId());
    const std::int64_t n2 = iris::MeshNode::create()->getNodeId();
    CHECK(n1 > 0 && b == n1 + 1 && n2 == b + 1,
          "the library's id %lld, this program's %lld, the library's %lld: consecutive (one counter)",
          (long long)n1, (long long)b, (long long)n2);
    // the engine's three resource counters (Ids.h) are IrisCore's too: two draws of each are consecutive, and drawing one
    // kind moves no other (the document draws none of them: the engine and the mirror do)
    const unsigned m1 = jahshaka::engine::reserveMeshId(), t1 = jahshaka::engine::reserveTextureId();
    const unsigned a1 = jahshaka::engine::reserveMaterialId(), a2 = jahshaka::engine::reserveMaterialId();
    const unsigned m2 = jahshaka::engine::reserveMeshId(), t2 = jahshaka::engine::reserveTextureId();
    CHECK(m1 > 0 && t1 > 0 && a1 > 0 && m2 == m1 + 1 && a2 == a1 + 1 && t2 == t1 + 1,
          "mesh ids %u, %u; material ids %u, %u; texture ids %u, %u: each kind consecutive, one counter each", m1, m2, a1,
          a2, t1, t2);

    std::printf("== (d) the Value-shaped parts through iris::json ==\n");
    const iris::InputMap input = iris::InputMap::defaults();
    bool ok = false;
    const iris::Value inputBack = roundTrip(input.toValue(), &ok);
    iris::InputMap read;
    CHECK(ok && input.toValue().asObject().size() > 0 && read.fromValue(inputBack) && read.toValue() == input.toValue(),
          "the input map (%zu actions) written, parsed and read back equal", input.toValue().asObject().size());
    iris::AvatarDefinition avatar;
    avatar.name = "Würfel ☃";
    avatar.modelAsset = "0f6e3d2c-guid";
    const iris::Value avatarValue = iris::avatarDefinitionToJson(avatar);
    const iris::Value avatarBack = roundTrip(avatarValue, &ok);
    iris::AvatarDefinition avatarRead;
    std::string error;
    CHECK(ok && iris::avatarDefinitionFromJson(avatarBack, avatarRead, &error) && avatarRead.name == avatar.name
              && avatarRead.modelAsset == avatar.modelAsset && iris::avatarDefinitionToJson(avatarRead) == avatarValue,
          "an avatar definition (a non-ASCII name) written, parsed and read back equal%s", error.c_str());
    const iris::Value overridesBack = roundTrip(scene->worldOverrides, &ok);
    CHECK(ok && overridesBack.find("bloom") && overridesBack == scene->worldOverrides,
          "the World overrides written, parsed and read back equal");

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
