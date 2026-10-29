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
        { "reflections", "reflections both|rays|screen", 1, 1,
          "the view's reflection sources (PostFxDesc::reflectSources): both = the one estimator (the screen trace first, "
          "the rays for the rest), rays = the rays alone, screen = the screen alone (its misses no sample) - the agreement "
          "row's door (Both vs Rays); a graph rebuild, the histories restart" },
        { "photon", "photon off|voxels|probes|cards|screenProbes|diffuse|reflections|hits|gatherHits", 1, 1,
          "the scene's Photon view (Scene::setPhotonView): 'reflections' shows the specular environment term alone - "
          "rtreflect.contact's crease arm reads the reflection there, free of the receiver's own diffuse" },
        { "frames", "frames N", 1, 1, "render N frames (the engine's fixed 1/60 s grid; frames, never time)" },
        { "settle", "settle [maxFrames]", 0, 1,
          "render until the Atom status is steady: nothing pending and the cut's triangles unchanged over 8 frames (cap 1200)" },
        { "shot", "shot [name]", 0, 1, "a PNG of the frame to <data-root>/shots/<name>.png (headless: readPixels; window: the frame capture)" },
        { "status", "status [file]", 0, 1,
          "the engine's AtomDrawStatus + RenderStats + the lab's state as ONE JSON line, appended to <file> (stdout without one)" },
        { "save", "save [file.jlab]", 0, 1, "the session as a .jlab (file or env, camera, tier, view, gi, occlusion, sun)" },
        { "cards", "cards [file]", 0, 1,
          "the surface cache's resident cards per object: its name, the cards resident and their faces (+x -x +y -y +z "
          "-z), the frame each face was last read (Scene::readCardRadiance; PHOTON-7 stage 5's residency by the "
          "readers' demand), appended to <file> (stdout without one)" },
        { "hemi", "hemi x,y,z nx,ny,nz N", 3, 3,
          "THE IRRADIANCE AT A CARD TEXEL THREE WAYS at one frame (REDS-1): (a) the card's Gather layer (its TLAS gather's "
          "envColourD), (b) the screen probes' irradiance at the pixel the texel projects to (the HDR pixel over albedo x "
          "DA(N.V, r): run it in `photon diffuse`), (c) a brute-force hemisphere of N x N stratified cosine rays through the "
          "TLAS from the texel, a hit on photon-contact's mirror floor continued once by reflection, a diffuse hit read from "
          "its card (stored radiance, no view restore)" },
        { "cardat", "cardat x,y,z nx,ny,nz", 2, 2,
          "THE CARD AT A WORLD POINT against the drawn pixel (REDS-1): Scene::readCardAt at the point and normal - the "
          "texel's stored radiance (its direct at V = N, its indirect diffuse half, its flat), its point and roughness - "
          "the INDIRECT half toward the camera (x DA(N.V, r) / DA_hemi(r), the card read's environment view term, the "
          "lobe's closed form) and the HDR pixel the texel's point projects to (its own and its 3 x 3 mean; the view's "
          "float readback is turned on and two frames drawn first). Run it in `photon diffuse` to hold the card's "
          "indirect half against the drawn indirect diffuse" },
        { "sideshares", "sideshares file", 1, 1,
          "cascade 0's surface voxels as the ray tier's sides' gather last left them (PHOTON-7 11-2 (6), the openings' "
          "instrument): one line a written voxel - its cell, its world centre, its normal (two-sided: the front's), the "
          "front and back escape shares (Scene::giVoxelSideShares) - to <file>" },
        { "pick", "pick x y frames file", 4, 4,
          "THE PICK (PHOTON-7 11-2, the reds' instrument): the reflection's trace texel holding full-resolution pixel (x, y) "
          "kept over `frames` frames (at most 60) - each frame's sample (its direction, hit distance and slot, the traced "
          "radiance, C2's traced lobe, the NEE part, the record and its flags) and the temporal pass's rest terms (the "
          "count v, the stop count, the standard error e against its share, the answer, the sample) - one line a frame "
          "to <file> (the arm reflect.pick)" },
        { "colourpick", "colourpick file", 1, 1,
          "THE COLOUR PICK's RING (BUGS-8, the arm colour.pick = 1 + x + 4096 y, set before the frames it reads - e.g. "
          "before a `stillness`): every recorded frame of the last 256, by engine frame, and each fragment the colour "
          "passes drew at the pixel - its template (pbs / atom), fog and blend, depth, target size, the pass-buffer lights' "
          "sum (the colour before the first Forward+ lamp), each Forward+ lamp's attenuation after its shadow ray and the "
          "colour before the second, the colour before the environment's BRDF with the diffuse and specular environment, "
          "the output before the fog law and after it, its alpha and the fog's lit share - one line a fragment to <file>" },
        { "histdump", "histdump file", 1, 1,
          "the reflection's per-texel history count (the state's N / 8 + v, the arm reflect.historyReadback's copy; the "
          "trace's grid, the eyes side by side) as one header line (W H frame) and one row of counts a line to <file> - "
          "two dumps' difference names the texels still converging (PHOTON-7 11-2, with `pick`)" },
        { "envcam", "envcam D", 1, 1, "an environment's dolly camera at stop D metres (envs/<name>.cpp stop())" },
        { "probe", "probe [file]", 0, 1,
          "the Atom cut probe of the next frame: drawn triangles, clusters and visible pixels per object, and its visible pixels by DAG depth (appended to file)" },
        { "measure", "measure D [file.tsv]", 1, 2,
          "an environment's stop D: the camera, settle, the probe, and each predicted quantity beside the measured one "
          "(a TSV row each: env, seed, tier, stop, quantity, predicted, measured)" },
        { "rays", "rays auto|on|off", 1, 1,
          "the document's Ray Tracing (iris::RayTracingMode, the mirror pushes it with the next frame): off = the no-ray "
          "route at any tier - the voxel chain's reflection and the no-ray area visibility (PHOTON-7: its march priced "
          "by `monitor`)" },
        { "monitor", "monitor <frames> <file> [x,y,z,tx,ty,tz]", 2, 3,
          "THE GPU MONITOR: render N frames on the fixed 1/60 s grid (still, or walking from the current pose to the one "
          "given, linearly, the last frame at it) with the engine's frame monitor on; each frame appends ONE line to "
          "<file> - its index, the engine frame, the frame's GPU span, idle and busy, the TOP-LEVEL passes' and rows' sums "
          "and the unnamed gaps, their OVERLAP over the busy span (time two top-level rows count twice: a row whose "
          "begin timestamp opened while earlier work still ran - a barrier's wait), then every compositor pass "
          "(pass:<node>/<pass>, nested: inside another) and every GPU row (row:<name> - injection, cards, gather, "
          "integrate, reflection, area visibility... - nested-row: inside a pass) in ms, and the frame's counts (count:vct."
          "bricks_passed<c> and vct.inject_marched<c> read back, landing a few frames late; cards.relit_texels_direct / "
          "_indirect and captured_texels), the frame's cascade rebuilds and shader compiles, and its untimed work "
          "(untimed:<name>=units: work inside another row's time); then "
          "one SUMMARY line per name: frames, mean, p95, max. The frames after the N that wait for the records' GPU "
          "timestamps are not measured" },
        { "move", "move x,y,z,tx,ty,tz N", 2, 2,
          "a scripted camera move: N frames walking from the current pose to that one (eye and target linearly, the "
          "last frame exactly at it); N = 0 is a TELEPORT (one frame drawn at the pose)" },
        { "lamp", "lamp <name> intensity <v> | lamp <name> position x,y,z", 3, 3,
          "a lamp step: the named light's intensity or position written in the document (the mirror pushes it on the "
          "next frame); no frame is drawn" },
        { "material", "material <object> colour r,g,b | material <object> roughness <v>", 3, 3,
          "a material edit (PHOTON-2 fix F5): the named object's PBR base colour (0..1 each) or roughness written in "
          "the document (the mirror pushes it on the next frame); no frame is drawn" },
        { "pin", "pin <c|all> x,y,z [scroll] | pin off", 1, 3,
          "THE GI PLACEMENT PIN (PHOTON-7 stage 1; Scene::setGiPlacementPin, the measuring door): cascade c (or every "
          "cascade) placed for the camera position x,y,z, every other cascade pinned where it stands (its builtCam); "
          "then a forced WHOLE rebuild of each pinned cascade at its pin (the arm gi.rebuildCascade, one frame each) - "
          "`scroll` leaves the re-placement to the scroll path instead (the pinned-scroll arm); `off` releases the pin" },
        { "voxels", "voxels <c> <direct|light|back|albedo|covp|covn|posp|posn|stats> [file] [x0,y0,z0,x1,y1,z1]", 2, 4,
          "ONE CASCADE'S VOXELS (PHOTON-7 stage 1; Scene::giVoxelVolume / giVoxelStats), read once the chain owes no "
          "voxel work (GiStatus::chainOwed; frames rendered while it does, cap 600): `stats` prints the cascade's "
          "GiVoxelStats; any other kind writes that level-0 array BY WORLD CELL to <data-root>/<file> - one JSON header "
          "line (cascade, kind, res, origin, cell, lo = the first voxel's WORLD cell index, dims) then "
          "dims[0] x dims[1] x dims[2] RGBA float32, x fastest - over the voxels whose centre lies in the world box "
          "(default the whole volume); a placement-free comparison joins two such files on the world cell" },
        { "regions", "regions [boxes]", 0, 1,
          "THE CHANGED-REGION LIST (PHOTON-7 stage 3; Scene::giChangedRegions) as the last frame closed it: its frame, "
          "its regions per kind (Slab, Card, Edit, Mover, MoverShadow, LampShadow, Lamp), its overflow bits (1 the sun, 2 "
          "the sky, 4 a change with no box), the restart flag, the merges at the 128-box cap and the sun's direction; "
          "`boxes` adds one LAB REGION line per region (kind, world lo, hi, cascade mask). No frame is drawn" },
        { "lights", "lights", 0, 0,
          "every light of the document: its name, type, WORLD position, intensity, range and source radius (one LAB LIGHT "
          "line each) - where a lamp is, for a pin or a camera (PHOTON-7 stage 1's Bistro read)" },
        { "arm", "arm <name> <value>", 2, 2,
          "a measuring arm (Engine::setArm, the engine's arm registry - e.g. gather.classOff 1); no frame is drawn" },
        { "row", "row <id> <value>", 2, 2,
          "a WORLD ROW set on the document through the World panel's own setter (irisgl worldmodes::row - e.g. "
          "reflectionRoughnessCutoff 100, in the row's own units); the mirror pushes it with the next frame; no frame is "
          "drawn" },
        { "stillness", "stillness <script|@file> <frames> [path] [age=N] [trail=N] [restarts] [dump=<name>|dumprgb=<name>] [freeze|freeze=world] [out=<name>]", 2, 9,
          "THE STILLNESS DOOR (PHOTON-2; irisgl Stillness.h): the move script (teleport:x,y,z,tx,ty,tz / "
          "walk:x,y,z,tx,ty,tz:N / hold:N, '/'-separated, or @file), the stop, then <frames> frames measured: the "
          "per-frame change in 8-bit codes on the pixels the gather saw before the stop (history count >= age, "
          "default 10), per material class (plaster >= 0.8, rough, glossy < 0.4, mirror, foliage = alpha-tested (cut-out), "
          "mover; by the "
          "environment's objects), the GI cache rows that fired, the gather's rest and the reflection count "
          "histogram; `path` adds the teleport window and the walk-vs-teleport comparison; `trail=N` measures the last N "
          "frames of the move too (every pixel, the ray levels re-fitted); `restarts` adds per frame the gather's foliage "
          "pixels and how many restarted their history (the class bit of its own history word); `dump=<name>` writes "
          "every trail frame's picture, the gather's view distance and surface test, its classes and camera to "
          "<data-root>/stillness/dump-<name>/ (M1's moving window, computed offline), `dumprgb=<name>` the pictures and "
          "cameras alone (a walk compared byte for byte); `freeze` switches the arm "
          "gi.freeze on at the script's first walk frame, only when the gather and the reflection HELD on the frame before "
          "(M1's frozen walk from the converged picture; the run fails otherwise - settle longer), restored at the end; "
          "`freeze=world` sets it to 2 instead - THE WORLD HELD: the caches and the gather frozen, the reflections live "
          "(rtreflect.walk_world: the reflection's own estimator alone). "
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
         "                      (a run on a model whose bake is not cached refuses, exit 5: never a bake under the card)\n"
         "  --arm <name>=<value>  an engine arm set before any scene is made (repeatable): a cache's first gather takes it\n"
         "  --camera x,y,z,tx,ty,tz   the camera at the start\n"
         "  --tier low|medium|high|epic   the World Mode at the start (default epic, a new scene's)\n"
         "  --size WxH          the window / offscreen size (default 1920x1080)\n"
         "  --help              this text\n\n"
         "keys (each runs the command line beside it, through the same parser):\n";
    for (const auto &k : keyTable()) o << "  " << k.first << std::string(k.first.size() < 8 ? 8 - k.first.size() : 1, ' ') << k.second << "\n";
    o << "  W/A/S/D move, Q/E down/up, Shift x4, right mouse button held = look\n\n"
         "commands (--script lines, stdin lines and the keys' lines):\n";
    for (const VerbDoc &v : verbs()) o << "  " << v.usage << "\n      " << v.what << "\n";
    o << "\na --script line the lab refuses STOPS the script there (exit 1, LAB SCRIPT STOPPED naming the line and its "
         "reason);\n  try <line>\n      opts that one line out: its refusal is said (LAB TRY) and the script goes on\n";
    return o.str();
}

}   // namespace lab
