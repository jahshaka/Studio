#pragma once
// THE GLOBAL STARTUP PASS's KEY, ONE DEFINITION (TEST-STREAMLINE-1 point 3, 2026-11-03): what decides the pass's bytes, never
// the Studio commit. The pass (shaderbuildgate.cpp) re-runs over a warm cache when this key moves; the test rows' startup
// seeds (tests/shadercache/test_async_after_open.cpp's tree key) are stale exactly when it moves - both read these
// functions. Why each term: cmake/startup_pass_id.py.
#include <QString>

namespace startuppass {

/// THE STAGED MEDIA BY CONTENT: every file under `dir` (the binary's media/), by its path RELATIVE to `dir` and its
/// bytes, sorted - the low-level material scripts, the compositors, the compute and Hlms media the pass compiles. Never
/// a tree's path (#154: two trees at one content read one digest), never a file's time. Hex sha256; "none" for no dir.
QString mediaDigest(const QString &dir);

/// THE KEY: the pass's code id (JAH_STARTUP_PASS_CODE_ID: its own sources + the engine's full build id), the staged media
/// by content and the engine shader cache's fingerprint (the fork, the templates, the GPU, the cache's own code).
QString passKey(const QString &passCodeId, const QString &mediaDigest, const QString &fingerprint);

}   // namespace startuppass
