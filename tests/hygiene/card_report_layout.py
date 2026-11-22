#!/usr/bin/env python3
# source.card_report_layout — THE REPORT OF JahCardIndirect_cs.glsl IS SIZED TO WHAT THE SHADER WRITES (GATHER-HANG-1 stage 2).
#
# WHAT WAS WRONG. The shader's report (caReport) is the count, `capacity` listings (caMode.z), then FRAME WORDS written
# past the capacity: mode 1's clear stores caReport[1u + caMode.z] and caReport[2u + caMode.z] from thread 0. The surface
# cache sized its report 3 + capacity; the irradiance field sized its probe marks' report 1 + probes, so every clear
# wrote two words past it into the next block of Ogre's shared buffer pool (GPU-assisted validation: 32,772 bytes
# bound at 8,192 probes, a store reaching byte 32,779).
#
# THE RULE: irisgl/engine/src/CardReportLayout.h is the layout's one home. (1) The highest constant k of any
# `caReport[ku + caMode.z]` in the shader equals kCardReportFrameWords there, and cardReportWords(c) is
# 1 + c + kCardReportFrameWords. (2) Each buffer the shader's report is bound to - the surface cache's mCardReport and
# the field's mMarkReport - is created through cardReportWords(...), and the cards' read-back too.
#
# argv[1] = the repo root
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from webgpu_census import strip  # noqa: E402  (the one comment stripper; its cases are the census's self-test)

SRC = "irisgl/engine/src"
SHADER = "irisgl/engine/media/Hlms/Jahshaka/JahCardIndirect_cs.glsl"
HEADER = SRC + "/CardReportLayout.h"
# (the file, the statement's start) for each create of a report buffer, and the cards' read-back
CREATES = (("irisgl/engine/src/photon/voxel/PhotonIrradianceField.cpp", re.compile(r"\bmMarkReport\s*=\s*[\w:>.\-]+\(")),
           ("irisgl/engine/src/OgreSurfaceCache.cpp", re.compile(r"\bmCardReport\s*=\s*[\w:>.\-]+\(")),
           ("irisgl/engine/src/OgreSurfaceCache.cpp", re.compile(r"\bmCardReport->readRequest\(")))
FRAME_WRITE = re.compile(r"\bcaReport\s*\[\s*(\d+)u?\s*\+\s*caMode\.z\s*\]")


def code_lines(text):
    """the text's lines with every comment blanked (webgpu_census.strip, the one stripper; line numbers kept)"""
    return strip(text).split("\n")


def read(root, rel, bad):
    try:
        return open(os.path.join(root, rel), encoding="utf-8").read()
    except OSError as e:
        bad.append("cannot read %s (%s)" % (rel, e))
        return None


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    bad = []
    shader = read(root, SHADER, bad)
    ks = []
    if shader is not None:
        for line in code_lines(shader):
            ks += [int(k) for k in FRAME_WRITE.findall(line)]
        if not ks:
            bad.append("%s: no caReport[ku + caMode.z] write found (the row's pattern no longer matches the shader)" % SHADER)
    header = read(root, HEADER, bad)
    frame = None
    if header is not None:
        text = "\n".join(code_lines(header))
        m = re.search(r"kCardReportFrameWords\s*=\s*(\d+)u?\s*;", text)
        if not m:
            bad.append("%s: no kCardReportFrameWords" % HEADER)
        else:
            frame = int(m.group(1))
        if not re.search(r"cardReportWords\s*\(\s*uint32_t\s+(\w+)\s*\)\s*\{\s*return\s+1u\s*\+\s*\1\s*\+\s*"
                         r"kCardReportFrameWords\s*;", text):
            bad.append("%s: cardReportWords(c) is not 1u + c + kCardReportFrameWords" % HEADER)
    if ks and frame is not None and max(ks) != frame:
        bad.append("the shader writes %d frame word(s) past the capacity (caReport[%du + caMode.z]); "
                   "CardReportLayout.h says kCardReportFrameWords = %d" % (max(ks), max(ks), frame))
    for rel, start in CREATES:
        text = read(root, rel, bad)
        if text is None:
            continue
        lines = code_lines(text)
        found = 0
        for n, line in enumerate(lines):
            if not start.search(line):
                continue
            found += 1
            stmt, j = line, n
            while ";" not in stmt and j + 1 < len(lines):   # the whole statement, however it is wrapped
                j += 1
                stmt += " " + lines[j]
            if "cardReportWords(" not in stmt:
                bad.append("%s:%d: a report buffer sized by hand, not by cardReportWords: %s"
                           % (rel, n + 1, " ".join(stmt.split())))
        if found == 0:
            bad.append("%s: no statement matching %s (the row's pattern no longer matches the code)" % (rel, start.pattern))
    if bad:
        print("source.card_report_layout: FAIL - JahCardIndirect_cs.glsl's report is not sized to what the shader writes:")
        for b in bad:
            print("    " + b)
        sys.stdout.flush()
        print("source.card_report_layout: FAILED (%d)" % len(bad))
        return 1
    print("source.card_report_layout: ok - the shader writes %d frame words past the capacity; CardReportLayout.h says %d; "
          "both report buffers and the read-back go through cardReportWords" % (max(ks), frame))
    print("source.card_report_layout: PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
