#pragma once
// A LOCKSTEP FRAME IS ONE GRID STEP (TEST-STREAMLINE-1 red (b); CODING_RULES §2: the engine has no wall clock).
//
// A frame a script, the MCP or the host steps (editor.frame, player.frame, renderFrames(n), an open's boundary frame)
// hands the document's SimulationClock a STATED number of seconds - one grid step unless the caller states another
// dt >= 0 - and never the wall time the frame took. It used to be the wall: `editor.frame(37)` during play bought 21-22
// grid steps on the headless host and 57-66 as an app row (a played lamp's animation read t 0.350 / 0.367 / 0.950 /
// 1.100 after the same 37 frames), so a scripted picture was a function of the box's pace. Only the render driver's
// free-running tick takes the wall (the owner's real-time session, EngineSceneViewport::syncFrame(-1)), and the
// timeline panel's preview play turns its wall time into whole steps through a SimulationClock of its own.
// Header-only, no Qt. Its law at the door: pool.host_epic::frame_step (tests/scripting/scripts/e2e_frame_step.js).

#include "irisgl/document/scenegraph/simulationclock.h"

namespace framestep {

/// The seconds a lockstep frame hands the clock: `dt` when stated (>= 0), ONE grid step otherwise - never the wall.
inline double lockstepSeconds(double dt) { return dt >= 0.0 ? dt : iris::SimulationClock::kStepSeconds; }

/// A scripted dt the verbs accept: not negative (no "use the wall clock" value) and within the clock's per-frame bound.
inline bool scriptedDtValid(double dt) { return dt >= 0.0 && dt <= iris::SimulationClock::kMaxAdvanceSeconds + 1e-9; }

}   // namespace framestep
