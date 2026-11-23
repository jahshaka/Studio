// script.commands - THE FREEZE's PROOF (SCRIPT-IN-ENGINE-1 E0; the audit's finding 2), COMPILE-ONLY: every record a
// family of stage 2 will table (the spec's 2c table and 2d's records: the commands take or answer them) is walked, down
// through its nested records, by records.h's carried<T>() - each member a shape paramOf / toValue / fromValue carry -
// and asserted here. A record with a member records.h cannot carry fails THIS FILE's build by name, before any lane
// meets it after records.h freezes. No table is made here (each is its family's): the walk needs none.
#include "script/records.h"

#include "jahshaka/engine/Atom.h"
#include "jahshaka/engine/Camera.h"
#include "jahshaka/engine/Fog.h"
#include "jahshaka/engine/FrameInput.h"
#include "jahshaka/engine/GiCards.h"
#include "jahshaka/engine/GiParams.h"
#include "jahshaka/engine/GiStatus.h"
#include "jahshaka/engine/GiVoxels.h"
#include "jahshaka/engine/Lights.h"
#include "jahshaka/engine/Materials.h"
#include "jahshaka/engine/Mesh.h"
#include "jahshaka/engine/Monitor.h"
#include "jahshaka/engine/Particles.h"
#include "jahshaka/engine/Pictures.h"
#include "jahshaka/engine/Planar.h"
#include "jahshaka/engine/PostFx.h"
#include "jahshaka/engine/Published.h"
#include "jahshaka/engine/Shaders.h"
#include "jahshaka/engine/Sky.h"
#include "jahshaka/engine/Stats.h"

namespace jahshaka { namespace engine { namespace script {

#define JAH_CARRIED(T) static_assert(carried<T>(), #T ": a member records.h cannot carry (records.h carried<T>)")
// P - scene, light, mesh, the GI readers
JAH_CARRIED(SkyDesc);
JAH_CARRIED(EnvironmentDesc);
JAH_CARRIED(FogDesc);
JAH_CARRIED(ShadowDesc);
JAH_CARRIED(MeshData);
JAH_CARRIED(LightDesc);
JAH_CARRIED(CardSample);
JAH_CARRIED(CardRadiance);
JAH_CARRIED(GiVoxelStats);
JAH_CARRIED(GiVoxelVolume);
JAH_CARRIED(GiChangedRegions);
// Q - material, effects, gi, engine, atom
JAH_CARRIED(PbrParams);
JAH_CARRIED(DecalDesc);
JAH_CARRIED(ParticleSystemDesc);
JAH_CARRIED(ParticleEmitterDesc);
JAH_CARRIED(ParticleAffectorDesc);
JAH_CARRIED(BillboardInstance);
JAH_CARRIED(PlanarReflectionParams);
JAH_CARRIED(GiParams);
JAH_CARRIED(GiPlacementPin);
JAH_CARRIED(SunContactDesc);
JAH_CARRIED(TextureMemoryEntry);
JAH_CARRIED(ValidationStatus);
// R - view, picture, and 2d's published records and the monitor
JAH_CARRIED(CameraDesc);
JAH_CARRIED(PostFxDesc);
JAH_CARRIED(ImageF);
JAH_CARRIED(PublishedFrame);
JAH_CARRIED(ViewFrames);
JAH_CARRIED(PublishedEngineFrame);
JAH_CARRIED(GiStatus);
JAH_CARRIED(MonitorRecord);
JAH_CARRIED(MonitorEvent);
JAH_CARRIED(FrameSubmitStatus);
JAH_CARRIED(ShaderCacheStats);
JAH_CARRIED(AtomDrawStatus);
#undef JAH_CARRIED

}}}  // namespace jahshaka::engine::script
