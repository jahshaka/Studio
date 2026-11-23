// core.log - THE ONE LOG's LAWS (NOQT-CORE-1 stage 2; core/log.h):
//  (1) THE LOG SERIALISES ITS RECORDS: 8 threads x 1000 records into an installed sink that is itself UNSYNCHRONISED (it
//      appends each record to ONE shared std::string a character at a time), so only the log's mutex keeps a record whole:
//      every record arrives whole, once, with its severity (info / warn / error by the record's number). CONTROL: the sink
//      was called exactly 8000 times.
//  (2) setSink REPLACES: a record after a second sink is installed reaches the second only; an empty sink is stderr again.
//  (3) NO SINK: each record is ONE whole line on stderr - "[info] " / "[warn] " / "[error] " + the text - while another
//      writer of stderr (a noise thread, its own single writes) runs beside it: 8 threads x 200 records through a pipe
//      dup'd onto stderr, every line a whole record or a whole noise line. CONTROL: the noise thread's lines all arrive
//      (more than zero).
//  (4) NO FILE: the test runs in its OWN new temp directory (never a live data root) and the log leaves nothing in it.
//      CONTROL: a marker file this test writes there is counted (the count can see a file).
// POSIX (pipe, dup2, mkdtemp).
#include "irisgl/core/log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using iris::log::Severity;

namespace {

constexpr int kThreads = 8;
constexpr int kRecords = 1000;        // (1)
constexpr int kStderrRecords = 200;   // (3), each thread

Severity severityOf(int i) { return i % 3 == 0 ? Severity::Info : i % 3 == 1 ? Severity::Warning : Severity::Error; }
char letterOf(Severity s) { return s == Severity::Info ? 'I' : s == Severity::Warning ? 'W' : 'E'; }
const char *prefixOf(Severity s) { return s == Severity::Info ? "[info] " : s == Severity::Warning ? "[warn] " : "[error] "; }

/// Record i of thread t: "t<t> r<i> " + 48 letters that are a function of (t, i), so a torn or spliced record never
/// reads as another whole one.
std::string textOf(int t, int i)
{
    std::string s = "t" + std::to_string(t) + " r" + std::to_string(i) + " ";
    unsigned h = unsigned(t) * 2654435761u + unsigned(i) * 40503u + 1u;
    for (int k = 0; k < 48; ++k) {
        h = h * 1664525u + 1013904223u;
        s.push_back(char('a' + (h >> 24) % 26));
    }
    return s;
}

void logOne(int t, int i)
{
    const std::string text = textOf(t, i);
    switch (severityOf(i)) {
    case Severity::Info:    iris::log::info(text); break;
    case Severity::Warning: iris::log::warn(text); break;
    case Severity::Error:   iris::log::error(text); break;
    }
}

/// (t, i) of a record's text, or false when it is not textOf(t, i) for any t < kThreads, i < `records`.
bool parseText(std::string_view s, int records, int *t, int *i)
{
    int tt = -1, ii = -1, n = 0;
    const std::string head(s.substr(0, 24));
    if (std::sscanf(head.c_str(), "t%d r%d %n", &tt, &ii, &n) != 2 || n == 0) return false;
    if (tt < 0 || tt >= kThreads || ii < 0 || ii >= records) return false;
    if (s != textOf(tt, ii)) return false;
    *t = tt;
    *i = ii;
    return true;
}

std::vector<std::string_view> linesOf(const std::string &all)
{
    std::vector<std::string_view> out;
    size_t at = 0;
    while (at < all.size()) {
        const size_t nl = all.find('\n', at);
        if (nl == std::string::npos) { out.emplace_back(all.data() + at, all.size() - at); break; }
        out.emplace_back(all.data() + at, nl - at);
        at = nl + 1;
    }
    return out;
}

int filesIn(const fs::path &dir)
{
    int n = 0;
    for (const auto &e : fs::directory_iterator(dir)) { (void)e; ++n; }
    return n;
}

}   // namespace

int main()
{
    int failures = 0;
    const auto fail = [&](const std::string &what) { std::printf("FAIL %s\n", what.c_str()); ++failures; };

    // (4)'s ground: our own new directory, the working directory for the whole run.
    const fs::path cwd0 = fs::current_path();
    std::string tmpl = (fs::temp_directory_path() / "core-log-XXXXXX").string();
    if (!mkdtemp(tmpl.data())) { std::printf("FAIL mkdtemp\ncore.log: FAILED (1)\n"); return 1; }
    const fs::path dir = tmpl;
    fs::current_path(dir);

    // ---- (1) the log serialises its records ---------------------------------------------------------------------------
    {
        std::string got;
        got.reserve(size_t(kThreads) * kRecords * 80);   // no reallocation: a torn record is the sink's race, not a crash
        std::atomic<long> calls{ 0 };
        iris::log::setSink([&](Severity s, std::string_view text) {
            calls.fetch_add(1, std::memory_order_relaxed);
            got.push_back(letterOf(s));
            std::this_thread::yield();   // a sink that is slow inside one record: only the log's lock keeps it whole
            for (char c : text) got.push_back(c);
            got.push_back('\n');
        });
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t)
            ts.emplace_back([t] { for (int i = 0; i < kRecords; ++i) logOne(t, i); });
        for (auto &th : ts) th.join();
        iris::log::setSink(nullptr);

        std::vector<int> seen(size_t(kThreads) * kRecords, 0);
        long whole = 0, torn = 0, wrongSeverity = 0;
        for (std::string_view line : linesOf(got)) {
            int t = 0, i = 0;
            if (line.empty() || !parseText(line.substr(1), kRecords, &t, &i)) {
                if (++torn <= 3) std::printf("  torn record: \"%.*s\"\n", int(std::min<size_t>(line.size(), 90)), line.data());
                continue;
            }
            if (line[0] != letterOf(severityOf(i))) { ++wrongSeverity; continue; }
            ++seen[size_t(t) * kRecords + size_t(i)];
            ++whole;
        }
        long missing = 0, twice = 0;
        for (int n : seen) { if (n == 0) ++missing; if (n > 1) ++twice; }
        std::printf("(1) sink: %ld calls (control: %d), %ld whole records, %ld torn, %ld with the wrong severity, %ld "
                    "missing, %ld twice\n", calls.load(), kThreads * kRecords, whole, torn, wrongSeverity, missing, twice);
        if (calls.load() != kThreads * kRecords) fail("(1) the sink was not called exactly 8000 times (the control)");
        if (torn) fail("(1) a record reached the sink torn: the log did not serialise its records");
        if (wrongSeverity) fail("(1) a record reached the sink with the wrong severity");
        if (missing || twice || whole != kThreads * kRecords) fail("(1) not every record arrived exactly once");
    }

    // ---- (2) setSink replaces ----------------------------------------------------------------------------------------
    {
        std::vector<std::string> a, b;
        iris::log::setSink([&a](Severity, std::string_view t) { a.emplace_back(t); });
        iris::log::warn("to the first");
        iris::log::setSink([&b](Severity, std::string_view t) { b.emplace_back(t); });
        iris::log::warn("to the second");
        iris::log::setSink(nullptr);
        std::printf("(2) replace: the first sink got %zu record(s), the second %zu\n", a.size(), b.size());
        if (a.size() != 1 || a[0] != "to the first") fail("(2) the first sink did not get exactly its one record");
        if (b.size() != 1 || b[0] != "to the second") fail("(2) the second sink did not get exactly its one record");
    }

    // ---- (3) no sink: one whole prefixed line on stderr, beside another writer of stderr ------------------------------
    {
        int fds[2];
        if (pipe(fds) != 0) { fail("(3) pipe"); }
        else {
            std::fflush(stderr);
            const int saved = dup(2);
            dup2(fds[1], 2);
            close(fds[1]);
            std::string out;
            std::thread reader([&] {
                char buf[65536];
                for (;;) {
                    const ssize_t n = read(fds[0], buf, sizeof buf);
                    if (n <= 0) break;
                    out.append(buf, size_t(n));
                }
            });
            std::atomic<bool> go{ false }, done{ false };
            long noiseWritten = 0;
            std::thread noise([&] {
                while (!go.load()) std::this_thread::yield();
                while (!done.load()) {
                    std::fwrite("noise\n", 1, 6, stderr);   // another writer of stderr: one write a line
                    ++noiseWritten;
                }
            });
            std::vector<std::thread> ts;
            for (int t = 0; t < kThreads; ++t)
                ts.emplace_back([t, &go] {
                    while (!go.load()) std::this_thread::yield();
                    for (int i = 0; i < kStderrRecords; ++i) logOne(t, i);
                });
            go.store(true);
            for (auto &th : ts) th.join();
            done.store(true);
            noise.join();
            std::fflush(stderr);
            dup2(saved, 2);   // the pipe's last write end closes: the reader sees its end
            close(saved);
            reader.join();
            close(fds[0]);

            std::vector<int> seen(size_t(kThreads) * kStderrRecords, 0);
            long whole = 0, noiseRead = 0, bad = 0;
            for (std::string_view line : linesOf(out)) {
                if (line == "noise") { ++noiseRead; continue; }
                int t = 0, i = 0;
                bool ok = false;
                for (Severity s : { Severity::Info, Severity::Warning, Severity::Error }) {
                    const std::string_view p = prefixOf(s);
                    if (line.substr(0, p.size()) == p && parseText(line.substr(p.size()), kStderrRecords, &t, &i)
                        && s == severityOf(i)) { ok = true; break; }
                }
                if (!ok) {
                    if (++bad <= 3) std::printf("  not a whole line: \"%.*s\"\n", int(std::min<size_t>(line.size(), 90)), line.data());
                    continue;
                }
                ++seen[size_t(t) * kStderrRecords + size_t(i)];
                ++whole;
            }
            long missing = 0, twice = 0;
            for (int n : seen) { if (n == 0) ++missing; if (n > 1) ++twice; }
            std::printf("(3) stderr: %ld whole prefixed lines of %d, %ld not whole, %ld missing, %ld twice; noise %ld read "
                        "of %ld written (control > 0)\n", whole, kThreads * kStderrRecords, bad, missing, twice, noiseRead,
                        noiseWritten);
            if (bad) fail("(3) a line on stderr was not one whole record with its prefix (or one whole noise line)");
            if (missing || twice || whole != kThreads * kStderrRecords) fail("(3) not every record reached stderr exactly once");
            if (noiseWritten <= 0 || noiseRead != noiseWritten) fail("(3) the noise control: not every noise line was read");
        }
    }

    // ---- (4) no file ---------------------------------------------------------------------------------------------------
    {
        const int left = filesIn(dir);
        { std::ofstream(dir / "marker") << "x"; }
        const int withMarker = filesIn(dir);
        fs::remove(dir / "marker");
        std::printf("(4) files the log left in its working directory: %d (control: with this test's marker %d)\n", left,
                    withMarker);
        if (left != 0) {
            for (const auto &e : fs::directory_iterator(dir)) if (e.path().filename() != "marker")
                std::printf("  left: %s\n", e.path().filename().string().c_str());
            fail("(4) the log created a file");
        }
        if (withMarker != left + 1) fail("(4) the control: the count did not see the marker");
    }

    fs::current_path(cwd0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (failures) std::printf("core.log: FAILED (%d)\n", failures);
    else std::printf("core.log: PASSED\n");
    return failures ? 1 : 0;
}
