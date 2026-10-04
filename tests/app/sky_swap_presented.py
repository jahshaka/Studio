#!/usr/bin/env python3
"""app.sky_swap_presented — NO PRESENTED FRAME LOSES THE SKY ENVIRONMENT (SKY-SWAP-1).

THE DEFECT IT GUARDS (the owner's smoke, 2026-10-03: "a noticeable flickering in
'were' when I move the camera up and down"). Every sky re-capture presented ONE
frame whose sky environment was black: the swap destroyed the previous
reflection cube in the change frame while the Atom route's decode twins (JSON
clones of the PBS datablocks, re-keyed by the NEXT frame's drain) still sampled
it, and a destroyed TextureGpu samples Ogre's blank texture. A gold sphere read
102 -> 0.4 grey codes for that one frame at Medium, the floor -6 at Epic.
`scripting.e2e.sky_swap_transient` guards the same transient through
editor.screenshot / camera.screenshot — offscreen renders of their own, which
never see the frame the VIEWPORT presented. This suite reads THAT frame: the
X server's copy of the window (xwd of the root, cropped to the viewport —
xwd of the Ogre child window itself is black, GATE_AND_RIG), taken while the
UI thread is frozen right after the frame was presented.

HOW A FRAME IS HELD. The MCP script policy is 'off' (the app's render loop does
not tick while a run is in flight), and every read is one run of
  <change>; editor.frame(1); log.write(<marker>); app.blockUiThread(ms)
so the last presented picture IS that frame for the whole freeze. The driver
waits for the marker in the app's own output and captures. FRAMES_AFTER
consecutive frames are read after each change, because a capture the mirror's
sync asks for while drawing the change frame lands one frame later.

THE TRIGGERS (the brief's list): the sun disc into the probes and back, the
cloud layer's altitude, the cloud layer off and on, and an observer-band
crossing (a vertical camera move past 2^0.25 of the observer's band — the
'were' repro; world.clouds().live.changeCaptures proves each crossing captured),
each at Medium and at Epic with rays off.

THE ASSERTION, per arm and per sample patch (a metal sphere — the reflection
cube's own reader — and the floor), in mean grey codes:
  * a sky arm (the view still): no frame read after the change falls more than
    DIP_LIMIT below the lower of the frames before and 60 frames after it — a
    change may move the picture; a dip below both ends is the flash;
  * a camera arm (the view moved, so "before" is another picture and Epic's
    temporal passes settle over frames as a ramp): no frame after the change
    frame falls more than DIP_LIMIT below BOTH its neighbours (the capture a
    move asks for lands one frame after it; the change frame has no judge).
An arm whose marker never arrived fails.

Exits 77 (skip) where xwd, xdpyinfo or Xvfb is missing.

usage: sky_swap_presented.py <jahshaka-binary> <outdir>
"""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import urllib.request

TAG = "sky_swap_presented"
DIP_LIMIT = 1.0          # grey codes beyond the change's own steady-state difference
FRAMES_AFTER = 3         # presented frames read after each change
FREEZE_MS = 1000         # the UI-thread freeze that holds a frame on screen (the grab
                         # fires within ~20 ms of the marker)


def say(msg):
    print("%s: %s" % (TAG, msg), flush=True)


def read_xwd(data):
    """A ZPixmap XWD of a 24-bit Xvfb root -> a dict the patch reader walks."""
    hdr = struct.unpack(">25I", data[:100])
    header_size, fmt, depth, w, h = hdr[0], hdr[2], hdr[3], hdr[4], hdr[5]
    byte_order, bits_per_pixel, bytes_per_line = hdr[7], hdr[11], hdr[12]
    red_mask, green_mask, blue_mask = hdr[14], hdr[15], hdr[16]
    ncolors = hdr[19]
    if fmt != 2 or bits_per_pixel not in (24, 32):
        raise RuntimeError("unexpected xwd format %d / %d bpp" % (fmt, bits_per_pixel))
    off = header_size + ncolors * 12
    return dict(w=w, h=h, data=data[off:], bpl=bytes_per_line, bpp=bits_per_pixel // 8,
                lsb=(byte_order == 0), masks=(red_mask, green_mask, blue_mask), depth=depth)


def patch_mean(img, x0, y0, w, h):
    """Mean Rec.601 grey of a rectangle (every other column), in 0..255 codes."""
    d, bpl, bpp = img["data"], img["bpl"], img["bpp"]
    rm, gm, bm = img["masks"]
    shifts = [(m & -m).bit_length() - 1 for m in (rm, gm, bm)]
    order = "little" if img["lsb"] else "big"
    total = 0.0
    n = 0
    for y in range(y0, y0 + h):
        row = y * bpl
        for x in range(x0, x0 + w, 2):
            p = row + x * bpp
            px = int.from_bytes(d[p:p + bpp], order)
            total += (0.299 * ((px & rm) >> shifts[0]) + 0.587 * ((px & gm) >> shifts[1]) +
                      0.114 * ((px & bm) >> shifts[2]))
            n += 1
    return total / max(1, n)


class App:
    def __init__(self, binary, outdir, display):
        self.log_path = os.path.join(outdir, "app.log")
        self.log = open(self.log_path, "wb")
        env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb")
        # cwd = the binary's directory: media resolves from there (CLAUDE.md)
        self.proc = subprocess.Popen([binary, "--mcp-port=0"], stdout=self.log, stderr=subprocess.STDOUT,
                                     env=env, cwd=os.path.dirname(binary))
        self.token = self.port = None
        for _ in range(1800):   # a cold shader cache (a fresh data root) boots in minutes
            if self.proc.poll() is not None:
                break
            text = self.text()
            t = re.search(r"^MCP: token (\S+)", text, re.M)
            p = re.search(r"^MCP: port (\d+)", text, re.M)
            if t and p:
                self.token, self.port = t.group(1), p.group(1)
                break
            time.sleep(0.5)
        if not self.token:
            # never leave a live Vulkan client behind for the Xvfb kill that follows
            self.close()
            raise RuntimeError("the app never published an MCP token and port")
        self.post({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                   "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                              "clientInfo": {"name": TAG, "version": "1"}}})

    def text(self):
        with open(self.log_path, "rb") as f:
            return f.read().decode("utf-8", "replace")

    def post(self, body):
        req = urllib.request.Request("http://127.0.0.1:%s/mcp" % self.port, json.dumps(body).encode(),
                                     {"Content-Type": "application/json",
                                      "Accept": "application/json, text/event-stream",
                                      "Authorization": "Bearer " + self.token})
        return urllib.request.urlopen(req, timeout=600).read().decode()

    def js(self, script):
        out = self.post({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                         "params": {"name": "run_script",
                                    "arguments": {"script": script, "timeoutMs": 590000}}})
        inner = json.loads(json.loads(out)["result"]["content"][0]["text"])
        if not inner.get("ok"):
            raise RuntimeError("script failed: %s\n%s" % (inner.get("error"), script[:400]))
        return inner.get("result")

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(20)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.log.close()


def own_display():
    """ITS OWN DISPLAY at the rig geometry, range 301-340 (play_select owns 171-240,
    vr.eye_grade 241-299): the capture reads the root, so nothing else may be on it."""
    for n in range(301, 341):
        if os.path.exists("/tmp/.X11-unix/X%d" % n) or os.path.exists("/tmp/.X%d-lock" % n):
            continue
        xvfb = subprocess.Popen(["Xvfb", ":%d" % n, "-screen", "0", "1920x1080x24", "-nolisten", "tcp"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(40):
            if subprocess.call(["xdpyinfo", "-display", ":%d" % n], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL) == 0:
                return xvfb, ":%d" % n
            if xvfb.poll() is not None:
                break
            time.sleep(0.25)
        xvfb.kill()   # the pid this function spawned, never one found by name
        xvfb.wait()
    return None, None


SCENE = """
    project.create('Sky swap presented ' + Date.now());
    world.sky('realistic');
    world.clouds({enabled:true, coverage:0.5, density:1, speed:0, altitude:2000, shadow:1});
    var floor = scene.addPrimitive('cube', {position:{x:0,y:-0.5,z:0}, scale:{x:60,y:1,z:60}});
    material.set(floor, {baseColor:'#8a8a8a', metallic:0.0, roughness:0.6});
    var ball = scene.addPrimitive('sphere', {position:{x:0,y:2.5,z:0}, scale:{x:5,y:5,z:5}});
    material.set(ball, {baseColor:'#ffc35a', metallic:1.0, roughness:0.15});
    editor.selectNone();
    editor.frame(30, 1/60); 'ok'"""


def cam(y):
    return "editor.setCamera({position:{x:0,y:%s,z:11}, lookAt:{x:0,y:2.2,z:0}})" % y


# (name, preparation, change, the view moves)
ARMS = [
    ("disc_in",    "", "world.sunDisc({inProbes:true})", False),
    ("disc_out",   "", "world.sunDisc({inProbes:false})", False),
    ("cloud_alt",  "", "world.clouds({altitude:2400})", False),
    ("cloud_alt2", "", "world.clouds({altitude:2000})", False),
    ("clouds_off", "", "world.clouds({enabled:false})", False),
    ("clouds_on",  "", "world.clouds({enabled:true})", False),
    # THE 'were' REPRO: each tier starts at 6.0 m; 7.4 m is past 6.0 x 2^0.25 and
    # 5.0 m is past the band below it (Atmosphere.h kObserverBandOctaves; the
    # hysteresis is why the way down needs the longer move). The captures counter
    # proves both crossings captured. (Measured: 0.25 m moves across the same edges
    # capture too but land through the deferred SH read at the next frame's top,
    # which the twins' drain follows, so they never flashed — the long moves did.)
    ("band_up",    "", cam(7.4), True),
    ("band_down",  "", cam(5.0), True),
]

# (label, World Mode, extra set-up). Epic traces its reflections by rays on this
# box, and in this scene the rays hid the flash entirely (no dip on the base
# binary either), so Epic runs with rays OFF, where the base dipped 36-44 codes.
# Epic as shipped is covered by the live repro on 'were' (spikes/sky-swap-1).
TIERS = [
    ("medium", "medium", ""),
    ("epic_norays", "epic", "world.rayTracing('off')"),
]


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    binary, outdir = os.path.abspath(sys.argv[1]), sys.argv[2]
    for tool in ("xwd", "Xvfb", "xdpyinfo"):
        if not shutil.which(tool):
            say("SKIP: %s is not installed" % tool)
            return 77
    shutil.rmtree(outdir, ignore_errors=True)
    os.makedirs(outdir)

    xvfb, display = own_display()
    if not display:
        say("FAIL: could not start an Xvfb of my own")
        return 1
    say("own display %s (pid %d)" % (display, xvfb.pid))

    app = None
    failures = []
    try:
        app = App(binary, outdir, display)
        say("app up on port %s" % app.port)
        app.js("app.scriptPolicy('off'); 'ok'")

        def grab(name):
            data = subprocess.run(["xwd", "-root", "-silent", "-display", display],
                                  stdout=subprocess.PIPE, check=True).stdout
            with open(os.path.join(outdir, name + ".xwd"), "wb") as f:
                f.write(data)
            return read_xwd(data)

        serial = [0]

        def held(script, name):
            """Run `script` + one frame, freeze the UI thread, capture the presented root."""
            serial[0] += 1
            mark = "SKYSWAPMARK-%d-" % serial[0]
            err = []

            def run():
                try:
                    app.js("%s; editor.frame(1); log.write('app', 'warning', '%s'); "
                           "app.blockUiThread(%d); 'ok'" % (script, mark, FREEZE_MS))
                except Exception as e:   # noqa: BLE001 — reported below
                    err.append(e)
            t = threading.Thread(target=run)
            t.start()
            img = None
            deadline = time.time() + 120
            while time.time() < deadline and t.is_alive():
                if mark in app.text():
                    img = grab(name)
                    break
                time.sleep(0.02)
            t.join()
            if err:
                raise err[0]
            return img

        app.js(SCENE)
        vp = json.loads(app.js("JSON.stringify(editor.viewportState())"))
        vx, vy, vw, vh = vp["windowX"], vp["windowY"], vp["windowW"], vp["windowH"]
        # the main window sits at the root's origin on a WM-less Xvfb
        patches = {
            "sphere": (vx + vw // 2 - 40, vy + int(vh * 0.40), 80, 40),
            "floor": (vx + vw // 2 - 200, vy + int(vh * 0.88), 400, 30),
        }
        say("viewport %dx%d at %d,%d" % (vw, vh, vx, vy))

        def means(img):
            return {k: patch_mean(img, *r) for k, r in patches.items()}

        for tier, mode, setup in TIERS:
            app.js("world.mode({mode:'%s'}); %s; %s; editor.frame(90, 1/60); 'ok'" % (
                mode, setup or "true", cam(6.0)))
            for name, prep, change, moves_view in ARMS:
                label = "%s_%s" % (tier, name)
                if prep:
                    app.js("%s; editor.frame(60, 1/60); 'ok'" % prep)
                caps0 = app.js("world.clouds().live.changeCaptures")
                b = held("true", label + "_before")
                seq = [held(change, label + "_f0")]
                for i in range(1, FRAMES_AFTER):
                    seq.append(held("true", label + "_f%d" % i))
                a = held("editor.frame(60, 1/60)", label + "_after")
                caps = app.js("world.clouds().live.changeCaptures") - caps0
                if b is None or a is None or any(x is None for x in seq):
                    failures.append("%s: a capture never happened (the marker did not arrive)" % label)
                    continue
                if moves_view and caps < 1:
                    failures.append("%s: the camera move re-captured nothing (no band crossed)" % label)
                mb, ma = means(b), means(a)
                ms = [means(x) for x in seq]
                for k in patches:
                    vals = [m[k] for m in ms]
                    if moves_view:
                        # the change frame itself has no judge (its "before" is
                        # another view, and Epic's temporal passes settle after a
                        # move as a ramp); the capture the move asks for lands on
                        # the NEXT frame, which is judged against both neighbours
                        nb = vals + [ma[k]]
                        dips = [min(nb[i - 1], nb[i + 1]) - nb[i] for i in range(1, len(vals))]
                    else:
                        ref = min(mb[k], ma[k])
                        dips = [ref - v for v in vals]
                    worst = max(dips)
                    line = "%-18s %-6s before %7.2f  frames %s  after %7.2f  worst dip %6.2f  captures %d" % (
                        label, k, mb[k], " ".join("%7.2f" % v for v in vals), ma[k], worst, caps)
                    if worst > DIP_LIMIT:
                        failures.append(line)
                        say("FAIL " + line)
                    else:
                        say("ok   " + line)
    except Exception as e:   # noqa: BLE001 — every failure is a verdict
        failures.append("driver: %s" % e)
    finally:
        if app:
            app.close()
        if xvfb.poll() is None:
            xvfb.terminate()
            try:
                xvfb.wait(10)
            except subprocess.TimeoutExpired:
                xvfb.kill()
    if failures:
        say("FAILED — %d: a presented frame lost its sky environment" % len(failures))
        for f in failures:
            print("  " + f)
        return 1
    for f in os.listdir(outdir):   # the captures are 8 MB each; a pass keeps none
        if f.endswith(".xwd"):
            os.remove(os.path.join(outdir, f))
    say("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
