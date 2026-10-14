#!/usr/bin/env python3
"""source.scene_no_photon_members — PHOTON'S STATE IS PHOTON'S (ENGINE-UPGRADE-2B stage 1).

OgreScene held Photon's whole state — the voxel cascades, the irradiance field, the probe
grid, the GI scheduler, the cards' inputs, the photon view — as ~150 of its own members,
read and written by OgreGi.cpp's methods. They now live in PhotonComponent
(photon/PhotonComponent.h), the scene holding the component and the component holding a
back-reference to the scene for the scene's own facts.

WHICH MEMBERS ARE PHOTON'S is a COMMITTED NAMED LIST (photon_members.txt beside this
script, one name per line) — never a prefix rule: the prefixes `mGi`/`mProbe`/`mScan`/
`mSun`... were wrong at both ends (the sky's sun disc and the light census matched them;
the PCC's `mPcc*`, the surface cache and the staleness serials did not).

WHAT THIS ASSERTS, both halves:
  (1) OgreScene's class body (irisgl/engine/src/EnginePrivate.h) declares NONE of the
      listed names as a data member: the count is 0 (it was 147 before the move);
  (2) PhotonComponent's class body declares EVERY listed name — a list that names a
      member nobody declares is a stale list, and a stale list makes (1) vacuous.
It prints OgreScene's declaration count (its data members and member-function
declarations at class depth) as a report line, not a bar.

$1 = the repo root.
"""
import os
import re
import sys


def strip_code(text):
    """Comments and string/char literals out (newlines kept)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            out.append('\n' * text.count('\n', i, n if j < 0 else j))
            i = n if j < 0 else j + 2
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def class_body(text, head_re):
    """The text between `class X ... {` (matched by head_re at a line start) and its `};`."""
    m = re.search(head_re, text, re.M)
    if not m:
        return None
    i = text.index('{', m.start())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
    return None


def statements(body):
    """The class-depth statements of a class body, nested braces collapsed to {}."""
    stmts, cur, depth = [], [], 0
    for ch in body:
        if ch == '{':
            if depth == 0:
                cur.append('{')
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                cur.append('}')
                s = ''.join(cur).strip()
                # a function body ends the statement; `struct X {...} mY;` does not
                if re.search(r'\)\s*(const\s*)?(noexcept\s*)?(override\s*)?(final\s*)?(->[^{]*)?\{\}$', s) \
                        or re.match(r'^(struct|class|union|enum|namespace)\b[^;]*\{\}$', s) is None and \
                        re.search(r'\)[^(){};]*\{\}$', s):
                    stmts.append(s)
                    cur = []
        elif depth == 0:
            if ch == ';':
                stmts.append(''.join(cur).strip())
                cur = []
            else:
                cur.append(ch)
    return [s for s in stmts if s]


def strip_templates(s):
    out, depth = [], 0
    for ch in s:
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    return ''.join(out)


def split_top(s, sep=','):
    parts, cur, depth = [], [], 0
    for ch in s:
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        if ch == sep and depth == 0:
            parts.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append(''.join(cur))
    return parts


def data_members(body):
    """(names of the data members, count of declarations) at class depth."""
    names, decls = [], 0
    for s in statements(body):
        s = re.sub(r'^\s*(public|private|protected)\s*:\s*', '', s)
        while re.match(r'^\s*(public|private|protected)\s*:', s):
            s = re.sub(r'^\s*(public|private|protected)\s*:\s*', '', s)
        s = s.strip()
        if not s or re.match(r'^(using|typedef|friend|static_assert|template)\b', s):
            continue
        if re.match(r'^(struct|class|union|enum)\b', s):
            # a type; `struct X {...} mY` also declares mY
            tail = s[s.rfind('}') + 1:] if '}' in s else ''
            if not re.search(r'[A-Za-z_]', tail):
                continue
            decls += 1
            for d in split_top(tail):
                ids = re.findall(r'[A-Za-z_]\w*', re.split(r'[={\[:]', d)[0])
                if ids:
                    names.append(ids[-1])
            continue
        t = strip_templates(s)
        head = re.split(r'=|\{', t, maxsplit=1)[0]
        decls += 1
        if '(' in head and not re.search(r'\(\s*\*', head):
            continue                      # a member function
        if re.match(r'^static\b', t):
            continue                      # a class constant, not a member
        for d in split_top(t):
            d = re.split(r'=|\{|\[|(?<!:):(?!:)', d, maxsplit=1)[0]
            ids = re.findall(r'[A-Za-z_]\w*', d)
            if ids:
                names.append(ids[-1])
    return names, decls


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    here = os.path.dirname(os.path.abspath(__file__))
    listed = []
    for line in open(os.path.join(here, 'photon_members.txt')):
        line = line.split('#', 1)[0].strip()
        if line:
            listed.append(line)
    src = os.path.join(root, 'irisgl', 'engine', 'src')
    scene_text = strip_code(open(os.path.join(src, 'EnginePrivate.h')).read())
    photon_text = strip_code(open(os.path.join(src, 'photon', 'PhotonComponent.h')).read())
    scene = class_body(scene_text, r'^class OgreScene final : public Scene \{')
    photon = class_body(photon_text, r'^class PhotonComponent\b[^;{]*\{')
    if scene is None or photon is None:
        print('source.scene_no_photon_members: FAIL — the OgreScene or PhotonComponent class body was not found')
        return 1
    scene_names, scene_decls = data_members(scene)
    photon_names, _ = data_members(photon)
    in_scene = [n for n in listed if n in set(scene_names)]
    missing = [n for n in listed if n not in set(photon_names)]
    print(f'source.scene_no_photon_members: {len(listed)} named Photon members; OgreScene declares '
          f'{len(scene_names)} data members in {scene_decls} declarations (a report, not a bar)')
    bad = False
    if in_scene:
        bad = True
        print(f'source.scene_no_photon_members: FAIL — OgreScene still declares {len(in_scene)} of them: '
              + ' '.join(in_scene))
    if missing:
        bad = True
        print(f'source.scene_no_photon_members: FAIL — PhotonComponent does not declare {len(missing)} '
              'listed name(s) (a stale list): ' + ' '.join(missing))
    if bad:
        return 1
    print('source.scene_no_photon_members: ok — 0 of the named Photon members in OgreScene, '
          'all of them in PhotonComponent')
    return 0


if __name__ == '__main__':
    sys.exit(main())
