// jahlab — THE SCRIPTS FOLDER (LAB-SCRIPTS-1 stage 2; the lead's ruling on Load): what `js next` (F10, the strip's Load)
// walks. ONE folder - the loaded script's, else --js-dir's, else <data root>/scripts (made at boot: the owner drops his
// files there) - its EVERY `.js` file in BYTEWISE name order, RE-READ from disk at every press (a file just saved is in
// the list, a deleted one is gone: never a stale list), wrapping after the last. An empty folder answers its own line,
// never a silent no-op. No Qt, no engine: lab.strip_hits' CPU row compiles it alone.
#pragma once

#include <string>
#include <vector>

namespace lab {

/// The `.js` files of `folder` (regular files, by name), in bytewise order; empty when there are none or no folder.
std::vector<std::string> scriptsIn(const std::string &folder);

/// One press of Load: the script after `current` (a name in `folder`; empty or absent = the first), the list read now.
struct ScriptPick
{
    bool ok = false;
    std::string path;      ///< folder/name of the pick
    std::string name;
    int index = 0;         ///< 1-based, of `count`
    int count = 0;
    std::string message;   ///< ok: "<i> of <n>: <name>"; else "no scripts in <folder>"
};
ScriptPick nextScript(const std::string &folder, const std::string &current);

/// "<i> of <n>: <name>" for `name` in the list as it is now ("<name>" alone when it is not in `folder`).
std::string scriptPlace(const std::string &folder, const std::string &name);

}   // namespace lab
