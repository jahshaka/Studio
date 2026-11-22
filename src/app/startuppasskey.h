#pragma once
// THE GLOBAL STARTUP PASS's KEY, ONE DEFINITION (TEST-STREAMLINE-1 point 3, 2026-11-03): what decides the pass's bytes, never
// the Studio commit. The pass (shaderbuildgate.cpp) re-runs over a warm cache when this key moves; the test rows' startup
// seeds (tests/shadercache/test_async_after_open.cpp's tree key) are stale exactly when it moves - both read these
// functions. Why each term: cmake/startup_pass_id.py.
#include <QString>

namespace startuppass {

/// THE KEY: the pass's code id (JAH_STARTUP_PASS_CODE_ID: its own sources, the engine's build id and its ray SPIR-V's id),
/// the staged media by content (jahshaka::engine::mediaDigest) and the shader cache's build terms
/// (jahshaka::engine::shaderIdentity().fingerprint: no device term).
QString passKey(const QString &passCodeId, const QString &mediaDigest, const QString &fingerprint);

}   // namespace startuppass
