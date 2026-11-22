#include "labscripts.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace lab {

static int gListings = 0;

int scriptListings() { return gListings; }

std::vector<std::string> scriptsIn(const std::string &folder)
{
    ++gListings;
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.size() > 3 && n.compare(n.size() - 3, 3, ".js") == 0 && it->is_regular_file(ec)) names.push_back(n);
    }
    std::sort(names.begin(), names.end());   // std::string's < : bytewise
    return names;
}

ScriptPick nextScript(const std::string &folder, const std::string &current)
{
    ScriptPick p;
    const std::vector<std::string> names = scriptsIn(folder);
    if (names.empty()) {
        p.message = "no scripts in " + folder;
        return p;
    }
    // the first name after `current` in the order (a name no longer there: the next one that would follow it)
    const auto after = std::upper_bound(names.begin(), names.end(), current);
    const size_t i = current.empty() || after == names.end() ? 0 : size_t(after - names.begin());
    p.ok = true;
    p.name = names[i];
    p.path = (std::filesystem::path(folder) / p.name).string();
    p.index = int(i) + 1;
    p.count = int(names.size());
    p.message = std::to_string(p.index) + " of " + std::to_string(p.count) + ": " + p.name;
    return p;
}

std::string scriptPlace(const std::string &folder, const std::string &name)
{
    const std::vector<std::string> names = scriptsIn(folder);
    const auto at = std::find(names.begin(), names.end(), name);
    if (at == names.end()) return name;
    return std::to_string(at - names.begin() + 1) + " of " + std::to_string(names.size()) + ": " + name;
}

}   // namespace lab
