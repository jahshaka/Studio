// Jahshaka web viewer — WebGPU only (owner decision, WEB_EXPORT_AUDIT §4).
// Runs as a classic inline script after the vendored three.js WebGPU IIFE
// bundle (global THREE). Scene arrives either embedded (window.JAH.glbBase64)
// or fetched from scene.glb (viewer.html, served).
/* global THREE */
(function () {
    "use strict";

    var cfg = window.JAH || {};

    function panel(html) {
        var el = document.getElementById("jah-panel");
        el.innerHTML = html;
        el.style.display = "flex";
        document.getElementById("jah-loading").style.display = "none";
    }

    function needsWebGpuPanel() {
        panel(
            '<div class="jah-card">' +
            "<h1>This browser can’t show 3D yet</h1>" +
            "<p>This scene renders with <b>WebGPU</b>, which this browser doesn’t provide.</p>" +
            "<p>Browsers that work:</p>" +
            "<ul>" +
            "<li>Chrome / Edge 113+ (Windows, macOS, ChromeOS; Linux with Chrome 139+)</li>" +
            "<li>Firefox 141+ (Windows; macOS/Linux rolling out)</li>" +
            "<li>Safari 26+ (macOS 26, iOS 26)</li>" +
            "</ul></div>");
    }

    function fatal(message) {
        panel('<div class="jah-card"><h1>Could not load the scene</h1><p>' + message + "</p></div>");
    }

    function decodeBase64(b64) {
        var binary = atob(b64);
        var bytes = new Uint8Array(binary.length);
        for (var i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
        return bytes.buffer;
    }

    function getGlb() {
        if (cfg.glbBase64) return Promise.resolve(decodeBase64(cfg.glbBase64));
        return fetch("scene.glb").then(function (r) {
            if (!r.ok) throw new Error("scene.glb: HTTP " + r.status);
            return r.arrayBuffer();
        }).catch(function (e) {
            throw new Error(
                "Could not fetch scene.glb — browsers block file:// fetches. " +
                "Serve this folder (python3 -m http.server) and open viewer.html over http. (" + e.message + ")");
        });
    }

    if (!navigator.gpu) { needsWebGpuPanel(); return; }
    // WebGPU ONLY (owner decision, audit §4): three's WebGPURenderer silently
    // falls back to WebGL2 when no adapter exists — gate explicitly instead.
    var adapterProbe = navigator.gpu.requestAdapter().then(function (adapter) {
        if (!adapter) throw new Error("no-webgpu-adapter");
        return adapter;
    });

    var renderer, scene, camera, controls, mixer, clock;
    var documentCameras = [];

    function applyShadow(light, jah) {
        if (!jah || !jah.shadow || !jah.shadow.castShadow) return;
        light.castShadow = true;
        var size = jah.shadow.mapSize || 1024;
        light.shadow.mapSize.set(size, size);
        light.shadow.bias = -(jah.shadow.bias || 0.0015);
        if (light.isDirectionalLight) {
            light.shadow.camera.left = light.shadow.camera.bottom = -20;
            light.shadow.camera.right = light.shadow.camera.top = 20;
            light.shadow.camera.far = 200;
        }
    }

    function strongestFilter(filters) {
        if (filters.indexOf("verysoft") >= 0) return THREE.PCFSoftShadowMap;
        if (filters.indexOf("soft") >= 0) return THREE.PCFShadowMap;
        return THREE.BasicShadowMap;
    }

    function applySky(jah) {
        var sky = jah && jah.sky ? jah.sky : { type: "color", color: "#3498db" };
        if (sky.type === "equirect" && sky.image) {
            new THREE.TextureLoader().load(sky.image, function (tex) {
                tex.mapping = THREE.EquirectangularReflectionMapping;
                tex.colorSpace = THREE.SRGBColorSpace;
                scene.background = tex;
                scene.environment = tex;   // IBL (audit §1 "IBL from sky")
            });
        } else if (sky.type === "realistic") {
            // Same zz85 Preetham model, same parameter names (audit §1).
            var skyMesh = new THREE.SkyMesh();
            skyMesh.scale.setScalar(450000);
            skyMesh.turbidity.value = sky.turbidity !== undefined ? sky.turbidity : 2;
            skyMesh.rayleigh.value = sky.rayleigh !== undefined ? sky.rayleigh : 1;
            skyMesh.mieCoefficient.value = sky.mieCoefficient !== undefined ? sky.mieCoefficient : 0.005;
            skyMesh.mieDirectionalG.value = sky.mieDirectionalG !== undefined ? sky.mieDirectionalG : 0.8;
            if (sky.sunPosition) skyMesh.sunPosition.value.set(sky.sunPosition[0], sky.sunPosition[1], sky.sunPosition[2]);
            scene.add(skyMesh);
        } else {
            scene.background = new THREE.Color(sky.color || "#3498db");
        }
    }

    function buildFromUserData(root, sceneUserData) {
        var jah = sceneUserData && sceneUserData.jah ? sceneUserData.jah : {};
        applySky(jah);
        if (jah.fog)
            scene.fog = new THREE.Fog(new THREE.Color(jah.fog.color || "#ffffff"),
                                      jah.fog.start || 1, jah.fog.end || 100);

        var filters = [];
        var ltcInstalled = false;
        root.traverse(function (obj) {
            var u = obj.userData && obj.userData.jah ? obj.userData.jah : null;
            if (!u) return;
            if (u.visible === false) obj.visible = false;
            if (u.shadow) {
                filters.push(u.shadow.filter);
                // the light is this shim node's child (GLTFLoader attached it)
                for (var i = 0; i < obj.children.length; i++)
                    if (obj.children[i].isLight) applyShadow(obj.children[i], u);
                if (obj.isLight) applyShadow(obj, u);
            }
            if (u.areaLight) {
                if (!ltcInstalled && THREE.RectAreaLightNode && THREE.RectAreaLightTexturesLib) {
                    THREE.RectAreaLightNode.setLTC(THREE.RectAreaLightTexturesLib.init());
                    ltcInstalled = true;
                }
                var area = new THREE.RectAreaLight(
                    new THREE.Color(u.areaLight.color || "#ffffff"),
                    u.areaLight.intensity || 1,
                    u.areaLight.width || 1, u.areaLight.height || 1);
                obj.add(area);   // shim node already points -Z where the doc's -Y was
            }
        });
        if (filters.length) {
            renderer.shadowMap.enabled = jah.shadowEnabled !== false;
            renderer.shadowMap.type = strongestFilter(filters);
            scene.traverse(function (o) {
                if (o.isMesh) { o.castShadow = true; o.receiveShadow = true; }
            });
        }

        // per-material IBL intensity
        scene.traverse(function (o) {
            if (o.isMesh && o.material && o.material.userData && o.material.userData.jah) {
                var mj = o.material.userData.jah;
                if (mj.iblIntensity !== undefined) o.material.envMapIntensity = mj.iblIntensity;
                if (mj.useIbl === false) o.material.envMapIntensity = 0;
            }
        });
    }

    function frameScene() {
        // Frame the CONTENT, not the ground plane: a huge floor mesh (the
        // default scene ships a +/-512 one) would push the camera out past the
        // scene's fog distance. Exclude meshes an order of magnitude larger
        // than the median mesh when there are smaller ones to frame instead.
        var boxes = [];
        scene.traverse(function (o) {
            if (!o.isMesh) return;
            var b = new THREE.Box3().expandByObject(o);
            if (!b.isEmpty()) boxes.push(b);
        });
        var dims = boxes.map(function (b) {
            var s = b.getSize(new THREE.Vector3());
            return Math.max(s.x, s.y, s.z);
        }).sort(function (a, b) { return a - b; });
        var median = dims.length ? dims[Math.floor(dims.length / 2)] : 0;
        var box = new THREE.Box3();
        for (var i = 0; i < boxes.length; i++) {
            var s = boxes[i].getSize(new THREE.Vector3());
            var d = Math.max(s.x, s.y, s.z);
            if (boxes.length > 1 && median > 0 && d > 10 * median) continue;
            box.union(boxes[i]);
        }
        if (box.isEmpty() && boxes.length) for (var j = 0; j < boxes.length; j++) box.union(boxes[j]);
        if (box.isEmpty()) box.setFromCenterAndSize(new THREE.Vector3(), new THREE.Vector3(4, 4, 4));
        var center = box.getCenter(new THREE.Vector3());
        var size = box.getSize(new THREE.Vector3());
        var radius = Math.max(size.x, size.y, size.z, 0.5);
        // adapt clip planes to scene radius — the farClip=500 lesson
        camera.near = Math.max(radius / 1000, 0.01);
        camera.far = Math.max(radius * 50, 500);
        camera.updateProjectionMatrix();
        camera.position.set(center.x + radius * 0.9, center.y + radius * 0.7, center.z + radius * 0.9);
        controls.target.copy(center);
        controls.update();
    }

    function start(arrayBuffer) {
        renderer = new THREE.WebGPURenderer({ antialias: true });
        renderer.toneMapping = THREE.NeutralToneMapping;
        renderer.setPixelRatio(window.devicePixelRatio);
        renderer.setSize(window.innerWidth, window.innerHeight);
        document.body.appendChild(renderer.domElement);

        renderer.init().then(function () {
            scene = new THREE.Scene();
            camera = new THREE.PerspectiveCamera(55, window.innerWidth / window.innerHeight, 0.1, 2000);
            controls = new THREE.OrbitControls(camera, renderer.domElement);
            controls.enableDamping = true;
            clock = new THREE.Clock();

            var loader = new THREE.GLTFLoader();
            loader.parse(arrayBuffer, "", function (gltf) {
                scene.add(gltf.scene);
                documentCameras = gltf.cameras || [];
                buildFromUserData(gltf.scene, gltf.scene.userData);
                frameScene();

                if (gltf.animations && gltf.animations.length) {
                    mixer = new THREE.AnimationMixer(gltf.scene);
                    for (var i = 0; i < gltf.animations.length; i++)
                        mixer.clipAction(gltf.animations[i]).play();
                }

                // minimal machine-readable state for tooling and smoke tests
                var stats = { meshes: 0, lights: 0, vertices: 0 };
                scene.traverse(function (o) {
                    if (o.isMesh) {
                        stats.meshes++;
                        var pos = o.geometry && o.geometry.getAttribute("position");
                        if (pos) stats.vertices += pos.count;
                    }
                    if (o.isLight) stats.lights++;
                });
                var bb = new THREE.Box3();
                scene.traverse(function (o) { if (o.isMesh) bb.expandByObject(o); });
                window.JAH_STATE = {
                    meshes: stats.meshes, lights: stats.lights, vertices: stats.vertices,
                    cameras: documentCameras.length,
                    animations: gltf.animations ? gltf.animations.length : 0,
                    bboxMin: bb.min.toArray(), bboxMax: bb.max.toArray(),
                    camera: camera.position.toArray()
                };

                document.getElementById("jah-loading").style.display = "none";
                renderer.setAnimationLoop(function () {
                    if (mixer) mixer.update(clock.getDelta());
                    controls.update();
                    renderer.render(scene, camera);
                });
            }, function (e) {
                fatal("glTF parse error: " + (e && e.message ? e.message : e));
            });
        }).catch(function () {
            // adapter exists but device init failed (blocklist, drivers)
            needsWebGpuPanel();
        });

        window.addEventListener("resize", function () {
            camera.aspect = window.innerWidth / window.innerHeight;
            camera.updateProjectionMatrix();
            renderer.setSize(window.innerWidth, window.innerHeight);
        });
    }

    if (cfg.notInlined) {
        fatal("This scene was too large to embed. Serve this folder " +
              "(<code>python3 -m http.server</code>) and open <b>viewer.html</b> — see README.txt.");
        return;
    }

    adapterProbe.then(function () {
        return getGlb().then(start);
    }).catch(function (e) {
        if (e && e.message === "no-webgpu-adapter") needsWebGpuPanel();
        else fatal(e.message || String(e));
    });
})();
