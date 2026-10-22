#include "labcommands.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace lab {

const std::vector<VerbDoc> &verbs()
{
    static const std::vector<VerbDoc> v = {
        { "camera", "camera x,y,z,tx,ty,tz  (or six words)", 1, 6, "place the fly camera at x,y,z looking at tx,ty,tz (metres)" },
        { "view", "view off|triangles|levels|buckets|objects|density|next", 1, 1,
          "the Atom view over the frame (Scene::setAtomView); refused where the view carries no id pass (the Low tier)" },
        { "tier", "tier low|medium|high|epic|next", 1, 1,
          "the World Mode: every tier row of the document's table (irisgl worldmodes::setMode, the app's own table)" },
        { "gi", "gi on|off|toggle", 1, 1, "Photon on or off at the current tier (worldmodes::setPhoton)" },
        { "occlusion", "occlusion on|off|toggle", 1, 1, "the id pass's depth-pyramid occlusion (Scene::setAtomOcclusionEnabled)" },
        { "draw", "draw on|off|toggle", 1, 1, "the Atom split itself; off = every object through stock PBS (Scene::setAtomDrawEnabled)" },
        { "overlay", "overlay on|off|toggle", 1, 1, "the F3 rows: triangles, occluded, frame ms, tier, view, camera (View::setOverlay)" },
        { "play", "play on|off|toggle", 1, 1,
          "the document's animation clock (Scene::setPlaying; each frame Scene::advance one 1/60 s step): the scene's "
          "animated nodes play - Bistro's, under the stillness door (PHOTON-2 round 2, G6)" },
        { "frames", "frames N", 1, 1, "render N frames (the engine's fixed 1/60 s grid; frames, never time)" },
        { "settle", "settle [maxFrames]", 0, 1,
          "render until the Atom status is steady: nothing pending and the cut's triangles unchanged over 8 frames (cap 1200)" },
        { "shot", "shot [name]", 0, 1, "a PNG of the frame to <data-root>/shots/<name>.png (headless: readPixels; window: the frame capture)" },
        { "status", "status [file]", 0, 1,
          "the engine's AtomDrawStatus + RenderStats + the lab's state as ONE JSON line, appended to <file> (stdout without one)" },
        { "save", "save [file.jlab]", 0, 1, "the session as a .jlab (file or env, camera, tier, view, gi, occlusion, sun)" },
        { "envcam", "envcam D", 1, 1, "an environment's dolly camera at stop D metres (envs/<name>.cpp stop())" },
        { "probe", "probe [file]", 0, 1,
          "the Atom cut probe of the next frame: drawn triangles, clusters and visible pixels per object (appended to file)" },
        { "measure", "measure D [file.tsv]", 1, 2,
          "an environment's stop D: the camera, settle, the probe, and each predicted quantity beside the measured one "
          "(a TSV row each: env, seed, tier, stop, quantity, predicted, measured)" },
        { "move", "move x,y,z,tx,ty,tz N", 2, 2,
          "a scripted camera move: N frames walking from the current pose to that one (eye and target linearly, the "
          "last frame exactly at it); N = 0 is a TELEPORT (one frame drawn at the pose)" },
        { "lamp", "lamp <name> intensity <v> | lamp <name> position x,y,z", 3, 3,
          "a lamp step: the named light's intensity or position written in the document (the mirror pushes it on the "
          "next frame); no frame is drawn" },
        { "material", "material <object> colour r,g,b | material <object> roughness <v>", 3, 3,
          "a material edit (PHOTON-2 fix F5): the named object's PBR base colour (0..1 each) or roughness written in "
          "the document (the mirror pushes it on the next frame); no frame is drawn" },
        { "arm", "arm <name> <value>", 2, 2,
          "a measuring arm (Engine::setArm, the engine's arm registry - e.g. gather.classOff 1); no frame is drawn" },
        { "stillness", "stillness <script|@file> <frames> [path] [age=N] [trail=N] [restarts] [dump=<name>] [freeze] [out=<name>]", 2, 9,
          "THE STILLNESS DOOR (PHOTON-2; irisgl Stillness.h): the move script (teleport:x,y,z,tx,ty,tz / "
          "walk:x,y,z,tx,ty,tz:N / hold:N, '/'-separated, or @file), the stop, then <frames> frames measured: the "
          "per-frame change in 8-bit codes on the pixels the gather saw before the stop (history count >= age, "
          "default 10), per material class (rough, glossy < 0.4, mirror, foliage = alpha-tested (cut-out), mover; by the "
          "environment's objects), the GI cache rows that fired, the gather's rest and the reflection count "
          "histogram; `path` adds the teleport window and the walk-vs-teleport comparison; `trail=N` measures the last N "
          "frames of the move too (every pixel, the ray levels re-fitted); `restarts` adds per frame the gather's foliage "
          "pixels and how many restarted their history (the class bit of its own history word); `dump=<name>` writes "
          "every trail frame's picture, the gather's view distance and surface test, its classes and camera to "
          "<data-root>/stillness/dump-<name>/ (M1's moving window, computed offline); `freeze` switches the arm "
          "gi.freeze on at the script's first walk frame, only when the gather and the reflection HELD on the frame before "
          "(M1's frozen walk from the converged picture; the run fails otherwise - settle longer), restored at the end. "
          "Writes "
          "<data-root>/stillness/<out>.tsv and the windows' last pictures; prints the STILLNESS lines" },
        { "help", "help", 0, 0, "print this table" },
        { "echo", "echo words...", 0, 64, "print the words (an agent's sync marker on stdout)" },
        { "quit", "quit", 0, 0, "end the run" },
    };
    return v;
}

const std::vector<std::pair<std::string, std::string>> &keyTable()
{
    static const std::vector<std::pair<std::string, std::string>> t = {
        { "F1", "help" },
        { "F2", "view next" },
        { "F3", "overlay toggle" },
        { "F4", "tier next" },
        { "F5", "save" },
        { "F6", "occlusion toggle" },
        { "F7", "draw toggle" },
        { "F8", "gi toggle" },
        { "F12", "shot" },
        { "Escape", "quit" },
    };
    return t;
}

std::string keyLine(const std::string &key)
{
    for (const auto &k : keyTable())
        if (k.first == key) return k.second;
    return std::string();
}

const std::vector<std::string> &atomViewNames()
{
    static const std::vector<std::string> n = { "off", "triangles", "levels", "buckets", "objects", "density" };
    return n;
}

const std::vector<std::string> &tierNames()
{
    static const std::vector<std::string> n = { "low", "medium", "high", "epic" };
    return n;
}

static std::string v_usage(const std::string &verb)
{
    for (const VerbDoc &v : verbs())
        if (verb == v.verb) return v.usage;
    return verb;
}

bool parse(const std::string &lineIn, Command &out, std::string &error)
{
    error.clear();
    out = Command();
    std::string line = lineIn;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    out.line = line;
    std::istringstream in(line);
    std::string w;
    if (!(in >> w) || w[0] == '#') return false;
    for (char &c : w) c = char(std::tolower(static_cast<unsigned char>(c)));
    out.verb = w;
    while (in >> w) out.args.push_back(w);
    for (const VerbDoc &v : verbs()) {
        if (out.verb != v.verb) continue;
        if (int(out.args.size()) < v.minArgs || int(out.args.size()) > v.maxArgs) {
            error = "'" + line + "': " + v.usage;
            return false;
        }
        // camera takes "x,y,z,tx,ty,tz" as one word or six: normalised to six words
        if (out.verb == "camera") {
            std::string joined;
            for (const std::string &a : out.args) joined += (joined.empty() ? "" : ",") + a;
            std::vector<std::string> six;
            std::string cur;
            for (char c : joined + ",") {
                if (c == ',') { if (!cur.empty()) six.push_back(cur); cur.clear(); }
                else cur += c;
            }
            if (six.size() != 6) { error = "'" + line + "': " + v.usage; return false; }
            for (const std::string &s : six) {
                char *end = nullptr;
                std::strtod(s.c_str(), &end);
                if (!end || *end) { error = "'" + line + "': '" + s + "' is not a number"; return false; }
            }
            out.args = six;
        }
        // NUMBERS ARE THE GRAMMAR'S: `frames abc` is a parse error, never 0 frames
        const auto number = [&](size_t i, double lo, bool integer) {
            if (i >= out.args.size()) return true;
            char *end = nullptr;
            const double v = std::strtod(out.args[i].c_str(), &end);
            if (!end || *end || v < lo || (integer && v != double((long long)v))) {
                error = "'" + line + "': '" + out.args[i] + "' is not " + (integer ? "a whole number" : "a number") +
                        " >= " + std::to_string((long long)lo) + " (" + v_usage(out.verb) + ")";
                return false;
            }
            return true;
        };
        if (out.verb == "frames" && !number(0, 0, true)) return false;
        if (out.verb == "settle" && !number(0, 1, true)) return false;
        if ((out.verb == "envcam" || out.verb == "measure") && !number(0, 0, false)) return false;
        if (out.verb == "move" && !number(1, 0, true)) return false;
        if (out.verb == "stillness" && !number(1, 2, true)) return false;
        if (out.verb == "arm" && !number(1, -1e9, false)) return false;
        return true;
    }
    error = "'" + line + "': unknown command '" + out.verb + "' (help lists them)";
    return false;
}

std::string helpText()
{
    std::ostringstream o;
    o << "jahlab - the engine-only harness (Atom + Photon + the core engine, no Studio)\n\n"
         "usage: jahlab [flags] <scene.fbx|.obj|.gltf|.glb|.jlab>   |   jahlab [flags] --env <name>\n\n"
         "flags:\n"
         "  --data-root <dir>   bakes, shots, .jlab saves and the shader cache (default ~/.local/share/jahlab)\n"
         "  --headless          render OFFSCREEN (a Vulkan offscreen view, no window; Ogre-Next's Vulkan still opens an X\n"
         "                      connection at boot, so DISPLAY must name a server — the rig's Xvfb)\n"
         "  --script <file>     run the file's command lines after the load (then stdin, unless --no-stdin)\n"
         "  --no-stdin          do not read commands from stdin\n"
         "  --env <name>        a generated test environment instead of a file (--env-list: the catalogue)\n"
         "  --seed N            the environment's seed (default 1)\n"
         "  --env-list          the catalogue: each environment, its law and its prediction at 22/88/177/354 m; exits\n"
         "  --env-check         every environment (or --env NAME's) built and baked (timed, <= 5 s) and again: the same\n"
         "                      triangles, scene hash and bake hash; another seed another hash; exits 0 green, 1 red\n"
         "  --footprint N       the occlusion test's footprint (texels) the expectations assume (default 2: this base)\n"
         "  --bake-only         bake the file into the data root (or find its bake) and exit: CPU only, no engine\n"
         "  --camera x,y,z,tx,ty,tz   the camera at the start\n"
         "  --tier low|medium|high|epic   the World Mode at the start (default epic, a new scene's)\n"
         "  --size WxH          the window / offscreen size (default 1920x1080)\n"
         "  --help              this text\n\n"
         "keys (each runs the command line beside it, through the same parser):\n";
    for (const auto &k : keyTable()) o << "  " << k.first << std::string(k.first.size() < 8 ? 8 - k.first.size() : 1, ' ') << k.second << "\n";
    o << "  W/A/S/D move, Q/E down/up, Shift x4, right mouse button held = look\n\n"
         "commands (--script lines, stdin lines and the keys' lines):\n";
    for (const VerbDoc &v : verbs()) o << "  " << v.usage << "\n      " << v.what << "\n";
    return o.str();
}

}   // namespace lab
