// core.files - THE ONE ATOMIC WRITE's LAWS (NOQT-CORE-1 stage 1; core/files.h): writeAtomic replaces a file WHOLE; a write
// that fails after half its bytes (the file-size limit, in a forked child) leaves the old file byte-equal and no temp; a
// child that dies between the staged write and the rename leaves the old file (its temp seen: the death observed where it
// must be); 8 threads x 100 writes of one file while a reader reads it: every read is one whole version; the staged steps
// (stagingTempPath + fsyncPath + atomicRename) replace an existing file whole, under the shape the library GC reads
// (<target>.tmp-<pid>-<serial>); a non-ASCII UTF-8 path round-trips; readAll of a missing file is nothing. POSIX (fork).
#include "core/files.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
static int failures = 0, checks = 0;
static void check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok && ++failures <= 40) std::printf("FAIL %s\n", what.c_str());
}

static std::vector<fs::path> temps(const fs::path &dir)
{
    std::vector<fs::path> out;
    for (const auto &e : fs::directory_iterator(dir))
        if (e.path().filename().string().find(".tmp-") != std::string::npos) out.push_back(e.path());
    return out;
}

// one whole version: "BEGIN <n>" + n x's + " END <n>"
static std::string version(int n) { return "BEGIN " + std::to_string(n) + " " + std::string(4096 + n, 'x') + " END " + std::to_string(n); }
static bool whole(const std::string &s)
{
    std::smatch m;
    static const std::regex re("^BEGIN (\\d+) x+ END (\\d+)$");
    if (!std::regex_match(s, m, re) || m[1] != m[2]) return false;
    return s.size() == version(std::stoi(m[1])).size();
}

int main()
{
    using namespace iris::files;
    // the row's own scratch, under its working directory (removed at the end)
    std::string tmpl = (fs::current_path() / "core-files-XXXXXX").string();
    const char *base = ::mkdtemp(tmpl.data());
    if (!base) { std::printf("FAIL mkdtemp\ncore.files: FAILED\n"); return 1; }
    const fs::path dir = base;
    const fs::path target = dir / "store.json";

    // ---- a whole replace
    check(writeAtomic(target, "old bytes"), "writeAtomic of a new file");
    check(writeAtomic(target, "the new bytes", Durability::Derived), "writeAtomic over it (Derived)");
    check(readAll(target) == std::string("the new bytes"), "the file is the new bytes, whole");
    check(temps(dir).empty(), "no temp left after a write");
    check(writeAtomic(dir / "deep" / "er" / "a.bin", std::string("\0\1\2", 3)) && readAll(dir / "deep" / "er" / "a.bin")->size() == 3,
          "writeAtomic makes the directories and keeps a NUL byte");

    // ---- a write failing after half its bytes: the file-size limit in a forked child (SIGXFSZ ignored: write fails EFBIG)
    const std::string big(1 << 20, 'n');
    check(writeAtomic(target, "OLD"), "the old file");
    pid_t child = ::fork();
    if (child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        rlimit rl{ static_cast<rlim_t>(big.size() / 2), static_cast<rlim_t>(big.size() / 2) };
        ::setrlimit(RLIMIT_FSIZE, &rl);
        std::string err;
        const bool ok = writeAtomic(target, big, Durability::Flush, &err);
        ::_exit(ok ? 0 : (err.empty() ? 3 : 2));
    }
    int st = 0;
    ::waitpid(child, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 2, "the half write reported its failure with an error (child status " + std::to_string(st) + ")");
    check(readAll(target) == std::string("OLD"), "a write failing after half its bytes left the old file byte-equal");
    check(temps(dir).empty(), "a failed write left no temp");

    // ---- a death between the staged write and the rename
    child = ::fork();
    if (child == 0) {
        const fs::path tmp = stagingTempPath(target);
        std::FILE *f = std::fopen(tmp.c_str(), "wb");
        std::fwrite(big.data(), 1, big.size(), f);
        std::fclose(f);
        fsyncPath(tmp);
        ::_exit(42);                         // dies here: no rename
    }
    ::waitpid(child, &st, 0);
    const auto left = temps(dir);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 42 && left.size() == 1 && fs::file_size(left[0]) == big.size(),
          "the child died between write and rename (status 42, its whole temp seen)");
    check(readAll(target) == std::string("OLD"), "a death between write and rename left the old file");
    for (const auto &t : left) fs::remove(t);

    // ---- 8 threads x 100 writes, a reader reading all along: every read one whole version
    check(writeAtomic(target, version(0)), "version 0");
    std::atomic<bool> done{ false };
    std::atomic<long> reads{ 0 }, torn{ 0 };
    std::thread reader([&] {
        while (!done.load()) {
            const auto s = readAll(target);
            if (!s) continue;                // never: the target always exists
            ++reads;
            if (!whole(*s)) ++torn;
        }
    });
    std::vector<std::thread> writers;
    std::atomic<int> failed{ 0 };
    for (int t = 0; t < 8; ++t)
        writers.emplace_back([&, t] {
            for (int i = 0; i < 100; ++i)
                if (!writeAtomic(target, version(t * 100 + i + 1), Durability::Derived)) ++failed;
        });
    for (auto &w : writers) w.join();
    done = true;
    reader.join();
    check(failed == 0, std::to_string(failed.load()) + " of 800 concurrent writes failed");
    check(reads > 0, "the reader read (" + std::to_string(reads.load()) + " reads)");
    check(torn == 0, std::to_string(torn.load()) + " of " + std::to_string(reads.load()) + " reads were not one whole version");
    check(whole(*readAll(target)) && temps(dir).empty(), "the end: one whole version, no temp");

    // ---- the staged steps, under the shape the library GC reads
    const fs::path staged = stagingTempPath(target);
    check(std::regex_match(staged.filename().string(), std::regex("^store\\.json\\.tmp-" + std::to_string(::getpid()) + "-\\d+$")),
          "stagingTempPath is <target>.tmp-<pid>-<serial>: " + staged.filename().string());
    check(stagingTempPath(target) != stagingTempPath(target), "two staging paths differ");
    {
        std::FILE *f = std::fopen(staged.c_str(), "wb");
        std::fputs("STAGED", f);
        std::fclose(f);
    }
    fsyncPath(staged);
    std::string err;
    check(atomicRename(staged, target, &err) && readAll(target) == std::string("STAGED"), "the staged steps replaced the file whole");
    check(!atomicRename(dir / "no-such.tmp", dir / "x", &err) && err.find("could not place") == 0, "a failed rename says so: " + err);

    // ---- a non-ASCII UTF-8 path; a missing file
    const std::string name = utf8(dir) + "/W\xC3\xBCrfel \xE2\x98\x83.json";
    check(writeAtomic(path(name), "u") && readAll(path(name)) == std::string("u") && utf8(path(name)) == name,
          "a non-ASCII UTF-8 path round-trips");
    check(!readAll(dir / "missing.json"), "readAll of a missing file is nothing");

    fs::remove_all(dir);
    std::printf("%d checks, %d failures; %ld concurrent reads, 0 torn wanted\n", checks, failures, reads.load());
    std::printf("core.files: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
