#!/usr/bin/env python3
"""document.reader_defaults — THE READER-DEFAULTS LAW, as a command.

ONE DEFINITION OF EVERY DEFAULT, AND IT IS THE CONSTRUCTOR'S (render audit I-5,
lane READER-DEFAULTS-1).

`src/io/scenereader.cpp` builds the document object FIRST — so it already holds
the constructor's value for every field — and then overwrites only the keys the
file carries:

    node->distance = nodeObj["distance"].toDouble(node->distance);   # right
    node->distance = nodeObj["distance"].toDouble(1.0);              # wrong

A LITERAL fallback is a second definition of a default, and two definitions of
one number drift. When they drift, "a scene that was never saved" and "a scene
whose file does not carry the key" become two different scenes, silently, for
ever. It has cost real pictures four times:

  * SUN1 (2026-09-12) — the shadow type read None against ShadowMap's Soft.
  * the darkness A/B (2026-09-13) — `exposure` read 0.0 against the scene's
    0.6, and FIVE SHIPPED SAMPLES rendered at half brightness because of it.
  * READER-DEFAULTS-1 (2026-09-17) — nine more, of which the loudest were a
    light's RADIUS (1 against 10), a light's shadow resolution (1024 against
    2048), the explorer camera's far clip (100 against 500) and six particle
    emitter fields that turned every pre-key emitter into a slow, dark,
    non-dissipating trickle.

WHY A LINT AND NOT A ROUND TRIP. The obvious test — write a node, strip every
optional key, read it back, compare — cannot be built: nothing in this tree
links `SceneReader`, which needs the library database, the asset store, the
material helper, the bake store and the load timeline (tests/mirror and
tests/defaults both record the constraint, and both test the reader's rules by
restating them instead). This scans the source, which is stronger in the one way
that matters: it fails on the NEXT literal somebody writes, not on a value
somebody remembered to assert. The runtime half — a real file with keys missing
— is `scene.reopen_fidelity` and `scripting.e2e.exposure_defaults`, which open
shipped archives written before those keys existed.

Every exception is listed below WITH ITS REASON. An exception is a default that
belongs to something other than a document field: a mathematical identity, a
retired key read on its way to the bin, a node TYPE, or a sentinel.
"""

import re
import sys
import os

READER = os.path.join("src", "io", "scenereader.cpp")

# A fallback handed to toDouble/toInt/toBool/toString that is a bare literal.
LITERAL = re.compile(
    r'\.to(?:Double|Int|Bool|String|Float)\(\s*'
    r'((?:[-+]?[0-9][0-9.]*f?)|true|false|"[^"]*")\s*\)')

# The allowed literal fallbacks: a UNIQUE substring of the line, and why the
# literal is not a document default. Anything not on this list fails.
ALLOWED = [
    # --- mathematical identities, not defaults of any field ------------------
    ('camRotQuat.value("scalar").toDouble(1.0)',
     'the IDENTITY quaternion (w=1, xyz=0) — a rotation with nothing said is '
     'no rotation, which is arithmetic and not a field default'),
    ('float(camRotQuat.value("x").toDouble(0.0))', 'the identity quaternion'),
    ('float(camRotQuat.value("y").toDouble(0.0))', 'the identity quaternion'),
    ('float(camRotQuat.value("z").toDouble(0.0))', 'the identity quaternion'),
    ('return iris::Quat(float(o["scalar"].toDouble(1.0))', 'the identity quaternion'),
    ('float(o["x"].toDouble(0.0))', 'the identity quaternion'),
    ('float(o["y"].toDouble(0.0))', 'the identity quaternion'),
    ('float(o["z"].toDouble(0.0))', 'the identity quaternion'),
    ('socket.rotation = iris::Quat(float(rot["scalar"].toDouble(1.0))',
     'the identity quaternion'),
    ('float(rot["x"].toDouble(0.0))', 'the identity quaternion'),
    ('float(rot["y"].toDouble(0.0))', 'the identity quaternion'),
    ('float(rot["z"].toDouble(0.0))', 'the identity quaternion'),

    # --- retired keys, read once on the way to the bin -----------------------
    ('sceneObj.value("fogStart").toDouble(100.0)',
     'the RETIRED linear fog pair. There is no fogStart field any more '
     '(CRUD, render audit I-6): the two keys are read into locals to derive a '
     'density for a file that predates fogDensity, and forgotten. 100/180 is '
     'the shape of that old fog, not a default of anything that exists'),
    ('sceneObj.value("fogEnd").toDouble(180.0)', 'the retired linear fog pair'),
    ('sceneObj.value("giAutoRefresh").toBool(true)',
     'a RETIRED key mapped onto giUpdateBudget: `false` meant budget 0 and '
     'absent meant the pre-fix-wave behaviour, which is what true expresses. '
     'Never written again'),

    # --- sentinels and enum parsing, not values ------------------------------
    ('casc.toInt(-1) < 0',
     "the cascade key's four historical spellings (absent / null / bool / "
     "int); -1 is the UNRESOLVED sentinel the tier resolves below, not a value"),
    ('casc.toInt(0) != 0', 'the same tri-state parse'),
    ('nodeObj["type"].toString("empty")',
     'a node TYPE, which selects a class — not a default value of a field'),
    ('mat["materialType"].toString("custom")',
     "the material FORMAT's own legacy spelling: a block with no type is the "
     'old custom-shader material, which is a format fact'),
    ('nodeObj["activeAnimation"].toInt(-1)',
     '-1 is "no active animation", a sentinel outside the index range'),
    ('l["default"].toBool(true)',
     'whether the locomotion clip ROLES in the FILE are the generated ones — '
     'a statement about the file, answered by markRolesFromFile'),

    # --- empty strings: absence, not a value ---------------------------------
    ('processObj["name"].toString("")', 'an absent name is the empty string'),
    ('sceneNode->name = nodeObj["name"].toString("")',
     'an absent name is the empty string'),
    ('QString source = nodeObj["mesh"].toString("")',
     'an absent mesh source is the empty string, which this function then '
     'reports on'),
]


def main(root):
    path = os.path.join(root, READER)
    if not os.path.isfile(path):
        print("reader_defaults: cannot find %s" % path)
        return 2
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")

    used = set()
    bad = []
    for number, line in enumerate(lines, 1):
        for match in LITERAL.finditer(line):
            hit = None
            for index, (needle, _why) in enumerate(ALLOWED):
                if needle in line:
                    hit = index
                    break
            if hit is None:
                bad.append((number, match.group(1), line.strip()))
            else:
                used.add(hit)

    for number, literal, text in bad:
        print("  FAIL %s:%d — a LITERAL fallback (%s):" % (READER, number, literal))
        print("       %s" % text)
        print("       The fallback must be the object's OWN value (the "
              "constructor's), e.g. .toDouble(node->field). If this literal "
              "really is not a document default, add it to ALLOWED in "
              "tests/document/reader_defaults.py WITH THE REASON.")

    stale = [ALLOWED[i][0] for i in range(len(ALLOWED)) if i not in used]
    for needle in stale:
        print("  FAIL an ALLOWED exception no longer matches anything: %r" % needle)
        print("       The line was changed or deleted — remove the exception "
              "rather than leaving a licence nobody uses.")

    if bad or stale:
        print("document.reader_defaults: FAILED (%d literal fallback(s), "
              "%d stale exception(s))" % (len(bad), len(stale)))
        return 1

    print("  ok: every stated fallback in %s is an expression, not a literal "
          "(%d documented exceptions, all live)" % (READER, len(ALLOWED)))
    print("document.reader_defaults: PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
