// script.commands - THE ENGINE COMMANDS' LAWS, ON THE CPU (SCRIPT-IN-ENGINE-1 stage 2; script_commands.h says the
// fixture). No card, no display. THE GENERIC LAWS (E0, the batch's base): EVERY FAMILY REGISTERED FOR REAL - the
// engine's whole command set on a host with no engine - leaves validate() empty; EVERY COMMAND carries its access tag
// and its doc; the commands counted are at least the 31 stage 2 started from (THE CONTROL: the count is above zero, and
// printed); A TEST DOOR IS REFUSED AT RUN TIME in a host not started with --test-doors (a test-local command tagged
// `test`: no engine command is one yet) and answers in one that was. Then every family's own cases
// (commands_cases_<family>.cpp), each on the same fixture: a host on no engine and one on the headless (NULL) engine.
#include "script_commands.h"
#include "enginetesthelpers.h"   // the headless (NULL) engine's config: no Qt, no card, no display

#include <cstdio>
#include <memory>

namespace scriptcommands {

static int gFail = 0;

void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    std::fflush(stdout);
    if (!ok) ++gFail;
}

bool failedWith(const Result &r, const std::string &part)
{
    return r.status == Status::Failed && r.value.isNull() && r.message.find(part) != std::string::npos;
}

}  // namespace scriptcommands

using namespace scriptcommands;

int main()
{
    // 1. EVERY FAMILY REGISTERED FOR REAL, on no engine: no fault; every command tagged and documented; >= 31 of them
    Host none;
    none.setConsole([](const std::string &) {});
    registerEngineCommands(none, nullptr, "no engine: the arguments' laws", false);
    const std::vector<std::string> faults = none.registry().validate();
    check(faults.empty(), "every family's commands registered on a host with no engine: validate() is empty" +
                              (faults.empty() ? std::string() : " - first fault: " + faults[0]));
    std::size_t checked = 0;
    std::string bare;
    for (const CommandInfo *c : none.registry().commands()) {
        ++checked;
        if (c->signature.access == Access::Unset || c->doc.empty()) bare += " " + c->name;
    }
    check(bare.empty() && checked >= 31,
          "every one of the " + std::to_string(checked) + " commands carries its access tag and its doc, and there are "
          "at least the 31 stage 2 started from" + (bare.empty() ? std::string() : " - bare:" + bare));
    check(checked > 0, "THE CONTROL: the commands were counted (" + std::to_string(checked) + ")");

    // 2. A TEST DOOR IS REFUSED AT RUN TIME without --test-doors, and answers with it (the registry's switch, set by the
    //    engine's registration from its testDoors argument)
    Host opened;
    opened.setConsole([](const std::string &) {});
    registerEngineCommands(opened, nullptr, "no engine: the arguments' laws", true);
    const auto door = [](Host &h) {
        h.registry().add({"test.door", {Access::Test, {}, Kind::Bool}, "a test-local test door: answers true"},
                         [](Call &) { return Value(true); });
        return h.call("test.door", {});
    };
    const Result closedDoor = door(none), openDoor = door(opened);
    check(failedWith(closedDoor, "test.door: a test door: this host was not started with --test-doors") &&
              openDoor.status == Status::Ok && openDoor.value == Value(true),
          "A TEST DOOR IS REFUSED AT RUN TIME in a host without --test-doors (" + closedDoor.message +
              "); THE CONTROL: the same door answers in a host with them");

    // 3. EVERY FAMILY's CASES, on the fixture: the host on no engine and the one on the headless engine
    std::string bootError;
    const std::unique_ptr<Engine> engine =
        Engine::create(enginetest::testEngineConfig("test-script-commands-ogre.log", enginetest::EngineOverrides().headless()),
                       bootError);
    check(engine != nullptr, "the headless (NULL) engine boots for the families' cases (" + bootError + ")");
    {
        Host headless;
        headless.setConsole([](const std::string &) {});
        const Commands live = makeEngineCommands(headless, engine.get(), bootError, false);
        Fixture f{none, headless, live, engine.get()};
        casesScene(f);
        casesNode(f);
        casesLight(f);
        casesMaterial(f);
        casesEffects(f);
        casesView(f);
        casesPicture(f);
        casesGi(f);
        casesGiRead(f);
        casesEngine(f);
        casesAtom(f);
        casesStatus(f);
    }   // (the hosts end before their engine: registerEngineCommands' contract)

    if (gFail) std::printf("script.commands: FAILED (%d)\n", gFail);
    else std::printf("script.commands: PASSED\n");
    return gFail ? 1 : 0;
}
