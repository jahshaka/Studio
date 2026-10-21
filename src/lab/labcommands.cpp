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
        { "frames", "frames N", 1, 1, "render N frames (the engine's fixed 1/60 s grid; frames, never time)" },
        { "settle", "settle [maxFrames]", 0, 1,
          "render until the Atom status is steady: nothing pending and the cut's triangles unchanged over 8 frames (cap 1200)" },
        { "shot", "shot [name]", 0, 1, "a PNG of the frame to <data-root>/shots/<name>.png (headless: readPixels; window: the frame capture)" },
        { "status", "status [file]", 0, 1,
          "the engine's AtomDrawStatus + RenderStats + the lab's state as ONE JSON line, appended to <file> (stdout without one)" },
        { "save", "save [file.jlab]", 0, 1, "the session as a .jlab (file or env, camera, tier, view, gi, occlusion, sun)" },
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
        return true;
    }
    error = "'" + line + "': unknown command '" + out.verb + "' (help lists them)";
    return false;
}

std::string helpText()
{
    std::ostringstream o;
    o << "jahlab - the engine-only harness (Atom + Photon + the core engine, no Studio)\n\n"
         "usage: jahlab [flags] <scene.fbx|.obj|.gltf|.glb|.jlab>\n\n"
         "flags:\n"
         "  --data-root <dir>   bakes, shots, .jlab saves and the shader cache (default ~/.local/share/jahlab)\n"
         "  --headless          render OFFSCREEN (a Vulkan offscreen view, no window; Ogre-Next's Vulkan still opens an X\n"
         "                      connection at boot, so DISPLAY must name a server — the rig's Xvfb)\n"
         "  --script <file>     run the file's command lines after the load (then stdin, unless --no-stdin)\n"
         "  --no-stdin          do not read commands from stdin\n"
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
