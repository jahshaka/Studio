// core.files - THE ONE ATOMIC WRITE's LAWS (NOQT-CORE-1 stage 1; core/files.h): writeAtomic replaces a file WHOLE; a write
// that fails after half its bytes (the file-size limit, in a forked child) leaves the old file byte-equal and no temp; a
// child that dies between the staged write and the rename leaves the old file (its temp seen: the death observed where it
// must be); 8 threads x 100 writes of one file while a reader reads it: every read is one whole version; the staged steps
// (stagingTempPath + fsyncPath + atomicRename) replace an existing file whole, under the shape the library GC reads
// (<target>.tmp-<pid>-<serial>); a non-ASCII UTF-8 path round-trips; readAll of a missing file is nothing; readAll ANSWERS
// "nothing" and never throws for what it cannot read - a DIRECTORY (on ext4 its seek-to-end says 2^63-1 bytes) and a file
// larger than the memory it may take (a sparse 64-GiB file in a child under a 16-GiB address-space limit); a Flush write
// whose fsync FAILS fails by name and is NOT renamed into place (the old file stands, no temp): the fault is injected by
// this binary's own fsync, which the dynamic linker binds ahead of libc's for libIrisCore's call (no seam in the product;
// its control: the interposer counted the call); an OWNER-ONLY write (Access::OwnerOnly) publishes mode 0600 and its temp
// never existed with a wider one - the one creation of its temp is seen by this binary's own open() (interposed the same
// way), exclusive and 0600 at creation, under a umask of 0; its control: a Default write under that umask publishes 0666.
// POSIX (fork; ELF interposition).
#include "core/files.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// THE FAULT: fsync as the kernel would answer a failed flush (EIO), while gFailFsync is set; else the real call.
static std::atomic<bool> gFailFsync{ false };
static std::atomic<int> gFsyncCalls{ 0 };
extern "C" int fsync(int fd)
{
    ++gFsyncCalls;
    if (gFailFsync.load()) {
        errno = EIO;
        return -1;
    }
    return static_cast<int>(::syscall(SYS_fsync, fd));
}

// THE WITNESS OF A TEMP's CREATION: every open() of this process that CREATES a staged temp ("....tmp-<pid>-<n>"), with its
// mode right after the creation (fstat: the umask applied) and whether it was exclusive; then the real call.
struct Creation
{
    std::string path;
    mode_t mode = 0;
    bool exclusive = false;
};
static std::mutex gCreationsMutex;
static std::vector<Creation> gCreations;
extern "C" int open(const char *file, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    const int fd = static_cast<int>(::syscall(SYS_openat, AT_FDCWD, file, flags, mode));
    if (fd >= 0 && (flags & O_CREAT) && std::strstr(file, ".tmp-")) {
        struct stat st {};
        ::fstat(fd, &st);
        std::lock_guard<std::mutex> lock(gCreationsMutex);
        gCreations.push_back({ file, st.st_mode & 07777, (flags & O_EXCL) != 0 });
    }
    return fd;
}

static mode_t modeOf(const fs::path &p)
{
    struct stat st {};
    return ::stat(p.c_str(), &st) == 0 ? (st.st_mode & 07777) : 07777;
}
static std::string octal(mode_t m)
{
    char b[16];
    std::snprintf(b, sizeof b, "%04o", unsigned(m));
    return b;
}

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
        ::_exit(fsyncPath(tmp) ? 42 : 43);   // dies here: no rename
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
    std::string err;
    check(fsyncPath(staged) && atomicRename(staged, target, &err) && readAll(target) == std::string("STAGED"), "the staged steps replaced the file whole");
    check(!atomicRename(dir / "no-such.tmp", dir / "x", &err) && err.find("could not place") == 0, "a failed rename says so: " + err);

    // ---- a non-ASCII UTF-8 path; a missing file
    const std::string name = utf8(dir) + "/W\xC3\xBCrfel \xE2\x98\x83.json";
    check(writeAtomic(path(name), "u") && readAll(path(name)) == std::string("u") && utf8(path(name)) == name,
          "a non-ASCII UTF-8 path round-trips");
    check(!readAll(dir / "missing.json"), "readAll of a missing file is nothing");

    // ---- a Flush write whose fsync fails is not published
    check(writeAtomic(target, "BEFORE THE FAILED FLUSH"), "the old file");
    gFsyncCalls = 0;
    gFailFsync = true;
    err.clear();
    const bool flushed = writeAtomic(target, "NOT DURABLE", Durability::Flush, &err);
    const bool stagedFlush = fsyncPath(target);
    gFailFsync = false;
    check(gFsyncCalls >= 2, "the interposed fsync was called (" + std::to_string(gFsyncCalls.load()) + " calls)");
    check(!flushed && err.find("fsync") != std::string::npos, "a Flush write whose fsync failed FAILED by name: " + err);
    check(readAll(target) == std::string("BEFORE THE FAILED FLUSH") && temps(dir).empty(),
          "...and was not renamed into place: the old file stands, no temp");
    check(!stagedFlush, "fsyncPath says a failed flush failed");
    check(writeAtomic(target, "DURABLE", Durability::Flush) && readAll(target) == std::string("DURABLE"),
          "the next Flush write, its fsync whole, is published");

    // ---- an owner-only write: 0600 from its temp's creation on, under a umask that would let a default one be 0666
    const mode_t umaskWas = ::umask(0);
    check(writeAtomic(dir / "open.json", "anyone", Durability::Derived) && modeOf(dir / "open.json") == 0666,
          "control: a Default write under umask 0 is published 0666 (" + octal(modeOf(dir / "open.json")) + ")");
    {
        std::lock_guard<std::mutex> lock(gCreationsMutex);
        gCreations.clear();
    }
    const fs::path secret = dir / "token.json";
    check(writeAtomic(secret, "a bearer token", Durability::Derived, nullptr, Access::OwnerOnly) &&
              readAll(secret) == std::string("a bearer token"),
          "an owner-only write publishes its bytes");
    check(modeOf(secret) == 0600, "...at mode 0600 (" + octal(modeOf(secret)) + ")");
    std::vector<Creation> made;
    {
        std::lock_guard<std::mutex> lock(gCreationsMutex);
        for (const Creation &c : gCreations)
            if (fs::path(c.path).filename().string().rfind("token.json.tmp-", 0) == 0) made.push_back(c);
    }
    check(made.size() == 1 && made[0].exclusive && made[0].mode == 0600,
          "...and its temp never existed wider: ONE exclusive creation seen, 0600 at creation (" + std::to_string(made.size()) +
              " seen" + (made.empty() ? std::string() : ", mode " + octal(made[0].mode)) + ")");
    ::umask(umaskWas);

    // ---- what readAll cannot read is nothing, never a throw (the header's promise)
    bool threw = false;
    std::optional<std::string> got;
    try {
        got = readAll(dir);
    } catch (const std::exception &e) {
        threw = true;
        std::printf("readAll(<a directory>) threw: %s\n", e.what());
    }
    check(!threw && !got, "readAll of a directory is nothing, not a throw");
    const fs::path huge = dir / "huge.bin";
    std::error_code sparse;
    std::FILE *empty = std::fopen(huge.c_str(), "wb");
    if (empty) std::fclose(empty);
    fs::resize_file(huge, std::uintmax_t(1) << 36, sparse);   // 64 GiB, no blocks
    check(empty && !sparse && fs::file_size(huge) == (std::uintmax_t(1) << 36), "a sparse 64-GiB file");
    child = ::fork();
    if (child == 0) {
        rlimit as{ rlim_t(16) << 30, rlim_t(16) << 30 };   // 16 GiB of address space: the 64-GiB buffer cannot exist
        ::setrlimit(RLIMIT_AS, &as);
        ::_exit(readAll(huge) ? 3 : 0);                       // a throw escapes: terminate, SIGABRT
    }
    ::waitpid(child, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "readAll of a file larger than its memory is nothing, not a throw (child status " + std::to_string(st) +
              (WIFSIGNALED(st) ? ", killed by signal " + std::to_string(WTERMSIG(st)) : std::string()) + ")");

    fs::remove_all(dir);
    std::printf("%d checks, %d failures; %ld concurrent reads, 0 torn wanted\n", checks, failures, reads.load());
    std::printf("core.files: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
