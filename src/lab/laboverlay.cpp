#include "laboverlay.h"

#include <cstdio>

namespace lab {

std::string grouped(unsigned long long n)
{
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

std::vector<std::string> overlayRows(const OverlayInput &in)
{
    std::vector<std::string> rows;
    char b[256];
    std::snprintf(b, sizeof b, "%.1f ms/frame   %.0f fps   frame %llu", in.frameMs, in.fps, in.frame);
    rows.push_back(b);
    rows.push_back(grouped(in.sceneTriangles) + " scene tris   " +
                   (in.counted ? grouped(in.submittedTriangles) : std::string("-")) + " submitted   " +
                   (in.counted ? grouped(in.draws) : std::string("-")) + " draws");
    if (in.atomOn) {
        rows.push_back("atom: " + (in.cutValid ? grouped(in.cutTriangles) : std::string("-")) + " cut tris   " +
                       (in.cutValid ? grouped(in.cutClusters) : std::string("-")) + " clusters   " +
                       grouped(in.atomItems) + " atom / " + grouped(in.pbsItems) + " pbs objects" +
                       (in.pending ? "   " + grouped(in.pending) + " pending" : std::string()));
        rows.push_back("hidden: " + (in.cutValid ? grouped(in.occluded) : std::string("-")) + " objects occluded   " +
                       (in.cutValid ? grouped(in.disoccluded) : std::string("-")) + " disoccluded");
    } else {
        rows.push_back("atom: off (every object on stock PBS)");
    }
    rows.push_back("tier " + in.tier + "   view " + in.view + "   gi " + (in.gi ? "on" : "off") + "   occlusion " +
                   (in.occlusion ? "on" : "off") + "   draw " + (in.draw ? "on" : "off"));
    std::snprintf(b, sizeof b, "camera %.2f,%.2f,%.2f -> %.2f,%.2f,%.2f", in.cam[0], in.cam[1], in.cam[2], in.target[0],
                  in.target[1], in.target[2]);
    rows.push_back(b);
    return rows;
}

}   // namespace lab
