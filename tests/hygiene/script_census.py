#!/usr/bin/env python3
"""source.script_census - THE ENGINE's COMMAND SET IS COUNTED BY AN EXPLICIT MAP (SCRIPT-IN-ENGINE-1 stage 2f, the train's tail;
the lead's rulings 2026-11-23).

ITS INPUTS: tests/hygiene/script_census/<family>.txt (the MAP: each line `<command> <Class>::<verb> [<where>]`, `#` a comment),
declared.txt (`<Class>::<verb> <its reason>`: a verb DECLARED, NOT WRAPPED), waiting.txt (`<Class>::<verb> <why>`: THE WAITING
SET, whose length is THE CEILING), Engine.h (the public verbs of Scene, View and Engine), the public fixtures, enginetesthelpers.h
and src/lab (who calls a verb), and the build's `jahengine --list --test-doors` (what the host registers).
THE LAWS:
  C1 every mapped command is REGISTERED (read from `jahengine --list --test-doors`: a documentation-only host, no engine);
  C2 every mapped verb is a PUBLIC verb of its class in Engine.h AND is acted on in its family's file - commands_<family>.cpp,
     or the file the line's third column names (a deleter's call: `commands_engine.cpp`) - by a call (`->verb(`, `.verb(`,
     `Class::verb`), or, for a verb that acts through Commands::submitScene, by the FrameInputScene FIELD the third column
     names (`field:fog`) being written there (`part.fog =`) and declared in FrameInput.h's FrameInputScene;
  C3 THE UNCOVERED SET - the public verbs the public fixtures, enginetesthelpers.h and src/lab call, minus the mapped verbs,
     minus declared.txt - IS waiting.txt, exactly: a verb uncovered and not waiting is RED (the 29th: map it or declare it with
     its reason); a waiting verb no longer uncovered is RED too (the ceiling only FALLS: take its line out in the same commit);
  C4 every declared / waiting line names a public verb of Engine.h (a stale line is RED), and no verb is both mapped and
     declared (or waiting).
A call inside command code that no map line names does NOT cover a verb. A PUBLIC FIXTURE is a tests/**/*.cpp (tests/hygiene
excluded) that includes a `jahshaka/engine/` header or enginetesthelpers.h and none of EngineInspect.h, EnginePrivate.h,
irisgl/document/, irisgl/mirror/, an Ogre header (the fixtures' private doors). Comments are stripped everywhere.
THE CONTROL (above zero): the fixture-and-helper verbs read and the lab's verbs read.
Its self-test first, on its own toy tree (never the live one): the clean toy passes; one planted fault per law reds it.

Run: script_census.py <source-root> <build-dir>
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

BAD = ('EngineInspect.h', 'EnginePrivate.h', 'irisgl/document/', 'irisgl/mirror/', '<Ogre', '"Ogre')


def strip(t):
    """The code alone: comments, and string literals' text (a doc string NAMING a verb is no call of it)."""
    t = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', t)
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def read(p):
    with open(p, errors='replace') as f:
        return f.read()


def public_verbs(engine_h):
    """{class: set(verb)} - the public methods of Scene, View and Engine, at the class's own depth."""
    out, cls, depth, access, buf = {}, None, 0, None, ''
    for line in read(engine_h).split('\n'):
        code = re.sub(r'//.*', '', line)
        m = re.match(r'^class (Scene|View|Engine) \{', code)
        if cls is None:
            if m:
                cls, depth, access, buf = m.group(1), 1, 'private', ''
                out[cls] = set()
            continue
        if depth == 1 and re.match(r'^\s*(public|protected|private):', code):
            access = re.match(r'^\s*(\w+):', code).group(1)
            continue
        if depth == 1 and code.strip():
            buf += ' ' + code.strip()
        opens, closes = code.count('{'), code.count('}')
        if depth == 1 and buf and (';' in code or '{' in code):
            mm = re.search(r'([A-Za-z_]\w*)\s*\(', buf)
            d = buf.strip()
            if access == 'public' and mm and not d.startswith(('using', 'friend', 'enum', 'struct', 'class', 'static_assert')):
                if mm.group(1) not in (cls, 'operator', 'decltype', 'alignas'):
                    out[cls].add(mm.group(1))
            buf = ''
        depth += opens - closes
        if depth <= 0:
            cls = None
    return out


def callrx(verb):
    return re.compile(r"(?:(?:->|\.)\s*" + re.escape(verb) + r"\s*\(|\b(?:Scene|View|Engine)::" + re.escape(verb) + r"\b)")


def lines_of(path):
    out = []
    for no, line in enumerate(read(path).split('\n'), 1):
        if line.strip() and not line.lstrip().startswith('#'):
            out.append((no, line.split()))
    return out


def judge(root, listing):
    """(faults, notes, fixture verbs, lab verbs) for a source root and the text of `jahengine --list --test-doors`."""
    faults, notes = [], []
    census = os.path.join(root, 'tests', 'hygiene', 'script_census')
    src = os.path.join(root, 'irisgl', 'engine', 'src', 'script')
    api = public_verbs(os.path.join(root, 'irisgl', 'engine', 'include', 'jahshaka', 'engine', 'Engine.h'))
    frame_scene = ''
    fi = os.path.join(root, 'irisgl', 'engine', 'include', 'jahshaka', 'engine', 'FrameInput.h')
    if os.path.exists(fi):
        m = re.search(r'struct FrameInputScene \{(.*?)\n\};', strip(read(fi)), flags=re.S)
        frame_scene = m.group(1) if m else ''
    registered = set(re.findall(r'^(engine\.[\w.]+)\(', listing, flags=re.M))

    def is_verb(cv):
        c, _, v = cv.partition('::')
        return v in api.get(c, ())

    mapped = {}
    for path in sorted(glob.glob(os.path.join(census, '*.txt'))):
        fam = os.path.basename(path)[:-4]
        if fam in ('declared', 'waiting'):
            continue
        for no, f in lines_of(path):
            where = '%s.txt:%d' % (fam, no)
            if len(f) not in (2, 3):
                faults.append('C2 %s: `%s` is no `<command> <Class>::<verb> [<where>]` line' % (where, ' '.join(f)))
                continue
            cmd, cv = f[0], f[1]
            mapped.setdefault(cv, []).append(cmd)
            if cmd not in registered:
                faults.append('C1 %s: %s is not registered (jahengine --list --test-doors does not list it)' % (where, cmd))
            if not is_verb(cv):
                faults.append('C2 %s: %s is no public verb of Engine.h' % (where, cv))
                continue
            third = f[2] if len(f) == 3 else ''
            fname = third if third.endswith('.cpp') else 'commands_%s.cpp' % fam
            fpath = os.path.join(src, fname)
            body = strip(read(fpath)) if os.path.exists(fpath) else None
            if body is None:
                faults.append('C2 %s: %s has no file %s' % (where, cmd, fname))
            elif third.startswith('field:'):
                field = third[len('field:'):]
                if not re.search(r'\b' + re.escape(field) + r'\b', frame_scene):
                    faults.append('C2 %s: FrameInputScene declares no field %s' % (where, field))
                elif not re.search(r'\bpart\.' + re.escape(field) + r'\s*=', body):
                    faults.append('C2 %s: %s writes no part.%s in %s (the verb %s acts through it)' % (where, cmd, field, fname, cv))
            elif not callrx(cv.split('::')[1]).search(body):
                faults.append('C2 %s: %s maps %s, which %s never calls' % (where, cmd, cv, fname))
    declared = {f[0]: ('declared.txt:%d' % no) for no, f in lines_of(os.path.join(census, 'declared.txt'))}
    waiting = {f[0]: ('waiting.txt:%d' % no) for no, f in lines_of(os.path.join(census, 'waiting.txt'))}
    for cv, where in list(declared.items()) + list(waiting.items()):
        if not is_verb(cv):
            faults.append('C4 %s: %s is no public verb of Engine.h (a stale line)' % (where, cv))
        if cv in mapped:
            faults.append('C4 %s: %s is mapped too (%s)' % (where, cv, ', '.join(mapped[cv])))
    for cv in set(declared) & set(waiting):
        faults.append('C4 %s is both declared and waiting' % cv)
    # who calls a verb: the public fixtures, the helpers, the lab
    fixtures = []
    for p in glob.glob(os.path.join(root, 'tests', '**', '*.cpp'), recursive=True):
        rel = os.path.relpath(p, root)
        if rel.startswith(os.path.join('tests', 'hygiene') + os.sep):
            continue
        raw = read(p)
        if ('#include "jahshaka/engine/' in raw or 'enginetesthelpers.h' in raw) and not any(b in raw for b in BAD):
            fixtures.append(strip(raw))
    hp = os.path.join(root, 'tests', 'support', 'enginetesthelpers.h')
    if os.path.exists(hp):
        fixtures.append(strip(read(hp)))
    lab = [strip(read(p)) for p in glob.glob(os.path.join(root, 'src', 'lab', '**', '*.*'), recursive=True)
           if p.endswith(('.cpp', '.h'))]
    # every name the texts CALL (`->name(` / `.name(`) or qualify (`Scene|View|Engine::name`), gathered once - callrx's
    # forms, one pass a text instead of one regex a verb
    called = re.compile(r'(?:->|\.)\s*([A-Za-z_]\w*)\s*\(|\b(?:Scene|View|Engine)::([A-Za-z_]\w*)\b')

    def names(texts):
        out = set()
        for t in texts:
            for m in called.finditer(t):
                out.add(m.group(1) or m.group(2))
        return out

    fix_n, lab_n = names(fixtures), names(lab)
    fix_v = {'%s::%s' % (c, v) for c, verbs in api.items() for v in verbs if v in fix_n}
    lab_v = {'%s::%s' % (c, v) for c, verbs in api.items() for v in verbs if v in lab_n}
    uncovered = (fix_v | lab_v) - set(mapped) - set(declared)
    for cv in sorted(uncovered - set(waiting)):
        faults.append('C3 %s is called by %s and neither mapped, declared nor waiting: map it (a command) or declare it with '
                      'its reason (the waiting set only falls)' % (cv, 'a public fixture' if cv in fix_v else 'src/lab'))
    for cv in sorted(set(waiting) - uncovered):
        faults.append('C3 %s (%s) is no longer uncovered: take its line out of waiting.txt (the ceiling falls)' % (cv, waiting[cv]))
    notes.append('%d public fixture sources (+ enginetesthelpers.h); %d fixture-and-helper verbs, %d lab verbs (THE CONTROL: '
                 'each above zero); %d mapped, %d declared; UNCOVERED %d, THE CEILING (waiting.txt) %d'
                 % (len(fixtures) - (1 if os.path.exists(hp) else 0), len(fix_v), len(lab_v), len(mapped), len(declared),
                    len(uncovered), len(waiting)))
    if not fix_v or not lab_v:
        faults.append('the control read zero (fixture verbs %d, lab verbs %d): the walk read nothing' % (len(fix_v), len(lab_v)))
    return faults, notes, len(fix_v), len(lab_v)


TOY = {
    'irisgl/engine/include/jahshaka/engine/Engine.h':
        'class Scene {\npublic:\n    void setFog(int);\n    void setLodBias(float);\n    void setSky(int);\n    void tune();\n'
        '    void probe();\nprotected:\n    void hidden();\n};\n'
        'class View {\npublic:\n    void resize(int, int);\n};\n'
        'class Engine {\npublic:\n    Scene *createScene(const char *);\n    void destroyScene(Scene *);\n    void halt();\n};\n',
    'irisgl/engine/include/jahshaka/engine/FrameInput.h': 'struct FrameInputScene {\n    int fog;\n};\n',
    'irisgl/engine/src/script/commands_scene.cpp':
        'void r() {\n  s->setLodBias(1);\n  k.submitScene(c, s, [&](FrameInputScene &part) { part.fog = d; });\n'
        '  e->createScene("x");\n}\n',
    'irisgl/engine/src/script/commands_engine.cpp': 'void d() { st->engine->destroyScene(dead); }\n',
    'tests/hygiene/script_census/scene.txt':
        'engine.scene.create Engine::createScene\nengine.scene.destroy Engine::destroyScene commands_engine.cpp\n'
        'engine.scene.setLodBias Scene::setLodBias\nengine.scene.setFog Scene::setFog field:fog\n',
    'tests/hygiene/script_census/declared.txt': 'Engine::halt the boot owns it\n',
    'tests/hygiene/script_census/waiting.txt': 'Scene::tune a test-only reader\n',
    'tests/x/test_a.cpp': '#include "jahshaka/engine/Engine.h"\nvoid t() { s->setLodBias(2); s->tune(); e->halt(); }\n',
    'tests/x/test_private.cpp': '#include "jahshaka/engine/Engine.h"\n#include "EnginePrivate.h"\nvoid t() { s->probe(); }\n',
    'tests/support/enginetesthelpers.h': 'void h() { s->setFog(1); }\n',
    'src/lab/lab.cpp': 'void l() { v->resize(1, 1); }\n',
}
TOY_LIST = ('engine.scene.create(name: string) -> scene\nengine.scene.destroy(scene) -> bool\n'
            'engine.scene.setLodBias(scene, bias) -> null\nengine.scene.setFog(scene, fog) -> null\n')


def toy(base, edits=None, drop=()):
    d = tempfile.mkdtemp(dir=base)
    files = dict(TOY)
    files.update(edits or {})
    for rel, text in files.items():
        if rel in drop:
            continue
        p = os.path.join(d, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w') as f:
            f.write(text)
    return d


def selftest():
    ok = True

    def expect(what, faults, law):
        nonlocal ok
        good = (not faults) if law is None else (len(faults) == 1 and faults[0].startswith(law))
        print('  %s %s (%s)' % ('ok:  ' if good else 'FAIL:', what, faults), flush=True)
        ok = ok and good

    with tempfile.TemporaryDirectory(prefix='script_census_toy_') as base:
        # (View::resize is the lab's: uncovered and not waiting in the clean toy unless declared - declare it)
        clean = {'tests/hygiene/script_census/declared.txt': 'Engine::halt the boot owns it\nView::resize the lab\'s\n'}
        f, _, nfix, nlab = judge(toy(base, clean), TOY_LIST)
        expect('the clean toy passes (its control: %d fixture verbs, %d lab verbs, a private fixture\'s probe() not read)'
               % (nfix, nlab), f if nfix == 3 + 1 and nlab == 1 else f + ['control'], None)
        expect('C1 red: a mapped command the host does not register',
               judge(toy(base, clean), TOY_LIST.replace('engine.scene.setLodBias(', 'engine.scene.lodBias('))[0], 'C1')
        e = dict(clean)
        e['irisgl/engine/src/script/commands_scene.cpp'] = TOY['irisgl/engine/src/script/commands_scene.cpp'].replace(
            '  s->setLodBias(1);\n', '')
        expect('C2 red: one mapped verb\'s call removed from its family file', judge(toy(base, e), TOY_LIST)[0], 'C2')
        e = dict(clean)
        e['irisgl/engine/src/script/commands_scene.cpp'] = TOY['irisgl/engine/src/script/commands_scene.cpp'].replace(
            '  s->setLodBias(1);\n', '  doc("the LOD dial (Scene::setLodBias): s->setLodBias(x)");\n')
        expect('C2 red: the call gone, only a doc string naming the verb left', judge(toy(base, e), TOY_LIST)[0], 'C2')
        e = dict(clean)
        e['irisgl/engine/src/script/commands_scene.cpp'] = TOY['irisgl/engine/src/script/commands_scene.cpp'].replace(
            'part.fog = d;', '(void)d;')
        expect('C2 red: a submitScene verb whose field is not written', judge(toy(base, e), TOY_LIST)[0], 'C2')
        e = dict(clean)
        e['tests/hygiene/script_census/scene.txt'] = TOY['tests/hygiene/script_census/scene.txt'].replace(
            ' commands_engine.cpp', '')
        expect('C2 red: a deleter\'s call looked for in the family file without its third column',
               judge(toy(base, e), TOY_LIST)[0], 'C2')
        e = dict(clean)
        e['tests/x/test_a.cpp'] = TOY['tests/x/test_a.cpp'].replace('e->halt();', 'e->halt(); s->setSky(1);')
        expect('C3 red: ONE undeclared public verb planted in a public fixture', judge(toy(base, e), TOY_LIST)[0], 'C3')
        e = dict(clean)
        e['tests/x/test_a.cpp'] = TOY['tests/x/test_a.cpp'].replace(' s->tune();', '')
        expect('C3 red: a waiting verb no longer called (the ceiling must fall)', judge(toy(base, e), TOY_LIST)[0], 'C3')
        e = dict(clean)
        e['tests/hygiene/script_census/declared.txt'] = clean['tests/hygiene/script_census/declared.txt'] + 'Scene::gone x\n'
        expect('C4 red: a declared line naming no public verb', judge(toy(base, e), TOY_LIST)[0], 'C4')
        e = dict(clean)
        e['tests/x/test_a.cpp'] = '// s->setSky(1);\n' + TOY['tests/x/test_a.cpp']
        expect('quiet on a commented call', judge(toy(base, e), TOY_LIST)[0], None)
    return ok


def main():
    root, build = sys.argv[1], sys.argv[2]
    print('the laws catch (a toy tree):', flush=True)
    toys = selftest()
    exe = os.path.join(build, 'bin', 'jahengine')
    r = subprocess.run([exe, '--list', '--test-doors'], capture_output=True, text=True)
    bad = 0 if toys else 1
    if r.returncode != 0:
        print('  FAIL: %s --list --test-doors exited %d: %s' % (exe, r.returncode, (r.stdout + r.stderr)[-400:]), flush=True)
        bad += 1
    faults, notes, _, _ = judge(root, r.stdout)
    print('the tree (%s):' % root, flush=True)
    for n in notes:
        print('  ' + n, flush=True)
    for f in faults:
        print('  ' + f, flush=True)
    bad += len(faults)
    print('source.script_census: PASSED' if not bad else 'source.script_census: FAILED (%d)' % bad, flush=True)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
