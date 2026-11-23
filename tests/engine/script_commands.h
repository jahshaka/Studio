#pragma once
// script.commands - THE ENGINE COMMANDS' FAMILY CASES, ON THE CPU (SCRIPT-IN-ENGINE-1 stage 2; test_script_commands.cpp
// holds the generic laws and runs every family's cases). ONE CASES FILE A FAMILY (commands_cases_<family>.cpp, each its
// builder's own): the batch's builders never meet in one test file. Every case gets the FIXTURE below.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Script.h"
#include "script/commands.h"   // the engine's command state and kinds (engine-private: this row tests them)

#include <string>

namespace scriptcommands {

using namespace jahshaka::engine;
using namespace jahshaka::engine::script;

/// WHAT EVERY FAMILY's CASES GET: two hosts carrying the engine's commands - `none` on NO engine (a command's declared
/// domain is refused by name before its body asks the engine: every argument law runs here) and `headless` on THE
/// HEADLESS (NULL) ENGINE (no card, no display: a command's effect where that engine answers it) - with its command
/// state `live` (a case's own test-local command reaches the made records and the helpers through it). `engine` is the
/// headless engine; null when it did not boot (its boot line is then the row's first FAIL: a case says so and skips).
/// `outside`: a host for an OUTSIDE client (Host::Options::outsideClient) on no engine - an engine-wide setter's
/// refusal (Commands::engineWide) is asked there.
struct Fixture {
    Host &none;
    Host &headless;
    const Commands &live;
    Engine *engine;
    Host &outside;
};

/// One law judged: `ok:` / `FAIL:` and its text (a FAIL fails the row); a call that FAILED with `part` in its message.
void check(bool ok, const std::string &what);
bool failedWith(const Result &r, const std::string &part);

/// The families' cases (commands_cases_<family>.cpp), run in this order by test_script_commands.cpp.
void casesScene(Fixture &);
void casesNode(Fixture &);
void casesLight(Fixture &);
void casesMaterial(Fixture &);
void casesEffects(Fixture &);
void casesView(Fixture &);
void casesPicture(Fixture &);
void casesGi(Fixture &);
void casesGiRead(Fixture &);
void casesEngine(Fixture &);
void casesAtom(Fixture &);
void casesStatus(Fixture &);

}  // namespace scriptcommands
