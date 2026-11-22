// lab.js_verbs — THE LAB's `js` VERB, its grammar and its keys (LAB-SCRIPTS-1 stage 1; src/lab/labcommands.h). CPU: the
// parser is the lab's API (one parser for the keys, --script and stdin) and compiles alone - no engine, no host.
// LAW: `js load <path>`, `js run`, `js next` and `js clear` parse as the verb `js` and their words - `load` keeps the REST
// OF THE LINE as its path (spaces included); `js` alone, `js load` with no path, a fifth word (`js bogus`) and a word
// after run / next / clear are REFUSED with a reason; the keys F9 and F10 are the lines `js run` and `js next` (the key
// table is the one the window and --help read), and each of those lines parses.
#include <cstdio>
#include <string>
#include "labcommands.h"

static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    lab::Command c;
    std::string err;
    check(lab::parse("js load /tmp/demo.js", c, err) && c.verb == "js" && c.args.size() == 2 && c.args[0] == "load" &&
              c.args[1] == "/tmp/demo.js", "`js load /tmp/demo.js` parses: the verb js, load and the path");
    check(lab::parse("  js   load   /tmp/my scripts/a b.js  ", c, err) && c.args.size() == 2 &&
              c.args[1] == "/tmp/my scripts/a b.js", "`js load` keeps the rest of the line as the path, spaces inside it");
    int words = 0;
    for (const char *w : { "run", "next", "clear" }) {
        const std::string line = std::string("js ") + w;
        if (lab::parse(line, c, err) && c.verb == "js" && c.args.size() == 1 && c.args[0] == w) ++words;
    }
    check(words == 3, "`js run`, `js next` and `js clear` parse (3 of 3)");
    check(!lab::parse("js", c, err) && !err.empty(), "`js` alone is refused, with a reason");
    check(!lab::parse("js load", c, err) && !err.empty(), "`js load` with no path is refused, with a reason");
    check(!lab::parse("js load   ", c, err) && !err.empty(), "`js load` and blanks is refused, with a reason");
    check(!lab::parse("js bogus", c, err) && !err.empty() && err.find("bogus") != std::string::npos,
          "`js bogus` is refused, naming the word");
    check(!lab::parse("js run now", c, err) && !err.empty(), "`js run now` (a word after run) is refused, with a reason");
    // THE KEYS ARE LINES: F9 and F10 run the js lines through the same parser
    check(lab::keyLine("F9") == "js run", "F9 is the line `js run`");
    check(lab::keyLine("F10") == "js next", "F10 is the line `js next`");
    int parsed = 0;
    for (const char *k : { "F9", "F10" })
        if (lab::parse(lab::keyLine(k), c, err) && c.verb == "js") ++parsed;
    check(parsed == 2, "each js key's line parses as the js verb (2 of 2)");
    bool helped = false;
    for (const lab::VerbDoc &v : lab::verbs()) helped = helped || std::string(v.verb) == "js";
    check(helped && lab::helpText().find("--js") != std::string::npos,
          "the help holds the verb and the --js flag (F1 / --help print the grammar)");
    // THE CONTROL: a key the table does not hold maps to nothing - the key checks above can fail
    check(lab::keyLine("F11").empty(), "control: F11 maps to no line");
    std::printf("%s\n", gFail ? ("lab.js_verbs: FAILED (" + std::to_string(gFail) + ")").c_str() : "lab.js_verbs: PASSED");
    return gFail ? 1 : 0;
}
