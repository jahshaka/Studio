// script.view_ownership (SCRIPT-IN-ENGINE-1 2b b2: THE OWNERSHIP LAW's views half; a card row, since a view needs a
// device - the headless engine makes none) - a view a script's command MADE is destroyed at its HOST's END; a view the
// host's owner HANDED IN survives the host, and is not the script's to destroy.
// THE READ: a view's name is unique among the live views (Engine::createOffscreenView refuses a taken one), so a name
// free again is a view gone. THE CONTROL: while the host lived, both names were taken. No frame is drawn.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Script.h"
#include "enginetesthelpers.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace jahshaka::engine;

static int gFail = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    if (!ok) ++gFail;
}

int main()
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-script-view-ownership-ogre.log"), err);
    if (!engine) {
        std::printf("FAIL: engine create: %s\nscript.view_ownership: FAILED (1)\n", err.c_str());
        return 1;
    }
    const auto make = [&](const char *name) {
        return engine->createOffscreenView(name, 8, 8, Colour(0, 0, 0), OffscreenContract::Live);
    };
    View *handed = make("handed-view");
    bool whileLived = false;
    int verdict = -1;
    {
        script::Host host;
        std::vector<std::string> said;
        host.setConsole([&said](const std::string &l) { said.push_back(l); });
        script::registerEngineCommands(host, engine.get(), err, false);
        const std::int64_t h = host.registry().hold("view", std::shared_ptr<View>(handed, [](View *) {}));
        const script::Result r = host.run(
            "const made = engine.view.createOffscreen({name: 'made-view', width: 8, height: 8});\n"
            "host.check({ok: made !== 0, what: 'the script made a view'});\n"
            "let refused = '';\n"
            "try { engine.view.destroy(" + std::to_string(h) + "); } catch (e) { refused = e.message; }\n"
            "host.check({ok: refused.indexOf(\"was handed in by the host's owner\") >= 0,\n"
            "            what: 'a handed-in view is not the script\\'s to destroy: ' + refused});\n",
            "view_ownership.js");
        verdict = host.verdict(r);
        for (const std::string &l : said) std::printf("  %s\n", l.c_str());
        whileLived = h > 0 && make("made-view") == nullptr && make("handed-view") == nullptr;
    }   // THE HOST ENDS
    View *again = make("made-view");
    check(verdict == 0, "the script ran its two laws (" + std::to_string(verdict) + " fault(s))");
    check(whileLived && again != nullptr && make("handed-view") == nullptr,
          "THE OWNERSHIP LAW, views: the view the script made is destroyed at its host's end (its name free again); the "
          "handed-in view survives (its name still taken); THE CONTROL: both names taken while the host lived");
    if (again) engine->destroyView(again);
    engine->destroyView(handed);
    if (gFail) std::printf("script.view_ownership: FAILED (%d)\n", gFail);
    else std::printf("script.view_ownership: PASSED\n");
    return gFail ? 1 : 0;
}
