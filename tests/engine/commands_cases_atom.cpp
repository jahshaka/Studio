// script.commands - THE ATOM FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2; test_script_commands.cpp runs them, in its fixed
// order): the three Atom doors are TEST DOORS (measuring arms: refused without --test-doors, the lead's ruling), their
// declared domain refused by name, their effect on a door-open host's own scene on the headless engine, and a scene its
// owner handed in not the script's to set.
#include "script_commands.h"
#include "script/records_atom.h"

#include <memory>

namespace scriptcommands {

void casesAtom(Fixture &f)
{
    if (!f.engine) {
        check(false, "the atom family's cases need the headless engine");
        return;
    }
    const auto door = [](bool on) { return Value(Value::Object{{"on", Value(on)}}); };
    const auto view = [](const char *v) { return Value(Value::Object{{"view", Value(v)}}); };
    Scene *real = f.engine->createScene("q-atom-domains");
    const Value scene(f.none.registry().hold("scene", std::shared_ptr<Scene>(real, [](Scene *) {})));
    // 1. THE TEST DOORS SHUT (no --test-doors): each refused by name before its arguments are read
    {
        const Result d = f.none.call("engine.atom.setDraw", {scene, door(false)});
        const Result o = f.none.call("engine.atom.setOcclusion", {scene, door(false)});
        const Result v = f.none.call("engine.atom.setView", {scene, view("triangles")});
        check(failedWith(d, "engine.atom.setDraw: a test door: this host was not started with --test-doors") &&
                  failedWith(o, "engine.atom.setOcclusion: a test door") && failedWith(v, "engine.atom.setView: a test door"),
              "THE ATOM DOORS ARE TEST DOORS: setDraw, setOcclusion and setView refused without --test-doors (" + d.message + ")");
    }
    // 2. WITH THE DOORS OPEN: the domain refused by name, the doors acting on the host's own scene (the headless engine),
    //    a scene handed in not the script's
    {
        Host run;
        run.setConsole([](const std::string &) {});
        makeEngineCommands(run, f.engine, "", true);
        const Value own = run.call("engine.scene.create", {Value("q-atom")}).value;
        const Result bad = run.call("engine.atom.setView", {own, view("wire")});
        const Result missing = run.call("engine.atom.setDraw", {own, Value(Value::Object{})});
        const Result d0 = run.call("engine.atom.setDraw", {own, door(false)}), d1 = run.call("engine.atom.setDraw", {own, door(true)});
        const Result o0 = run.call("engine.atom.setOcclusion", {own, door(false)});
        const Result v1 = run.call("engine.atom.setView", {own, view("density")}), v0 = run.call("engine.atom.setView", {own, view("off")});
        f.engine->renderOneFrame();
        const Value handed(run.registry().hold("scene", std::shared_ptr<Scene>(real, [](Scene *) {})));
        const Result notMine = run.call("engine.atom.setDraw", {handed, door(false)});
        check(failedWith(bad, "picture.view 'wire' is not one of") && failedWith(missing, "door.on is required") &&
                  d0.status == Status::Ok && d1.status == Status::Ok && o0.status == Status::Ok && v1.status == Status::Ok &&
                  v0.status == Status::Ok && failedWith(notMine, "its settings are not this host's to change"),
              "with the test doors open: an unknown view and a missing switch FAIL by name, the three doors act on the host's "
              "own scene, a handed-in scene's are not the script's (" + notMine.message + ")");
    }
    f.engine->destroyScene(real);
}

}  // namespace scriptcommands
