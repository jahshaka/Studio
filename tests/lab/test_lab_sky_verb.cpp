// lab.sky_verb — THE LAB's SKY VERB, its grammar and its value rule (PHOTON-7-9A; src/lab/labcommands.h). CPU: the
// parser is the lab's API (one parser for keys, scripts and stdin) and compiles alone - no engine, no document.
// LAW: `sky brightness <v>` and `sky rayleigh <v>` parse as the verb `sky` and two words; the value is a plain decimal in
// the document's range 0..10 (Scene::clampSkyRealistic), and anything else is REFUSED, never clamped (a row must set
// exactly the sky it says it sets).
#include <cstdio>
#include <string>
#include "labcommands.h"

static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    lab::Command c;
    std::string err;
    check(lab::parse("sky brightness 4", c, err) && c.verb == "sky" && c.args.size() == 2 && c.args[0] == "brightness" &&
              c.args[1] == "4", "`sky brightness 4` parses: the verb sky, two words");
    check(lab::parse("sky rayleigh 0.5", c, err) && c.verb == "sky" && c.args.size() == 2, "`sky rayleigh 0.5` parses");
    check(!lab::parse("sky brightness", c, err) && !err.empty(), "one word is refused, with a reason");
    check(!lab::parse("sky brightness 1 2", c, err) && !err.empty(), "three words are refused, with a reason");
    bool helped = false;
    for (const lab::VerbDoc &v : lab::verbs()) helped = helped || std::string(v.verb) == "sky";
    check(helped, "the help table holds the verb (F1 / --help print the grammar)");
    float v = -1.0f;
    check(lab::parseSkyDial("0", v, err) && v == 0.0f, "0 is a value (the dial's floor)");
    check(lab::parseSkyDial("10", v, err) && v == 10.0f, "10 is a value (the document's ceiling)");
    check(lab::parseSkyDial("2.5", v, err) && v == 2.5f, "2.5 is 2.5");
    int refused = 0;
    for (const char *bad : { "-0.1", "10.5", "abc", "nan", "inf", "1e1", "0x2", "", "1-2", "+11" })
        if (!lab::parseSkyDial(bad, v, err)) ++refused;
    check(refused == 10, "every value outside 0..10 or not a plain decimal is refused (10 of 10), never clamped");
    // NEGATIVE CONTROL: an unknown verb is refused - the grammar's check above can fail
    check(!lab::parse("skies brightness 4", c, err), "negative control: an unknown verb is refused");
    std::printf("%s\n", gFail ? "lab.sky_verb: FAILED" : "lab.sky_verb: PASSED");
    return gFail ? 1 : 0;
}
