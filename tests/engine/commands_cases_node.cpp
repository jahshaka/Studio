// script.commands - THE NODE FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2, builder P; test_script_commands.cpp runs them,
// in its fixed order): the node's flags (visible, movable, helper, the face cull) - each declared domain refused by
// name on the null engine, the stale / foreign / wrong-kind handle, a handed-in scene's node not this host's to change,
// and the effect on the headless engine where it answers there.
#include "script_commands.h"

namespace scriptcommands {

void casesNode(Fixture &f)
{
    if (!f.engine) {
        check(false, "THE NODE FAMILY: the headless engine did not boot - its cases are skipped");
        return;
    }
    const Value scene = f.headless.call("engine.scene.create", {Value("p-node")}).value;
    const Value node = f.headless.call("engine.node.create", {scene}).value;
    Scene *raw = f.headless.registry().object<Scene>(scene.asInt());
    const std::int64_t noneScene = f.none.registry().hold<Scene>(kScene, std::shared_ptr<Scene>(raw, [](Scene *) {}));
    const Value noneNode(f.none.registry().name(kNode, Call::idOf(node)));
    const char *const kFlags[] = {"engine.node.setVisible", "engine.node.setMovable", "engine.node.setHelper"};

    // 1. THE DECLARED DOMAIN on the null engine: a flag that is no bool, a face cull no name of the four, a missing
    //    argument - each FAILS by name; THE CONTROL: a flag in its domain passes them (the node's existence, asked of no engine, then FAILS)
    {
        std::string missed;
        for (const char *name : kFlags) {
            const Result notBool = f.none.call(name, {Value(noneScene), noneNode, Value(1)});
            const Result absent = f.none.call(name, {Value(noneScene), noneNode});
            const Result reached = f.none.call(name, {Value(noneScene), noneNode, Value(true)});
            if (!failedWith(notBool, "must be bool (it is int)") || !failedWith(absent, "is required") ||
                !failedWith(reached, "the node's scene is gone"))
                missed += std::string("\n      ") + name + ": " + notBool.message + " / " + absent.message + " / " +
                          reached.message;
        }
        const Result cull = f.none.call("engine.node.setFaceCull", {Value(noneScene), noneNode, Value("sideways")});
        if (!failedWith(cull, "engine.node.setFaceCull: cull 'sideways' is not one of \"material\"|\"back\"|\"front\"|\"twoSided\""))
            missed += "\n      cull: " + cull.message;
        check(missed.empty(), "THE NODE's FLAGS' DECLARED DOMAIN, refused by name on the null engine (a non-bool flag, a "
                              "missing one, a face cull outside its four names: " + cull.message +
                                  "); THE CONTROL: a flag in its domain passes them (the node's existence, asked of no engine, then FAILS)" + missed);
    }

    // 2. ON THE HEADLESS ENGINE every flag and every face cull answers on a node this host made
    {
        std::string missed;
        for (const char *name : kFlags)
            for (const bool on : {true, false}) {
                const Result r = f.headless.call(name, {scene, node, Value(on)});
                if (r.status != Status::Ok) missed += std::string("\n      ") + name + ": " + r.message;
            }
        for (const char *cull : {"back", "front", "twoSided", "material"}) {
            const Result r = f.headless.call("engine.node.setFaceCull", {scene, node, Value(cull)});
            if (r.status != Status::Ok) missed += std::string("\n      cull ") + cull + ": " + r.message;
        }
        check(missed.empty(), "ON THE HEADLESS ENGINE the node's flags (visible, movable, helper: on and off) and its "
                              "four face culls each answer on a node this host made" + missed);
    }

    // 3. THE HANDLES: a destroyed node, another host's node handle, a mesh given as a node - each FAILS by name before
    //    the engine is asked; and a node of a scene its owner HANDED IN that this host did not make is not its to change
    {
        const Value dead = f.headless.call("engine.node.create", {scene}).value;
        f.headless.call("engine.node.destroy", {scene, dead});
        const Value mesh = f.headless.call("engine.mesh.unitCube", {scene}).value;
        const Result stale = f.headless.call("engine.node.setVisible", {scene, dead, Value(true)});
        const Result foreign = f.headless.call("engine.node.setHelper", {scene, noneNode, Value(true)});
        const Result kind = f.headless.call("engine.node.setFaceCull", {scene, mesh, Value("back")});
        // the owner's own scene and node (made in C++), handed in to the headless host unowned
        Scene *owners = f.engine->createScene("p-node-owners");
        const NodeId theirs = owners->createNode();
        f.headless.registry().add({"test.p.handIn", {Access::Test, {}, Kind::Array}, "the owner's scene and node, handed in"},
                                  [owners, theirs](Call &c) -> Value {
                                      const Value s = c.hold(kScene, std::shared_ptr<Scene>(owners, [](Scene *) {}));
                                      return Value(Value::Array{s, c.name(kNode, theirs)});
                                  });
        f.headless.registry().openTestDoors(true);
        const Value handed = f.headless.call("test.p.handIn", {}).value;
        f.headless.registry().openTestDoors(false);
        const Result notMine = f.headless.call("engine.node.setMovable", {handed.asArray()[0], handed.asArray()[1], Value(true)});
        check(failedWith(stale, "is not a node of scene 'p-node'") && failedWith(foreign, "is no node handle of this host") &&
                  failedWith(kind, "is a mesh handle, not a node handle") &&
                  failedWith(notMine, "is not this host's to change: the scene 'p-node-owners' was handed in"),
              "A FLAG's NODE HANDLE: a destroyed node (" + stale.message + "), another host's (" + foreign.message +
                  "), a mesh as a node (" + kind.message + ") each FAIL by name; a handed-in scene's own node is not this "
                  "host's to change (" + notMine.message + ")");
        f.headless.registry().release(handed.asArray()[0].asInt());
        f.engine->destroyScene(owners);
        f.headless.call("engine.mesh.destroy", {scene, mesh});
    }
    f.none.registry().release(noneScene);
    f.headless.call("engine.scene.destroy", {scene});
}

}  // namespace scriptcommands
