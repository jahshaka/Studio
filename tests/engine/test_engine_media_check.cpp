// engine.media_check — FORWARD-ONLY-1 D2: the staged tonemap media is checked
// ONCE, at engine init (OgreEngine::ensureHlms -> chain::verifyTonemapMedia),
// and a failure is FATAL and STICKY.
//
// The negative control this lane was missing: a copy of the staged media whose
// HDR/FinalToneMapping pass has NO fragment program. That shape used to pass the
// check vacuously (no parameter block, nothing to test). Asserted:
//   1. the first view refuses with a clear error naming HDR/FinalToneMapping;
//   2. a SECOND view refuses with the same error (sticky — never a
//      half-initialised engine drawing without its shadow node);
//   3. the untouched media creates a view (the positive control).
#include "jahshaka/engine/Engine.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace jahshaka::engine;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static bool stripToneMapProgram(const fs::path &material)
{
    std::ifstream in(material);
    if (!in) return false;
    std::stringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();
    const std::string head = "material HDR/FinalToneMapping :";
    const size_t at = text.find(head);
    if (at == std::string::npos) return false;
    const std::string ref = "fragment_program_ref HDR/FinalToneMapping_ps";
    const size_t r = text.find(ref, at);
    if (r == std::string::npos) return false;
    const size_t open = text.find('{', r);
    const size_t close = text.find('}', open);
    if (open == std::string::npos || close == std::string::npos) return false;
    text.erase(r, close + 1 - r);
    std::ofstream out(material, std::ios::trunc);
    out << text;
    return bool(out);
}

static std::string firstViewError(const std::string &mediaDir, const char *log, bool twice,
                                  std::string *second)
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = mediaDir;
    cfg.logFile = log;
    auto engine = Engine::create(cfg, err);
    if (!engine) return "engine create: " + err;
    View *view = engine->createOffscreenView("media_check", 64, 64, Colour(0, 0, 0, 1));
    const std::string first = view ? std::string() : engine->lastError();
    if (twice && second) {
        View *again = engine->createOffscreenView("media_check_2", 64, 64, Colour(0, 0, 0, 1));
        *second = again ? std::string() : engine->lastError();
    }
    return first;
}

int main()
{
    const fs::path media = JAHSHAKA_TEST_MEDIA_DIR;
    const fs::path scratch = fs::temp_directory_path() / "jahshaka-media-check";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::copy(media, scratch, fs::copy_options::recursive, ec);
    CHECK(!ec, "the staged media copied");
    const fs::path material = scratch / "2.0/scripts/materials/HDR/HDR.material";
    CHECK(stripToneMapProgram(material),
          "the copy's HDR/FinalToneMapping pass lost its fragment program");

    std::string second;
    const std::string first =
        firstViewError(scratch.string() + "/", "test-media-check-broken.log", true, &second);
    std::printf("info: first view error: %s\n", first.c_str());
    CHECK(first.find("HDR/FinalToneMapping") != std::string::npos,
          "a tonemap pass without its fragment program REFUSES the first view, naming it");
    CHECK(second.find("HDR/FinalToneMapping") != std::string::npos,
          "...and a SECOND view refuses with the same error (sticky)");

    const std::string good = firstViewError(media.string(), "test-media-check-good.log", false, nullptr);
    CHECK(good.empty(), "the untouched staged media creates a view (the positive control)");

    fs::remove_all(scratch, ec);
    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
