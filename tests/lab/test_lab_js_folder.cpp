// lab.js_folder — THE SCRIPTS FOLDER the strip's Load (`js next`, F10) walks (LAB-SCRIPTS-1 stage 2, the lead's ruling;
// src/lab/labscripts.h). CPU, in a scratch folder of its own (removed at the end).
// LAW: (1) an EMPTY folder answers `no scripts in <folder>` and picks nothing; (2) the list is RE-READ at every press:
// files added between two presses are seen at the next one, a deleted file is gone (the current one included: the press
// goes to the name that follows it); (3) the order is BYTEWISE (`B.js` before `a.js`), only `.js` files count; (4) after
// the last comes the first; each pick says `<i> of <n>: <name>`.
// CONTROL: a press on a folder with one script picks it (a walk that never picks would pass (1) alone).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include "labscripts.h"

static int gFail = 0;
static void check(bool ok, const std::string &what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str()); if (!ok) ++gFail; }
static void touch(const std::string &dir, const char *name) { std::ofstream(dir + "/" + name) << "// " << name << "\n"; }

int main()
{
    char tmpl[] = "/tmp/lab-js-folder-XXXXXX";
    const char *made = ::mkdtemp(tmpl);
    if (!made) { std::printf("FAIL: no scratch folder\nlab.js_folder: FAILED (1)\n"); return 1; }
    const std::string dir = made;
    lab::ScriptPick p = lab::nextScript(dir, "");
    check(!p.ok && p.message == "no scripts in " + dir, "(1) an empty folder says `" + p.message + "`");
    touch(dir, "b.js");
    p = lab::nextScript(dir, "");
    check(p.ok && p.name == "b.js" && p.message == "1 of 1: b.js", "control: one script is picked (" + p.message + ")");
    // between two presses: three scripts and a note added
    touch(dir, "a.js"); touch(dir, "c.js"); touch(dir, "B.js"); touch(dir, "notes.txt");
    p = lab::nextScript(dir, "b.js");
    check(p.ok && p.message == "4 of 4: c.js", "(2)(3) the files added are seen, bytewise, .js only: after b.js is " + p.message);
    p = lab::nextScript(dir, p.name);
    check(p.ok && p.message == "1 of 4: B.js", "(4) after the last, the first (" + p.message + ")");
    p = lab::nextScript(dir, p.name);
    check(p.ok && p.message == "2 of 4: a.js", "(3) B.js before a.js: bytewise (" + p.message + ")");
    std::remove((dir + "/b.js").c_str());
    p = lab::nextScript(dir, "b.js");
    check(p.ok && p.message == "3 of 3: c.js", "(2) a deleted script is gone, the current one too: after b.js is " + p.message);
    check(lab::scriptPlace(dir, "a.js") == "2 of 3: a.js" && lab::scriptPlace(dir, "gone.js") == "gone.js",
          "a script's place is read as the folder is now");
    for (const char *n : { "a.js", "c.js", "B.js", "notes.txt" }) std::remove((dir + "/" + n).c_str());
    ::rmdir(dir.c_str());
    std::printf("%s\n", gFail ? ("lab.js_folder: FAILED (" + std::to_string(gFail) + ")").c_str() : "lab.js_folder: PASSED");
    return gFail ? 1 : 0;
}
