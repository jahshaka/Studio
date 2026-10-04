// perf-ab fixture: reflect_mover — gi.reflect_mover's cost scene, in the app.
// A glossy floor and a glossy sphere moving over it, a sun, the camera low over the
// floor so the reflection fills the frame. The suite's own arm (test_reflect_mover.cpp
// costMain: 1920x1080 offscreen, the sphere on a path, "reflect.motion" on/off) is the
// engine-level twin; this is the same question asked through the app's frame.
//
// A fixture is ordinary script: it builds the scene once, after the project exists.
// It may define perfAbStep(i), called before EVERY frame the harness draws (warm-up,
// settle and capture alike), to animate the scene.
var floor = scene.addPrimitive("plane", { scale: { x: 40, y: 1, z: 40 } });
material.set(floor, { baseColor: "#8c8c8c", roughness: 0.2, metallic: 0.0 });
var sphere = scene.addPrimitive("sphere", { position: { x: 0, y: 0.6, z: 0 } });
material.set(sphere, { baseColor: "#d0a060", roughness: 0.1, metallic: 1.0 });
scene.addLight("directional", { position: { x: 0, y: 8, z: 0 } });
editor.setCamera({ position: { x: 0, y: 1.2, z: 6 }, lookAt: { x: 0, y: 0.2, z: 0 } });

function perfAbStep(i) {
    var a = 0.05 * i;
    node.setProperty(sphere, "position", { x: 1.6 * Math.sin(a), y: 0.6, z: 1.6 * Math.cos(a) - 1.0 });
}
