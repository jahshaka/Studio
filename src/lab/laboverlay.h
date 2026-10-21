// jahlab — THE F3 ROWS (LAB-1 stage 1): a pure function of the engine's readouts (RenderStats + the scene's
// AtomDrawStatus) and the lab's state, drawn through the engine's own overlay (View::setOverlay(ViewOverlayDesc
// {lines})). The shape of the app's rows (src/viewport/statsrows.h compose — copied as a function, not linked):
// same numbers in, same strings out.
#pragma once

#include <string>
#include <vector>

namespace lab {

struct OverlayInput
{
    double frameMs = 0.0, fps = 0.0;              ///< RenderStats' rolling average
    bool counted = false;                         ///< RenderStats::metricsRecording (false = the counts read "-")
    unsigned long long sceneTriangles = 0;        ///< the document's level-0 triangles (the authored content)
    unsigned long long submittedTriangles = 0, draws = 0;
    bool atomOn = false, cutValid = false;
    unsigned atomItems = 0, pbsItems = 0, pending = 0;
    unsigned long long cutTriangles = 0;
    unsigned cutClusters = 0, occluded = 0, disoccluded = 0;
    std::string tier, view;
    bool gi = true, occlusion = true, draw = true;
    float cam[3] = {}, target[3] = {};
    unsigned long long frame = 0;
};

/// The rows, top to bottom.
std::vector<std::string> overlayRows(const OverlayInput &in);
/// A count with thousands separators ("1,234,567").
std::string grouped(unsigned long long n);

}   // namespace lab
