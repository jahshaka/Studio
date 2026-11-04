// engine.route_declarations — THE ROUTE DECLARATIONS' LIFETIME (PHOTON-7-9A, the reader's audit of R7;
// tests/support/enginetesthelpers.h). CPU: the declarations are a list in the helper, judged without a device - no
// engine is created and no view is ever read (the pointers below are never dereferenced).
// LAWS: (forget) a view's pending declarations are dropped when it dies (enginetest::destroyView ->
// framesdetail::forgetRoutes), each said as a FAIL by name, the others kept - the frame verb then never reads a freed
// view; (exit) a declaration still pending when the process ends is a FAIL by name and the exit is 1, never a green.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../support/enginetesthelpers.h"

using namespace enginetest;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main(int argc, char **argv)
{
    View *a = reinterpret_cast<View *>(std::uintptr_t(0x10)), *b = reinterpret_cast<View *>(std::uintptr_t(0x20));
    if (argc == 2 && std::strcmp(argv[1], "child") == 0) {
        declareRoutes(a, nullptr, Routes().gather().reflection(), "the child's never-asserted view");
        return 0;   // the exit judge must turn this into 1
    }
    if (argc == 2 && std::strcmp(argv[1], "forget") == 0) {
        declareRoutes(a, nullptr, Routes().gather(), "view A");
        declareRoutes(b, nullptr, Routes().reflection(), "view B");
        check(framesdetail::forgetRoutes(a) == 1, "view A's death drops its one pending declaration");
        const auto &d = framesdetail::declaredRoutes();
        check(d.size() == 1 && d[0].what == "view B", "view B's declaration is kept");
        check(framesdetail::forgetRoutes(a) == 0, "a second death of A drops nothing");
        check(framesdetail::forgetRoutes(b) == 1 && d.empty(), "B's death empties the list (the exit judge then passes)");
        std::printf("%s\n", gFail ? "engine.route_declarations (forget): FAILED" : "engine.route_declarations (forget): PASSED");
        return gFail ? 1 : 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "exit") == 0) {
        const std::string cmd = std::string("'") + argv[0] + "' child 2>&1";
        FILE *p = popen(cmd.c_str(), "r");
        std::string out;
        char buf[512];
        while (p && std::fgets(buf, sizeof buf, p)) out += buf;
        const int st = p ? pclose(p) : -1;
        const int rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        std::printf("the child: exit %d, said: %s", rc, out.c_str());
        check(rc == 1, "a declaration pending at the process's end exits 1 (the child returned 0)");
        check(out.find("FAIL: the child's never-asserted view declared {gather reflection}") != std::string::npos,
              "...and is a FAIL by name with its routes");
        std::printf("%s\n", gFail ? "engine.route_declarations (exit): FAILED" : "engine.route_declarations (exit): PASSED");
        return gFail ? 1 : 0;
    }
    std::printf("FAIL: usage: test_route_declarations forget|exit\n");
    return 2;
}
