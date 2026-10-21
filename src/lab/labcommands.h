// jahlab — THE COMMAND SURFACE (LAB-1 stage 1; API-first: the script grammar IS the lab's API).
//
// ONE parser serves three sources: the keys (each key is a fixed command LINE, keyLine below, handed to the same
// parser), `--script <file>` (one line per command, run after the load) and stdin (the same lines, for an agent
// driving a live lab). Nothing else changes the lab's state: a key never calls a setter of its own. `--help` and F1
// print the table this file holds, so the help cannot drift from the grammar.
#pragma once

#include <string>
#include <vector>

namespace lab {

/// One parsed command: the verb and its words, plus the line it came from.
struct Command
{
    std::string verb;
    std::vector<std::string> args;
    std::string line;
    std::string source;   ///< "key", "script", "stdin" or "flag" — said in the log, never consulted
};

/// One verb of the grammar: its name, its argument shape (words after the verb) and what it does.
struct VerbDoc
{
    const char *verb = nullptr;
    const char *usage = nullptr;   ///< the line's shape, e.g. "view triangles|levels|density|objects|buckets|off|next"
    int minArgs = 0, maxArgs = 0;
    const char *what = nullptr;
};
const std::vector<VerbDoc> &verbs();

/// The key table: a key (X keysym name) -> the command line it runs. Empty = not a command key.
std::string keyLine(const std::string &key);
/// Every command key and its line, in table order (F1's help and --help print it).
const std::vector<std::pair<std::string, std::string>> &keyTable();

/// Parses one line. Blank lines and `#` comments answer false with an EMPTY error (nothing to run); an unknown verb
/// or a wrong argument count answers false with the reason.
bool parse(const std::string &line, Command &out, std::string &error);

/// The whole help text: the keys, the verbs and the flags.
std::string helpText();

/// The Atom view names in the engine's AtomView order (Types.h: Off, Triangles, Levels, Buckets, Objects, Density).
const std::vector<std::string> &atomViewNames();
/// The tier names in worldmodes::Mode order (Low, Medium, High, Epic).
const std::vector<std::string> &tierNames();

}   // namespace lab
