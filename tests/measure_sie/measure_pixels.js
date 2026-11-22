// MEASURING ARM (SCRIPT-IN-ENGINE-1, the lead's last item (c)): A PER-PIXEL LAW OVER A 1920 x 1080 PICTURE - the mean of
// every channel, once as the NATIVE reduction (engine.picture.mean) and once as a JS loop over engine.picture.bytes;
// R repeats each, alternated (ABBA), the times by performance.now. Never registered.
const W = 1920, H = 1080, R = 8;
const view = engine.view.createOffscreen({name: "pixels", width: W, height: H, background: [0.2, 0.3, 0.4]});
const scene = engine.scene.create("pixels");
engine.view.setScene(view, {scene});
engine.view.setPostFx(view, {allowOffscreen: true});
engine.frames(2);
const shot = engine.view.readPixels({view});
const nativeMs = [], jsMs = [], copyMs = [];
let a = null, b = null;
const native = () => {
    const t0 = performance.now();
    a = engine.picture.mean(shot.picture, {x0: 0, y0: 0, x1: W, y1: H});
    nativeMs.push(performance.now() - t0);
};
const loop = () => {
    const t0 = performance.now();
    const px = engine.picture.bytes(shot.picture);
    const t1 = performance.now();
    const s = [0, 0, 0, 0];
    for (let i = 0; i < px.length; i += 4) { s[0] += px[i]; s[1] += px[i + 1]; s[2] += px[i + 2]; s[3] += px[i + 3]; }
    const n = W * H;
    b = [s[0] / n, s[1] / n, s[2] / n, s[3] / n];
    copyMs.push(t1 - t0);
    jsMs.push(performance.now() - t0);
};
for (let r = 0; r < R; ++r) { if (r % 2) { loop(); native(); } else { native(); loop(); } }   // ABBA
const med = (x) => { const y = x.slice().sort((p, q) => p - q); return (y[(y.length - 1) >> 1] + y[y.length >> 1]) / 2; };
const span = (x) => Math.min(...x).toFixed(2) + ".." + Math.max(...x).toFixed(2);
console.log("MEASURE pixels native_ms=" + med(nativeMs).toFixed(2) + " [" + span(nativeMs) + "] js_loop_ms=" +
            med(jsMs).toFixed(2) + " [" + span(jsMs) + "] of_which_bytes_copy_ms=" + med(copyMs).toFixed(2) + " [" + span(copyMs) + "]");
host.check({ok: a.every((v, i) => Math.abs(v - b[i]) < 1e-9), what: "the two means agree: " + a.map((v) => v.toFixed(3))});
