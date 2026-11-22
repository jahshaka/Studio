#!/usr/bin/env python3
# source.gpu_table_strides (ATOM-HOUSE-1 stage 1, the audit's finding 1) - THE GPU SCENE'S TWO TABLE STRIDES HAVE ONE HOME.
# GpuScene::kLevelsPerMesh (the level table: `meshIndex * stride + level`) and GpuScene::kSubmeshesPerMesh (a row block:
# `row + level * stride + submesh`) reach the Hlms media only as the properties `jah_levels_per_mesh` /
# `jah_submeshes_per_mesh` (GpuScene::setTableStrideProperties for the compute jobs, HlmsAtom's merged properties for the
# decode). A hand copy (`* 8u`) agrees today and goes silently wrong the day a constant moves: a ray hit's far copy or a
# posed item then reads another mesh's rows, and the picture is the first thing to notice.
#
# WHAT IT LINTS, exactly (every .any / .glsl under irisgl/engine/media, `//` comments dropped, case-insensitive):
#   S1  an identifier naming a level, mesh, row or submesh times a literal 8:   `\w*(level|mesh|row|submesh)\w* * 8u`
#   S2  the same, the literal first:                                            `8u * \w*(level|mesh|row|submesh)\w*`
#   S3  a hand #define of either stride:                    `#define JAH_LEVELS_PER_MESH <digit>`, `..._SUBMESHES_...`
# NOT CAUGHT (the residual, said here so nobody trusts more than this): a copy through an identifier that names none of
# the four words (the old decode's loop `atomL < 8u` was one: it is a property now), and any other spelling of 8. The
# media's other `* 8u` (a brick's 8^3 cells, a byte shift, a state word's index) name none of the four words: 0 false hits
# over the tree at this row's commit. The ray tier's build-time GLSL (irisgl/engine/src/rayquery) takes no Hlms property
# and carries no stride arithmetic (grepped at this commit); it is not linted.
# FIRST, THE NEGATIVE CONTROL: the exact lines this row's commit replaced must be flagged, and the media's lawful `8u`
# lines must not.
#
# argv: <studio source root>
import os
import re
import sys

WORDS = r"\w*(?:level|mesh|row|submesh)\w*"
RULES = [
    ("S1", re.compile(r"\b" + WORDS + r"\s*\*\s*8u\b", re.I)),
    ("S2", re.compile(r"\b8u\s*\*\s*" + WORDS, re.I)),
    ("S3", re.compile(r"#\s*define\s+JAH_(?:LEVELS|SUBMESHES)_PER_MESH\s+\d")),
]


def fail(msg):
    print("FAIL: " + msg)
    print("source.gpu_table_strides: FAILED (1)")
    sys.stdout.flush()
    sys.exit(1)


def hits(text):
    """(line number, rule, line) for every stride copy in a media file's text, `//` comments dropped."""
    out = []
    for n, line in enumerate(text.splitlines(), 1):
        code = line.split("//", 1)[0]
        for name, rx in RULES:
            if rx.search(code):
                out.append((n, name, line.strip()))
                break
    return out


def main():
    if len(sys.argv) != 2:
        fail("usage: gpu_table_strides.py <studio source root>")
    media = os.path.join(sys.argv[1], "irisgl", "engine", "media")

    must = ["\t\t\t\t\tuint atomLAt = atomMesh * 8u + atomL;",
            "\t\t\tuint atomLevelAtRaw = atomMesh * 8u + atomLevel;",
            "\t\t\tatomRowSrc = atomRaster.z != 0xFFFFFFFFu ? atomRaster.z + atomLevel * 8u : atomLevelRow.w;",
            "\tuint geomRow = posed ? skinRow + level * 8u : L.geomRow;",
            "#define JAH_LEVELS_PER_MESH 8u",
            "\tuint b = 8u * levelIndex;"]
    lawful = ["\tconst uint3 jahCell = jahBrick * 8u + uint3( jahSub & 1u, ( jahSub >> 1u ) & 1u, jahSub >> 2u ) * 4u +",
              "\t\t\t\ttag |= 8u;",
              "\tdst.v[o + 8u] = outTan.z;",
              "\t\tconst uint bone = min( ( packedIdx >> ( 8u * k ) ) & 0xFFu, lastBone );",
              "\t// level l submesh 0 at row + 8 l, a mesh * 8u in a comment",
              "\tuint atomLAt = atomMesh * @value( jah_levels_per_mesh )u + atomL;"]
    missed = [m for m in must if not hits(m)]
    false_hits = [m for m in lawful if hits(m)]
    if missed or false_hits:
        fail("the negative control: missed %s, flagged %s" % (missed, false_hits))
    print("ok:   the negative control: the %d replaced lines are flagged, the %d lawful ones are not"
          % (len(must), len(lawful)))

    if not os.path.isdir(media):
        fail("no media tree at %s" % media)
    files, found = 0, []
    for d, _, names in os.walk(media):
        for name in sorted(names):
            if not name.endswith((".any", ".glsl")):
                continue
            files += 1
            p = os.path.join(d, name)
            with open(p, encoding="utf-8", errors="replace") as f:
                for n, rule, line in hits(f.read()):
                    found.append("%s:%d [%s] %s" % (os.path.relpath(p, sys.argv[1]), n, rule, line))
    if not files:
        fail("no .any / .glsl file under %s" % media)
    if found:
        print("FAIL: %d hand copies of a GPU-scene table stride in the media (GpuScene.h is their one home: take the"
              " property, JahLevelTable's macro or `@value( jah_levels_per_mesh )u`):" % len(found))
        for h in found:
            print("        " + h)
        print("source.gpu_table_strides: FAILED (1)")
        sys.stdout.flush()
        sys.exit(1)
    print("ok:   %d media files, no hand copy of either stride" % files)
    print("source.gpu_table_strides: PASSED")
    sys.stdout.flush()


if __name__ == "__main__":
    main()
