#ifndef PREVIEWENVIRONMENT_H
#define PREVIEWENVIRONMENT_H

// THE STUDIO ENVIRONMENT every preview surface is lit by (MATPREVIEW-ENV-1,
// owner review R9).
//
// WHAT IT REPLACES. The material preview dock and the thumbnail renderer each
// carried a HAND RIG: two point-like lights over a flat colour sky, at two
// different intensities, over two different greys, graded at two different
// exposures. A flat sky is a uniform sphere of light, so a metal sphere
// mirrored a blank grey ball with two specular dots burnt into it — the owner's
// "burnt-out lights and weird mirrored reflections" — and a material's
// thumbnail could never match its own preview because nothing about the two
// rigs was the same.
//
// WHAT THIS IS. ONE environment, GENERATED (no shipped file, no licence, no
// HDRI to download): a neutral studio — a soft vertical gradient with a bright
// horizon band and a dim floor half — carrying three soft-edged rectangular
// SOFTBOXES placed high-left, high-right and overhead-behind, written into the
// preview document as an equirect sky. The sky IS the lighting: its
// cosine-convolved integral is the diffuse term (pushed through
// Scene::setAmbientSh, the same nine bands a real sky pushes) and the engine's
// own capture of it is the specular one, so a chrome sphere reflects softboxes
// with shape and a matte one reads with real direction. There are NO light
// nodes left in either preview document.
//
// WHY 8-BIT AND NOT A FLOAT HDRI. The sky an iris document can carry reaches
// the engine through SceneMirror::applySky, and every upload path there —
// loadTexture, createTexture, the live-texture path this uses — is 8-bit RGBA.
// A float environment would need a new engine entry point (an out-of-lane
// change), so the softboxes sit at the top of the LDR range (radiance 1.0) and
// the EXPOSURE is what keeps them off the clip: with the grey card placed at
// 118/255 by the derivation below, a mirror of a 1.0 softbox lands in the 230s,
// which is a highlight with a shoulder on it instead of a flat white hole.
//
// DETERMINISTIC, all of it: the image is float maths over texel centres with no
// clock and no randomness, the ambient is integrated from the quantised texels
// (what the GPU actually samples), and the exposure is derived — so a thumbnail
// of a material is byte-identical from run to run, which is what the library's
// tiles are compared on.

#include <QImage>
#include <QVariantMap>
#include <vector>

#include "irisgl/irisglfwd.h"

namespace jahshaka { namespace engine { class Scene; } }

namespace previewenv {

/// A softbox, as the picture is authored: the world direction its centre hangs
/// in, its half-extents in DEGREES about that direction, and its radiance.
struct Softbox
{
    const char *name;
    float dir[3];
    float halfWidthDeg;
    float halfHeightDeg;
    float radiance;
};

/// The three softboxes, in the order they are painted.
const std::vector<Softbox> &softboxes();

/// The environment as an equirect image (sRGB RGBA8888, `size()` texels), built
/// once on first use. The lat-long convention is the renderer's own
/// (SceneMirror: u = (atan2(x,-z)+PI)/2PI, row 0 = the zenith).
const QImage &image();
QSize size();

/// Binds the environment to a PREVIEW document: the sky, the exposure a preview
/// grades at, and the three things a preview never has (a sun disc, fog,
/// shadows). Adds no light node — the sky is the light.
void apply(const iris::ScenePtr &scene);

/// THE ENVIRONMENT'S OWN VIEWPOINT: the unit direction from the subject towards
/// the camera the studio is authored for — up and to the right, with the lamps
/// behind that shoulder and the dark wall opposite. A surface that photographs
/// a material (the thumbnail renderer's sphere) looks from HERE, so its picture
/// and the dock's are the same picture; the exposure is derived for a grey card
/// facing this way.
void viewDirection(float out[3]);

/// The environment's cosine-convolved ambient, in the basis and the units
/// Scene::setAmbientSh documents: 9 bands x 3 channels, mean incident radiance.
/// Integrated once from the image's own texels.
const float *ambientSh();

/// Pushes both halves of the environment light into an engine Scene: the
/// diffuse nine bands and the specular gain. The sky itself travels the
/// ordinary way (SceneMirror::applySky).
void pushAmbient(jahshaka::engine::Scene *scene);

/// THE PREVIEW'S EXPOSURE, in STOPS (iris::Scene::exposure's unit), DERIVED:
/// the environment's own mean incident radiance is its key irradiance, and
/// `iris::lens::exposureForKeyIrradiance` is the same meter every world in this
/// app is exposed by. An 18% grey diffuse sphere therefore lands on the grey
/// card — 118/255 — by construction rather than by taste.
float exposureEv();

/// The chain exposure `E` the same value corresponds to (PostFxDesc::exposure).
float exposureChain();

/// What `materials.previewEnvironment()` answers: the constants, as data.
QVariantMap describe();

}   // namespace previewenv

#endif   // PREVIEWENVIRONMENT_H
