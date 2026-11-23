#!/usr/bin/env python3
"""mcp.engine_session - AN OUTSIDE CLIENT SEES THE ENGINE's PICTURE OVER MCP, EXACTLY (MCP-ON-HOST-1 stage 3, step 5).

THE DRIVER: `jahengine --mcp --data-root <the row's own dir>` with DISPLAY REMOVED from the child's environment (the engine
boots surfaceless: the server needs no display). Over the protocol (revision 2026-07-28, one JSON-RPC message a line) it
builds rtreflect.odd_size's scene (tests/rtreflect/odd_size.js: the 257 x 145 view, the floor, the wall, GI, ssr 1, the
camera) by the `call` tool with NAMED arguments - WITHOUT that row's test arm reflect.screenOff (engine-wide AND a test door:
an outside client is refused it) and its route declaration; async shaders stay off (the view's default: a frame compiles
what it needs first, so two processes draw the same frame). engine.frames {n: 89}, engine.view.readPixels (the 90th), then
engine.picture.hash, engine.picture.bytes and the `picture` tool {pictures: [h]} (1024 >= 257: no downscale).
THE LAWS:
  E1 the `picture` tool answers ONE image item (image/png) then ONE text item, its structured entry {picture, width 257,
     height 145, pngBytes = the PNG's length};
  E2 the PNG DECODES (its signature, every chunk's CRC-32, IHDR 257 x 145 at 8 bits, colour type 6 RGBA or 2 RGB - FreeImage
     writes an all-opaque picture as RGB, whose alpha is 255 by the PNG's own rule - the IDATs inflated by the standard
     library's zlib, the five PNG filters undone here) to engine.picture.bytes EXACTLY;
  E3 `jahengine --script` runs THE SAME CALLS as JavaScript (a scratch .js written in the row's dir) and prints its picture's
     hash: THE TWO HASHES ARE EQUAL (the MCP door draws what the script door draws);
  E4 the server exits 0 at stdin's end (L13).
It PRINTS the PNG's bytes and the alpha's min and max. ITS SELF-TEST FIRST: the decoder on PNGs this file writes with
each of the five filters (a decode equal to the source), and one flipped byte of a copy refused by its CRC.
Run: mcp_engine_session.py <jahengine> [--self-test]
"""
import base64
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib

ROW = "mcp.engine_session"
REVISION = "2026-07-28"
W, H = 257, 145
SIGNATURE = b"\x89PNG\r\n\x1a\n"


def say(line):
    print(line, flush=True)


# ---------------------------------------------------------------------------------------------------------------- the PNG
def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode_png(png):
    """(rgba bytes, width, height, colour type) of an 8-bit RGBA or RGB PNG, non-interlaced; ValueError naming the fault."""
    if png[:8] != SIGNATURE:
        raise ValueError("no PNG signature")
    at, ihdr, idat, ended = 8, None, b"", False
    while at < len(png):
        if at + 12 > len(png):
            raise ValueError(f"a chunk cut short at byte {at}")
        n, kind = struct.unpack(">I4s", png[at:at + 8])
        data = png[at + 8:at + 8 + n]
        crc = struct.unpack(">I", png[at + 8 + n:at + 12 + n])[0]
        if len(data) != n or zlib.crc32(kind + data) != crc:
            raise ValueError(f"chunk {kind!r} at byte {at}: its CRC-32 does not match")
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", data)
        elif kind == b"IDAT":
            idat += data
        elif kind == b"IEND":
            ended = True
            break
        at += 12 + n
    if not ihdr or not ended:
        raise ValueError("no IHDR or no IEND")
    w, h, depth, colour, _, _, interlace = ihdr
    if depth != 8 or colour not in (2, 6) or interlace != 0:
        raise ValueError(f"IHDR depth {depth}, colour type {colour}, interlace {interlace}: not 8-bit RGB(A), plain")
    bpp = 4 if colour == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    if len(raw) != h * (stride + 1):
        raise ValueError(f"the inflated data holds {len(raw)} bytes, not {h} x ({stride} + 1)")
    out, prev = bytearray(), bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                line[i] = (line[i] + paeth(a, b, c)) & 255
            elif f != 0:
                raise ValueError(f"row {y}: filter {f} is none of the five")
        prev = line
        if bpp == 4:
            out += line
        else:
            for x in range(w):
                out += line[x * 3:x * 3 + 3] + b"\xff"
    return bytes(out), w, h, colour


def encode_png(rgba, w, h, filters):
    """THE SELF-TEST's writer: an RGBA PNG whose row y is filtered by filters[y % len(filters)]."""
    stride, raw, prev = w * 4, bytearray(), bytes(w * 4)
    for y in range(h):
        line, f = rgba[y * stride:(y + 1) * stride], filters[y % len(filters)]
        raw.append(f)
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            pred = (0, a, b, (a + b) // 2, paeth(a, b, c))[f]
            raw.append((line[i] - pred) & 255)
        prev = line

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def self_test():
    faults = []
    w, h = 5, 7
    rgba = bytes((x * 37 + y * 11 + c * 53) & 255 for y in range(h) for x in range(w) for c in range(4))
    for f in range(5):
        got = decode_png(encode_png(rgba, w, h, [f]))
        if got[0] != rgba:
            faults.append(f"filter {f}: the decode differs from the source")
    mixed = encode_png(rgba, w, h, [0, 1, 2, 3, 4])
    if decode_png(mixed)[0] != rgba:
        faults.append("five filters mixed: the decode differs from the source")
    flipped = bytearray(mixed)
    flipped[8 + 8 + 3] ^= 1   # a byte of IHDR's width
    try:
        decode_png(bytes(flipped))
        faults.append("THE CONTROL: a flipped IHDR byte decoded (its CRC-32 was not checked)")
    except ValueError as e:
        if "CRC-32" not in str(e):
            faults.append(f"THE CONTROL: a flipped IHDR byte refused for another reason: {e}")
    for f in faults:
        say(f"FAIL: self-test: {f}")
    say(f"self-test: the decoder over the five filters and one flipped byte: {'clean' if not faults else 'RED'}")
    return len(faults)


# ---------------------------------------------------------------------------------------------------------------- the session
class Server:
    def __init__(self, jahengine, root, env):
        self.err = open(os.path.join(root, "jahengine-mcp.stderr"), "wb")
        self.p = subprocess.Popen([jahengine, "--mcp", "--data-root", root], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=self.err, env=env, cwd=root)
        self.next = 0

    def request(self, method, params):
        self.next += 1
        params = dict(params)
        params["_meta"] = {"io.modelcontextprotocol/protocolVersion": REVISION,
                           "io.modelcontextprotocol/clientCapabilities": {}}
        line = json.dumps({"jsonrpc": "2.0", "id": self.next, "method": method, "params": params}, separators=(",", ":"))
        self.p.stdin.write(line.encode() + b"\n")
        self.p.stdin.flush()
        while True:   # the server's own lines until this request's answer (a notification is skipped)
            got = self.p.stdout.readline()
            if not got:
                raise RuntimeError(f"the server closed its output before answering {method} (stderr: {self.err.name})")
            m = json.loads(got)
            if m.get("id") == self.next:
                if "error" in m:
                    raise RuntimeError(f"{method}: protocol error {m['error']}")
                return m["result"]

    def tool(self, name, arguments):
        return self.request("tools/call", {"name": name, "arguments": arguments})

    def call(self, command, **args):
        r = self.tool("call", {"command": command, "args": args})
        s = r["structuredContent"]
        if r.get("isError") or s.get("status") != "ok":
            raise RuntimeError(f"call {command}: {s}")
        return s["value"]

    def end(self):
        self.p.stdin.close()
        rc = self.p.wait()   # bounded by the server's own exit at stdin's end (L13); the row's ctest TIMEOUT above it
        self.err.close()
        return rc


# THE SCENE: rtreflect.odd_size's (tests/rtreflect/odd_size.js), as named calls - and the same calls as JavaScript
def build(s):
    view = s.call("engine.view.createOffscreen", desc={"name": "oddsize", "width": W, "height": H,
                                                       "background": [0.05, 0.05, 0.06, 1]})
    scene = s.call("engine.scene.create", name="oddsize")
    s.call("engine.view.setScene", view=view, bind={"scene": scene})
    s.call("engine.scene.setAmbient", scene=scene, ambient={"upper": [0.05, 0.05, 0.06, 1], "lower": [0.03, 0.03, 0.03, 1]})
    cube = s.call("engine.mesh.unitCube", scene=scene)
    floor, wall = s.call("engine.node.create", scene=scene), s.call("engine.node.create", scene=scene)
    metal = s.call("engine.material.createPbr", scene=scene,
                   params={"albedo": [0.9, 0.9, 0.9, 1], "metalness": 1.0, "roughness": 0.2})
    glow = s.call("engine.material.createPbr", scene=scene,
                  params={"albedo": [0, 0, 0, 1], "emissive": [0.9, 0.6, 0.2, 1], "roughness": 1.0})
    if not (s.call("engine.node.attachMesh", scene=scene, node=floor, draw={"mesh": cube, "material": metal}) and
            s.call("engine.node.attachMesh", scene=scene, node=wall, draw={"mesh": cube, "material": glow})):
        raise RuntimeError("the floor and the wall were not attached")
    s.call("engine.node.setTransform", scene=scene, node=floor, transform={"position": [0, -0.1, 0], "scale": [40, 0.2, 40]})
    s.call("engine.node.setTransform", scene=scene, node=wall, transform={"position": [0, 3, -8], "scale": [40, 6, 0.5]})
    if not s.call("engine.gi.set", scene=scene, params={"mode": "vct", "quality": "high", "bounces": True,
                                                        "cascades": [{"halfSize": 32, "resolution": 64, "stepCells": 0}]}):
        raise RuntimeError("GI did not build")
    s.call("engine.view.setPostFx", view=view, desc={"allowOffscreen": True, "ssr": 1})
    s.call("engine.view.lookAt", view=view, pose={"eye": [0, 1.6, 6], "target": [0, 0.4, -4]})
    s.call("engine.frames", n=89)
    shot = s.call("engine.view.readPixels", request={"view": view})   # the 90th frame
    return shot


SCRIPT = """// mcp.engine_session's scratch script: THE SAME CALLS as the MCP session's (mcp_engine_session.py build())
const view = engine.view.createOffscreen({name: "oddsize", width: %d, height: %d, background: [0.05, 0.05, 0.06, 1]});
const scene = engine.scene.create("oddsize");
engine.view.setScene(view, {scene});
engine.scene.setAmbient(scene, {upper: [0.05, 0.05, 0.06, 1], lower: [0.03, 0.03, 0.03, 1]});
const cube = engine.mesh.unitCube(scene);
const floor = engine.node.create(scene), wall = engine.node.create(scene);
const metal = engine.material.createPbr(scene, {albedo: [0.9, 0.9, 0.9, 1], metalness: 1.0, roughness: 0.2});
const glow = engine.material.createPbr(scene, {albedo: [0, 0, 0, 1], emissive: [0.9, 0.6, 0.2, 1], roughness: 1.0});
host.check({ok: engine.node.attachMesh(scene, floor, {mesh: cube, material: metal}) &&
                engine.node.attachMesh(scene, wall, {mesh: cube, material: glow}), what: "the floor and the wall exist"});
engine.node.setTransform(scene, floor, {position: [0, -0.1, 0], scale: [40, 0.2, 40]});
engine.node.setTransform(scene, wall, {position: [0, 3, -8], scale: [40, 6, 0.5]});
host.check({ok: engine.gi.set(scene, {mode: "vct", quality: "high", bounces: true,
                                      cascades: [{halfSize: 32, resolution: 64, stepCells: 0}]}), what: "GI builds"});
engine.view.setPostFx(view, {allowOffscreen: true, ssr: 1});
engine.view.lookAt(view, {eye: [0, 1.6, 6], target: [0, 0.4, -4]});
engine.frames(89);
const shot = engine.view.readPixels({view});
host.check({ok: shot.width === %d && shot.height === %d, what: "the picture reads back " + shot.width + " x " + shot.height});
console.log("SCRIPT HASH " + engine.picture.hash(shot.picture));
""" % (W, H, W, H)


def main():
    if len(sys.argv) < 2:
        say(__doc__)
        say(f"{ROW}: FAILED (1)")
        return 1
    failures = self_test()
    if "--self-test" in sys.argv[2:]:
        say(f"{ROW}: {'PASSED' if not failures else f'FAILED ({failures})'}")
        return 1 if failures else 0

    def law(ok, what):
        nonlocal failures
        say(("ok:   " if ok else "FAIL: ") + what)
        failures += 0 if ok else 1

    jahengine = sys.argv[1]
    # the row's own directory, made fresh, under the row's own HOME (jah_gpu_row's fresh home; the working directory without one)
    root = os.path.join(os.environ.get("HOME") or os.getcwd(), "mcp_engine_session.d")
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    env = {k: v for k, v in os.environ.items() if k != "DISPLAY"}   # NO DISPLAY: the server needs none
    s = Server(jahengine, root, env)
    try:
        shot = build(s)
        pic = shot["picture"]
        law(shot["width"] == W and shot["height"] == H, f"the picture reads back {shot['width']} x {shot['height']}")
        mcp_hash = s.call("engine.picture.hash", picture=pic)
        held = base64.b64decode(s.call("engine.picture.bytes", picture=pic)["$bytes"])
        law(len(held) == W * H * 4, f"engine.picture.bytes carries {len(held)} bytes ({W} x {H} x 4 = {W * H * 4})")
        answer = s.tool("picture", {"pictures": [pic]})
        content, entry = answer["content"], answer["structuredContent"]
        law(not answer.get("isError") and [c["type"] for c in content] == ["image", "text"] and
            content[0].get("mimeType") == "image/png",
            f"E1: the picture tool answers one image/png item then one text item ({[c['type'] for c in content]})")
        png = base64.b64decode(content[0]["data"]) if content and content[0]["type"] == "image" else b""
        want = {"picture": pic, "width": W, "height": H, "pngBytes": len(png)}
        law(entry.get("pictures") == [want], f"E1: its structured entry {entry.get('pictures')} == [{want}]")
        say(f"THE PNG: {len(png)} bytes for {W} x {H}")
        try:
            rgba, pw, ph, colour = decode_png(png)
            alpha = rgba[3::4]
            say(f"THE ALPHA: min {min(alpha)} max {max(alpha)} (colour type {colour})")
            law(pw == W and ph == H and rgba == held,
                f"E2: the PNG decodes ({pw} x {ph}, colour type {colour}) to engine.picture.bytes EXACTLY")
        except (ValueError, zlib.error) as e:
            law(False, f"E2: the PNG decodes: {e}")
    except RuntimeError as e:
        law(False, f"the session: {e}")
        mcp_hash = None
    rc = s.end()
    law(rc == 0, f"E4: the server exits {rc} at stdin's end (0 owed)")

    js = os.path.join(root, "same_calls.js")
    with open(js, "w") as f:
        f.write(SCRIPT)
    run = subprocess.run([jahengine, "--script", js, "--name", ROW + ".script"], capture_output=True, text=True, env=env,
                         cwd=root)
    script_hash = None
    for line in run.stdout.splitlines():
        if line.startswith("SCRIPT HASH "):
            script_hash = line.split()[-1]
    law(run.returncode == 0 and script_hash is not None,
        f"the same calls as a script ran to their verdict (exit {run.returncode}; its last line: "
        f"{(run.stdout.strip().splitlines() or ['(none)'])[-1]})")
    law(mcp_hash is not None and mcp_hash == script_hash,
        f"E3: THE TWO HASHES ARE EQUAL: the MCP session's {mcp_hash}, the script's {script_hash}")
    say(f"{ROW}: {'PASSED' if not failures else f'FAILED ({failures})'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
