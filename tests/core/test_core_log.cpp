// core.log - THE ONE LOG's LAWS (NOQT-CORE-1 stage 2; core/log.h):
//  (1) THE LOG SERIALISES ITS RECORDS: 8 threads x 1000 records into an installed sink that is itself UNSYNCHRONISED (it
//      appends each record to ONE shared std::string a character at a time), so only the log's mutex keeps a record whole:
//      every record arrives whole, once, with its severity (info / warn / error by the record's number). CONTROL: the sink
//      was called exactly 8000 times.
//  (2) setSink REPLACES: a record after a second sink is installed reaches the second only; an empty sink is stderr again.
//  (3) setSink IS SERIALISED WITH THE RECORDS: logger thread 0 installs sink A or B before EACH of its own records (A
//      before an even record, B before an odd one) while threads 1-7 log beside it: every record reaches exactly ONE of
//      the two sinks, whole, 8000 in all, and each of thread 0's records reaches the sink thread 0 had just installed.
//      CONTROL: both sinks received records - certain by construction (thread 0 alternates), not by scheduling.
//  (4) THE SINK setSink REPLACES DIES AFTER THE LOG's LOCK: a sink whose capture LOGS from its destructor is replaced;
//      that record reaches the new sink (under the lock it would deadlock on the non-recursive mutex). Run in a forked
//      child bounded by alarm(): a deadlock is a named red, not a hung row. CONTROL: the destructor ran (its record).
//  (5) NO SINK: each record is ONE whole line on stderr - "[info] " / "[warn] " / "[error] " + the text - while another
//      writer of stderr (a noise thread, its own single writes) runs beside it: the log threads start only AFTER the noise
//      thread wrote its first line (an event this test controls), 8 threads x 200 records through a pipe dup'd onto
//      stderr, every line a whole record or a whole noise line. CONTROL: every noise line arrives (at least the first).
//  (6) NO FILE: the test runs in its OWN new temp directory (never a live data root) with HOME, TMPDIR and the XDG_*
//      directories pointed into it before the first record, and the log leaves nothing in it, counted RECURSIVELY.
//      CONTROL: a marker file this test writes in a SUBDIRECTORY there is counted (the count can see a nested file).
// POSIX (pipe, dup2, mkdtemp, fork, alarm).
#include "irisgl/core/log.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using iris::log::Severity;

namespace {

constexpr int kThreads = 8;
constexpr int kRecords = 1000;        // (1), (3)
constexpr int kStderrRecords = 200;   // (5), each thread
constexpr unsigned kDeadlockBoundS = 20;   // (4): the child's work is microseconds; a deadlock never ends

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

/// An UNSYNCHRONISED sink: each record appended to `got` a character at a time (its severity's letter, a yield - slow
/// inside one record - the text, a newline). Only the log's lock keeps its records whole.
iris::log::Sink appender(std::string &got, std::atomic<long> &calls)
{
    return [&got, &calls](Severity s, std::string_view text) {
        calls.fetch_add(1, std::memory_order_relaxed);
        got.push_back(letterOf(s));
        std::this_thread::yield();
        for (char c : text) got.push_back(c);
        got.push_back('\n');
    };
}

/// What the appenders received, judged against kThreads x kRecords: whole records, each once, with its severity.
struct Received { long whole = 0, torn = 0, wrongSeverity = 0, missing = 0, twice = 0; };
Received judge(const std::vector<const std::string *> &gots)
{
    Received r;
    std::vector<int> seen(size_t(kThreads) * kRecords, 0);
    for (const std::string *got : gots)
        for (std::string_view line : linesOf(*got)) {
            int t = 0, i = 0;
            if (line.empty() || !parseText(line.substr(1), kRecords, &t, &i)) {
                if (++r.torn <= 3)
                    std::printf("  torn record: \"%.*s\"\n", int(std::min<size_t>(line.size(), 90)), line.data());
                continue;
            }
            if (line[0] != letterOf(severityOf(i))) { ++r.wrongSeverity; continue; }
            ++seen[size_t(t) * kRecords + size_t(i)];
            ++r.whole;
        }
    for (int n : seen) { if (n == 0) ++r.missing; if (n > 1) ++r.twice; }
    return r;
}

void runLoggers()
{
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
        ts.emplace_back([t] { for (int i = 0; i < kRecords; ++i) logOne(t, i); });
    for (auto &th : ts) th.join();
}

/// A capture that LOGS as it dies (case 4).
struct LogsOnDeath
{
    ~LogsOnDeath() { iris::log::info("the old sink's capture, dying"); }
};

int filesUnder(const fs::path &dir)
{
    int n = 0;
    for (const auto &e : fs::recursive_directory_iterator(dir))
        if (!e.is_directory()) ++n;
    return n;
}

}   // namespace

int main()
{
    int failures = 0;
    const auto fail = [&](const std::string &what) { std::printf("FAIL %s\n", what.c_str()); ++failures; };

    // (6)'s ground: our own new directory - the working directory, HOME, TMPDIR and every XDG base - before any record.
    const fs::path cwd0 = fs::current_path();
    std::string tmpl = (fs::temp_directory_path() / "core-log-XXXXXX").string();
    if (!mkdtemp(tmpl.data())) { std::printf("FAIL mkdtemp\ncore.log: FAILED (1)\n"); return 1; }
    const fs::path dir = tmpl;
    fs::current_path(dir);
    for (const char *v : { "HOME", "TMPDIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME",
                           "XDG_RUNTIME_DIR" })
        setenv(v, dir.c_str(), 1);

    // ---- (1) the log serialises its records ---------------------------------------------------------------------------
    {
        std::string got;
        got.reserve(size_t(kThreads) * kRecords * 80);   // no reallocation: a torn record is the sink's race, not a crash
        std::atomic<long> calls{ 0 };
        iris::log::setSink(appender(got, calls));
        runLoggers();
        iris::log::setSink(nullptr);
        const Received r = judge({ &got });
        std::printf("(1) sink: %ld calls (control: %d), %ld whole records, %ld torn, %ld with the wrong severity, %ld "
                    "missing, %ld twice\n", calls.load(), kThreads * kRecords, r.whole, r.torn, r.wrongSeverity,
                    r.missing, r.twice);
        if (calls.load() != kThreads * kRecords) fail("(1) the sink was not called exactly 8000 times (the control)");
        if (r.torn) fail("(1) a record reached the sink torn: the log did not serialise its records");
        if (r.wrongSeverity) fail("(1) a record reached the sink with the wrong severity");
        if (r.missing || r.twice || r.whole != kThreads * kRecords) fail("(1) not every record arrived exactly once");
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

    // ---- (3) setSink is serialised with the records --------------------------------------------------------------------
    {
        std::string gotA, gotB;
        gotA.reserve(size_t(kThreads) * kRecords * 80);
        gotB.reserve(size_t(kThreads) * kRecords * 80);
        std::atomic<long> callsA{ 0 }, callsB{ 0 };
        const iris::log::Sink sinkA = appender(gotA, callsA), sinkB = appender(gotB, callsB);
        iris::log::setSink(sinkA);
        std::vector<std::thread> ts;
        ts.emplace_back([&] {   // thread 0: the swapper, between its own records
            for (int i = 0; i < kRecords; ++i) {
                iris::log::setSink(i % 2 ? sinkB : sinkA);
                logOne(0, i);
            }
        });
        for (int t = 1; t < kThreads; ++t)
            ts.emplace_back([t] { for (int i = 0; i < kRecords; ++i) logOne(t, i); });
        for (auto &th : ts) th.join();
        iris::log::setSink(nullptr);
        const Received r = judge({ &gotA, &gotB });
        // thread 0's record i must sit in the sink it installed just before it: A for even i, B for odd
        long misplaced = 0;
        for (const auto &[got, odd] : { std::pair<const std::string *, int>{ &gotA, 0 }, { &gotB, 1 } })
            for (std::string_view line : linesOf(*got)) {
                int tt = 0, i = 0;
                if (!line.empty() && parseText(line.substr(1), kRecords, &tt, &i) && tt == 0 && i % 2 != odd) ++misplaced;
            }
        std::printf("(3) swapping: thread 0 swapped %d times; sink A %ld calls, sink B %ld (control: both > 0); %ld whole "
                    "records, %ld torn, %ld with the wrong severity, %ld missing, %ld twice; %ld of thread 0's records "
                    "in the other sink\n", kRecords, callsA.load(), callsB.load(), r.whole, r.torn, r.wrongSeverity,
                    r.missing, r.twice, misplaced);
        if (callsA.load() <= 0 || callsB.load() <= 0) fail("(3) the control: one sink received nothing");
        if (callsA.load() + callsB.load() != kThreads * kRecords) fail("(3) the sinks were not called 8000 times in all");
        if (r.torn) fail("(3) a record reached a sink torn while setSink swapped them");
        if (r.wrongSeverity || r.missing || r.twice || r.whole != kThreads * kRecords)
            fail("(3) not every record reached exactly one sink exactly once");
        if (misplaced) fail("(3) a record of thread 0 reached a sink other than the one it had just installed");
    }

    // ---- (4) the replaced sink dies after the log's lock ------------------------------------------------------------
    {
        std::fflush(stdout);
        const pid_t child = fork();
        if (child == 0) {
            alarm(kDeadlockBoundS);   // a deadlock ends here, by SIGALRM: the parent names it
            std::vector<std::string> fresh;
            {
                auto dying = std::make_shared<LogsOnDeath>();
                iris::log::setSink([dying](Severity, std::string_view) {});
            }   // the sink now holds the only reference
            iris::log::setSink([&fresh](Severity, std::string_view t) { fresh.emplace_back(t); });
            iris::log::setSink(nullptr);
            const bool ok = fresh.size() == 1 && fresh[0] == "the old sink's capture, dying";
            std::printf("(4) child: the new sink got %zu record(s)%s\n", fresh.size(),
                        fresh.empty() ? "" : (" - \"" + fresh[0] + "\"").c_str());
            std::fflush(stdout);
            _exit(ok ? 0 : 1);
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFSIGNALED(status))
            fail("(4) setSink deadlocked replacing a sink whose capture logs as it dies (the child ended by signal " +
                 std::to_string(WTERMSIG(status)) + " after its " + std::to_string(kDeadlockBoundS) + "-s bound)");
        else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            fail("(4) the dying capture's record did not reach the new sink exactly once");
        else
            std::printf("(4) the replaced sink's capture logged as it died, after the lock: its record reached the new sink\n");
    }

    // ---- (5) no sink: one whole prefixed line on stderr, beside another writer of stderr ------------------------------
    {
        int fds[2];
        if (pipe(fds) != 0) { fail("(5) pipe"); }
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
            std::atomic<bool> noiseStarted{ false }, done{ false };
            long noiseWritten = 0;
            std::thread noise([&] {
                do {
                    std::fwrite("noise\n", 1, 6, stderr);   // another writer of stderr: one write a line
                    ++noiseWritten;
                    noiseStarted.store(true);
                } while (!done.load());
            });
            while (!noiseStarted.load()) std::this_thread::yield();   // the log starts after the noise's first line
            std::vector<std::thread> ts;
            for (int t = 0; t < kThreads; ++t)
                ts.emplace_back([t] { for (int i = 0; i < kStderrRecords; ++i) logOne(t, i); });
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
                    if (++bad <= 3)
                        std::printf("  not a whole line: \"%.*s\"\n", int(std::min<size_t>(line.size(), 90)), line.data());
                    continue;
                }
                ++seen[size_t(t) * kStderrRecords + size_t(i)];
                ++whole;
            }
            long missing = 0, twice = 0;
            for (int n : seen) { if (n == 0) ++missing; if (n > 1) ++twice; }
            std::printf("(5) stderr: %ld whole prefixed lines of %d, %ld not whole, %ld missing, %ld twice; noise %ld read "
                        "of %ld written (control >= 1)\n", whole, kThreads * kStderrRecords, bad, missing, twice, noiseRead,
                        noiseWritten);
            if (bad) fail("(5) a line on stderr was not one whole record with its prefix (or one whole noise line)");
            if (missing || twice || whole != kThreads * kStderrRecords) fail("(5) not every record reached stderr exactly once");
            if (noiseWritten < 1 || noiseRead != noiseWritten) fail("(5) the noise control: not every noise line was read");
        }
    }

    // ---- (6) no file ---------------------------------------------------------------------------------------------------
    {
        const int left = filesUnder(dir);
        fs::create_directories(dir / "control" / "nested");
        { std::ofstream(dir / "control" / "nested" / "marker") << "x"; }
        const int withMarker = filesUnder(dir);
        fs::remove_all(dir / "control");
        std::printf("(6) files the log left under its directory (HOME, TMPDIR, XDG_*, the working directory): %d "
                    "(control: with this test's nested marker %d)\n", left, withMarker);
        if (left != 0) {
            for (const auto &e : fs::recursive_directory_iterator(dir))
                if (!e.is_directory()) std::printf("  left: %s\n", fs::relative(e.path(), dir).string().c_str());
            fail("(6) the log created a file");
        }
        if (withMarker != left + 1) fail("(6) the control: the recursive count did not see the nested marker");
    }

    fs::current_path(cwd0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (failures) std::printf("core.log: FAILED (%d)\n", failures);
    else std::printf("core.log: PASSED\n");
    return failures ? 1 : 0;
}
