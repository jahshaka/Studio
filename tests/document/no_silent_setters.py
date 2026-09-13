#!/usr/bin/env python3
"""document.no_silent_setters — EVERY SETTER ON A SCENE NODE REPORTS ITSELF.

SPECS/DIRTY_SET_MIRROR_SPEC.md §3.8 item 3. Since the dirty-set mirror, the
renderer only looks at a node the DOCUMENT says changed — so a setter that
writes a reflected field and reports nothing is a change that never reaches the
screen. It is not a crash and it is not visible in any pixel test written
against the scenes we happen to have: the background verifier pushes it a second
or two later, so the screen is merely LATE, and only `verifierCatches` says so.

This is the static half of that law, shaped like theme.no_raw_sheets: a grep
over the scene-graph headers and their .cpp files for `void set*(...)` bodies
that assign a member and never reach notifyChanged / markedParams / touch.

It would have caught the two the lead's second read found by hand: thirteen
ParticleSystemNode typed setters (the emitter panel drives two of them live) and
the CameraNode lens setters.

ALLOWED, BY NAME AND WITH A REASON — never by a blanket pattern:
  * setters that only touch state no mirror latch reads;
  * the `_` raw setters whose whole contract is "replay without side effects",
    where the caller marks (and which the reviewer of this file must check).
"""
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
SCAN = [ROOT / "irisgl/document/scenegraph", ROOT / "irisgl/document/materials"]

# name -> why it is allowed to be silent. Adding a line here is a DECISION.
ALLOWED = {
    # Identity and bookkeeping the mirror never reads.
    "setGUID": "the node's identity; no engine state derives from it",
    "setScene": "scene membership — it INSTALLS the collector; see _setDirtySet",
    "setSocketAttachment": "the raw reader/duplication setter; Scene::attachToSocket marks",
    "setAttached": "outliner expand state; marks anyway (listed for completeness)",
    # Runtime-only state the mirror reads from elsewhere or every frame.
    "setPlaying": "iris::Scene, not a node — the play edge is a full-walk trigger",
    "setPlayMode": "iris::Scene, not a node",
    "setChangeObserver": "deleted; kept here so a re-introduction fails loudly",
    # Raw replay setters: the contract is "no side effects", the caller marks.
    "_setGraphNode": "iris::graph's own migration hook; the adoption guard sees it",
    "_setFolderPath": "marks (listed for completeness)",
    "_clearStaticHint": "the GRAPH class only; no mirror latch reads it",
    "_setCountsAsMovement": "the transform-write EPOCH's exemption (nodegraph.h): read by "
                            "iris::graph on the write path, never by a mirror latch, and "
                            "written once — by CameraNode's constructor",
    "_setDirtySet": "installs the collector itself",
    "_setSoftMovable": "marks (listed for completeness)",
    "_setMobility": "marks (listed for completeness)",
    "_applyStaticHint": "marks (listed for completeness)",
    # Materials: the mark is Material::touch(), checked below by the same rule.
    "setName": "both classes mark (SceneNode) / touch (Material)",
    "setGuid": "the material's asset guid; not a rendered field",
    "setRenderLayer": "retired with the legacy render list; nothing reads it",
    "setBlendState": "iris::RenderStates, hashed through the material fingerprint",
    # NOT NODES. These live under scenegraph/ but are scene-level or helper
    # state the mirror reads through its own signatures, not through a visit.
    "setSkyTextureSource": "iris::Scene's sky; applySky has its own signature",
    "_setGraphEvacuationHook": "a callback slot on iris::Scene, not document state",
    "setPoseSource": "SocketResolver's pose callback; not a document node at all",
    "setStagingScene": "iris::graph's process-wide device handle",
    "setSkyTexture": "iris::Scene's sky; applySky keeps its own SkySource signature",
    "setSkyColor": "iris::Scene's sky; same signature",
    "setAmbientMusic": "audio; the renderer has no opinion about it",
    "setAmbientMusicVolume": "audio",
    "setCamera": "iris::Scene's VIEWPORT camera — applyCamera pushes it every frame",
    "setGraphScene": "the bind itself, and a bind is a full-walk trigger",
    "setOutlineWidth": "iris::Scene; syncHighlight pushes the outline on its own latch",
    "setOutlineColor": "iris::Scene; same",
    "setOutlinePrimaryColor": "iris::Scene; same",
    "setResolution": "iris::ShadowMap; LightNode::setShadowMapResolution is the marked wrapper",
    "setPostProcesses": "the retired post-process manager; nothing in the renderer reads it",
    "setRasterizerState": "iris::RenderStates, hashed through the material fingerprint",
    "setDepthState": "iris::RenderStates, hashed through the material fingerprint",
}

MARKERS = ("notifyChanged", "markedParams", "notifyChangedSubtree", "touch()",
           "markChanged", "_setDirtySet", "setPropertyValue")

# `void setFoo(args)` — the brace may be on the same line (headers) or on the
# next one (the .cpp style), which is why this matches the SIGNATURE and the
# body scan finds the brace. (The first draft required them on one line and so
# read no .cpp at all — it passed a tree whose camera setters were all silent.)
OPEN = re.compile(r"^\s*(?:virtual\s+)?void\s+(?:\w+::)?(_?set\w+)\s*\([^;{}]*\)\s*(?:const\s*)?\{?\s*$"
                  r"|^\s*(?:virtual\s+)?void\s+(?:\w+::)?(_?set\w+)\s*\([^;{}]*\)\s*(?:const\s*)?\{.*$")

# An assignment TO A MEMBER, which is what "writes a reflected field" means.
# The left side has to be ONE lvalue chain and nothing else: that is what
# separates `speed = s` and `shadowMap->bias = v` from `const float v = ...`
# and `iris::Vec3 p = ...`, which are locals and are not a document write.
LVALUE = re.compile(r"^\s*(?:this->)?[A-Za-z_]\w*"
                    r"(?:\s*\[[^\]]*\]|\s*\.\w+|\s*->\w+)*\s*$")
ASSIGN_OP = re.compile(r"(?<![=!<>+\-*/%&|^])=(?!=)")


def writes_member(inner):
    """True when `inner` assigns to something that looks like a member."""
    for stmt in inner.split(";"):
        # Strip trailing // comments so a commented-out write is not a write.
        stmt = re.sub(r"//.*", "", stmt)
        m = ASSIGN_OP.search(stmt)
        if not m:
            continue
        if LVALUE.match(stmt[: m.start()]):
            return True
    return False

def bodies(path):
    """Yields (name, line, body-text) for every void set*( ... ) { ... }."""
    text = path.read_text(errors="replace").split("\n")
    i = 0
    while i < len(text):
        m = OPEN.match(text[i])
        if not m:
            i += 1
            continue
        name = m.group(1) or m.group(2)
        j = i
        # The brace opens here or on one of the next lines (an initialiser list
        # or a wrapped argument list may sit between).
        while j < len(text) and "{" not in text[j]:
            if ";" in text[j]:          # a DECLARATION, not a definition
                break
            j += 1
        if j >= len(text) or "{" not in text[j]:
            i += 1
            continue
        body = text[i:j + 1]
        depth = sum(l.count("{") - l.count("}") for l in body)
        while depth > 0 and j + 1 < len(text):
            j += 1
            body.append(text[j])
            depth += text[j].count("{") - text[j].count("}")
        yield name, i + 1, "\n".join(body)
        i = j + 1

bad = []
scanned = 0
for d in SCAN:
    for path in sorted(list(d.glob("*.h")) + list(d.glob("*.cpp"))):
        for name, line, body in bodies(path):
            scanned += 1
            if name in ALLOWED:
                continue
            inner = body[body.index("{") + 1:]      # past the signature
            if not writes_member(inner):
                continue                     # a forwarder, not a write
            if any(k in body for k in MARKERS):
                continue
            bad.append((path.relative_to(ROOT), line, name))

print(f"scanned {scanned} void set*() bodies under "
      + ", ".join(str(d.relative_to(ROOT)) for d in SCAN))
if bad:
    print("FAIL: setters that write a reflected field and report NOTHING "
          "(SPECS/DIRTY_SET_MIRROR_SPEC.md §3.8):")
    for path, line, name in bad:
        print(f"  {path}:{line}  {name}()")
    print("\nAdd notifyChanged(NodeChange::<kind>) — or `return markedParams();` for a "
          "setPropertyValue branch, or touch() on a material — at the end of the body. "
          "If the field really is one no renderer reads, say so BY NAME in this "
          "script's ALLOWED table with the reason.")
    sys.exit(1)
print("ok:   every setter that writes a reflected field reports it")
