#!/usr/bin/env python3
"""source.member_init — EVERY MEMBER OF OURS CARRIES A VALUE (UNINIT-SWEEP-1, 2026-09-21).

A family of defects, each one a member declared and never given a value, each found
by its own symptom: `ProjectManager::openInPlayMode` (every sample-browser open landed
in the Player, SMOKE-FIX-1), `MainWindow::previousSpace` (Ctrl+Tab's first press read a
garbage space), `WorldSettingsWidget::autoSave` (whether Save Scene appeared was
undefined at every launch), `CameraNode::isPerspective` (previews rendered orthographic
for years), `Mesh::numFaces` (a garbage triangle readout, STATS-1), the 24 `Database *`
of DBPTR-1. This gate is the population of that family, held at zero.

WHAT THIS ASSERTS

  For every `class`/`struct` body in scope, every member declared at that body's own
  depth whose type is

    (i)   a fundamental arithmetic type or one of the integer aliases listed in SCALAR
          (`size_t`, `qint*`, `quint*`, `qreal`, `qsizetype`, `(u)int*_t`,
          `uint/ushort/uchar/ulong`, `Real`, the `Vk*`/`Xr*` handles),
    (ii)  a pointer (a `*` in the declarator or in the type outside template arguments;
          function pointers included),
    (iii) an enum declared anywhere in scope, or a `Qt::`-qualified one,
    (iv)  `std::atomic<i|ii|iii>`, `std::array<i|ii|iii, N>`, or a C array of them,

  and which is not `static`, not a reference and not a function, MUST carry a default
  member initialiser (`= …` or `{…}`), OR be named in the mem-initialiser list or
  assigned in the body of EVERY constructor of the class that is neither `= default`
  nor `= delete` (a delegating constructor counts as its target).

  NOTHING ELSE COUNTS — not a helper the constructor calls, not an out-parameter, not
  "every caller sets it". The rule is stricter than "initialised somewhere" on purpose:
  a helper can grow an early return, an out-parameter can grow a second call site, and
  a caller who sets it is one refactor from a caller who does not. The one form this
  gate can see is also the one form every constructor of the class shares.

THE TWO FILES BESIDE IT — both carry a reason per line, `path Class::member  # why`:

  member_init_allow.txt     permanent exemptions: a member whose whole point is to
                            stay unwritten. Ten rows today, all of them the maths
                            types' `Qt::Uninitialized` overloads (a DMI would zero
                            the storage first and defeat the overload). Adding a line
                            here is a decision a reviewer sees in the diff.

  member_init_baseline.txt  THE SWEEP IS FINISHED AND THIS FILE IS GONE. It held the
                            rows UNINIT-SWEEP-1 had not reached yet — 506 of them at
                            phase 0 — and it could only SHRINK: the gate reds on a
                            flagged member in neither file AND on a baseline row whose
                            member is now compliant or gone, so a phase that half
                            finished could not leave it behind. It emptied at phase 4
                            and was deleted at phase 5. The mechanism stays: if a future
                            sweep ever needs a staged population again, write the file
                            (`--emit-baseline` prints it) and it works exactly as
                            before. A stale ALLOW row reds for the same reason.

  A FALSE POSITIVE IS ANSWERED WITH THE DEFAULT MEMBER INITIALISER, never with an
  allow row: the initialiser costs one line and is correct however the scanner is wrong.

BLIND SPOTS (this is grep-grade — no compiler; clang-tidy and cppcheck are not on this
box and clang's own `optin.cplusplus.UninitializedObject` sees only constructors it can
reach from an analysed call in the same TU, which is none of our widgets):

  * members declared through a macro;
  * a type alias to a scalar defined in a header outside scope — an unknown `X` is
    treated as a class type and NOT flagged (a false negative, never a false positive);
  * templates' dependent types;
  * a constructor defined in a file outside scope;
  * two classes with one name in two files (the twin `Plane`) are scanned per file and
    never merged — each file answers for its own.

Usage:  member_init.py <source-root>
        member_init.py <source-root> --self-test      the fixtures under fixtures/member_init/
        member_init.py <source-root> --emit-baseline  print the rows, baseline format
"""

import bisect
import collections
import os
import re
import sys

EXTS = {'.h', '.hpp', '.cpp', '.cc', '.inl'}

# The scope: our source. `tests/**` is deliberately out (232 candidates there, most of
# them ctor-less fixtures — a follow-up, not this gate's population).
SCOPE = ['src', 'irisgl/core', 'irisgl/document', 'irisgl/engine', 'irisgl/import',
         'irisgl/mirror', 'irisgl/irisgl.h', 'irisgl/irisglfwd.h']


def in_scope(rel):
    if '/thirdparty/' in rel or rel.startswith('thirdparty/'):
        return False
    if '_autogen' in rel or '/misc/QtAwesome' in rel:
        return False
    if '-old.' in os.path.basename(rel):
        return False
    return True


def source_files(root, roots=None):
    for r in (roots if roots is not None else SCOPE):
        p = os.path.join(root, r)
        if os.path.isfile(p):
            yield p
            continue
        for dirpath, dirnames, filenames in os.walk(p):
            dirnames.sort()
            for fn in sorted(filenames):
                full = os.path.join(dirpath, fn)
                if os.path.splitext(fn)[1] in EXTS and in_scope(os.path.relpath(full, root)):
                    yield full


def strip(text):
    """Comments and string/char literals blanked, newlines kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join('\n' if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
        elif c in '"\'':
            if c == '"' and i > 0 and text[i - 1] == 'R':
                m = re.match(r'R"([^(]*)\(', text[i - 1:])
                if m:
                    d = m.group(1)
                    end = text.find(')' + d + '"', i)
                    end = n if end < 0 else end + len(d) + 2
                    out.append(''.join('\n' if ch == '\n' else ' ' for ch in text[i:end]))
                    i = end
                    continue
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == '\\':
                    j += 1
                if j < n and text[j] == '\n':
                    break
                j += 1
            j = min(j + 1, n)
            out.append(c + ' ' * (j - i - 2) + c if j - i >= 2 else c)
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def match_brace(s, i):
    """s[i] == '{'; the index of the matching '}' (or len(s))."""
    d = 0
    for j in range(i, len(s)):
        if s[j] == '{':
            d += 1
        elif s[j] == '}':
            d -= 1
            if d == 0:
                return j
    return len(s)


CLASS_RE = re.compile(
    r'\b(class|struct)\s+'
    r'(?:(?:alignas\([^)]*\)|__attribute__\(\([^)]*\)\)|[A-Z_][A-Z0-9_]*EXPORT\w*|Q_DECL_\w+)\s+)*'
    r'(\w+)\s*(?:final\s*)?(?::\s*[^{;]*)?\{')

SCALAR = set('''bool char short int long unsigned signed float double size_t ssize_t wchar_t
char16_t char32_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t
uintptr_t ptrdiff_t quint8 quint16 quint32 quint64 qint8 qint16 qint32 qint64 qreal qsizetype
qintptr quintptr uint ushort uchar ulong GLuint GLint GLenum GLfloat GLsizei GLboolean uint8
uint16 uint32 uint64 int8 int16 int32 int64 Real VkDeviceSize VkBool32 XrTime XrBool32
XrSessionState XrSpace XrSession XrInstance XrSystemId HWND WId'''.split())

ATOMIC_RE = re.compile(r'^(std::)?atomic<\s*([\w:]+)\s*>$')
ARRAY_RE = re.compile(r'^std::array<\s*([\w:*]+)\s*,')

# Qt enum leaf names commonly held as members (a `Q…::<leaf>` member type).
QT_ENUMS = set('''Orientation MouseButton MouseButtons Key CheckState KeyboardModifiers
KeyboardModifier CursorShape SortOrder Alignment ToolButtonStyle FocusReason DropAction
WindowState WindowStates SocketState PlaybackState MediaStatus State Status Format ShaderType
Mode Type Kind Role'''.split())

SKIP_STMT = re.compile(
    r'^\s*(static|typedef|using|friend|template|enum|class|struct|union|operator|virtual|'
    r'explicit|inline|constexpr|Q_\w+|signals|public|private|protected|slots|namespace|extern|'
    r'return|if|for|while|else|do|switch|case|default|break|continue|goto|try|catch|throw|'
    r'delete|new|emit|#)\b')
ACCESS_RE = re.compile(r'\b(public|private|protected|signals|slots|Q_SIGNALS|Q_SLOTS)\s*(slots\s*)?:')
QTMACRO_RE = re.compile(
    r'\bQ_(OBJECT|GADGET|DISABLE_COPY(?:_MOVE)?|PROPERTY|ENUM|FLAG|ENUMS|DECLARE_\w+|INTERFACES|'
    r'INVOKABLE|NAMESPACE|CLASSINFO|SIGNAL|SLOT)\b(\s*\([^)]*\))?')


class Scanner:
    def __init__(self, root, roots=None):
        self.root = root
        self.texts = {}
        self.nospace = {}
        self.enums = set()
        # Every `class X` / `struct X` in scope. A name in BOTH sets is not an
        # enum here: enum names collide across files (irisgl's `enum Value` in
        # document/materials/material.h against materials::Value, the bake
        # program's own struct, which initialises itself) and this scanner has
        # no scopes. The collision is resolved towards "not an enum", which
        # costs a false negative and never a false positive — the rule this
        # gate is written under.
        self.class_names = set()
        self.aliases = {}
        for path in source_files(root, roots):
            with open(path, errors='replace') as fh:
                t = strip(fh.read())
            rel = os.path.relpath(path, root)
            self.texts[rel] = t
            # `Foo(` with the spaces out: the cheap pre-filter ctors_of() asks per class,
            # once per file instead of once per class per file (26 s -> 8 s on this tree).
            self.nospace[rel] = t.replace(' ', '')
            for m in re.finditer(r'\benum\s+(?:class\s+|struct\s+)?(\w+)', t):
                self.enums.add(m.group(1))
            for m in CLASS_RE.finditer(t):
                # `enum class X {` matches CLASS_RE too — that is the enum, not
                # a class of the same name.
                if t[:m.start()].rstrip().endswith('enum'):
                    continue
                self.class_names.add(m.group(2))
            for m in re.finditer(r'\busing\s+(\w+)\s*=\s*([\w:]+)\s*;', t):
                self.aliases[m.group(1)] = m.group(2)
            for m in re.finditer(r'\btypedef\s+([\w:\s]+?)\s+(\w+)\s*;', t):
                self.aliases[m.group(2)] = m.group(1).split()[-1]

    # --- the type test -------------------------------------------------------------
    def kind_of(self, ty):
        ty = re.sub(r'\b(const|volatile|mutable|typename|struct|enum|class)\b', ' ', ty)
        ty = ' '.join(ty.split())
        if not ty:
            return None
        outside = re.sub(r'<[^<>]*(?:<[^<>]*>[^<>]*)*>', '<>', ty)
        if '*' in outside:
            return 'pointer'
        if '&' in outside or '*' in ty:
            return None
        m = ATOMIC_RE.match(ty)
        if m:
            return 'atomic' if self.kind_of(m.group(2)) else None
        m = ARRAY_RE.match(ty)
        if m:
            return 'array' if self.kind_of(m.group(1)) else None
        if '<' in ty:
            return None
        toks = ty.split()
        if all(tk.split('::')[-1] in SCALAR or tk in SCALAR for tk in toks):
            return 'scalar'
        last = toks[-1]
        short = last.split('::')[-1]
        seen = set()
        while short in self.aliases and short not in seen:
            seen.add(short)
            short = self.aliases[short].split('::')[-1]
            if short in SCALAR:
                return 'scalar'
        if short in self.enums and short not in self.class_names:
            return 'enum'
        if last.startswith('Qt::') or (last.count('::') >= 1 and last.split('::')[0].startswith('Q')
                                       and short in QT_ENUMS):
            return 'enum'
        if short.endswith('Ptr') and short.startswith(('Vk', 'Xr')):
            return 'pointer'
        if short.startswith(('Vk', 'Xr')) and short[2:3].isupper() \
                and not short.endswith(('Info', 'Properties', 'Binding')):
            return 'handle'
        return None

    # --- class bodies --------------------------------------------------------------
    def members_of_body(self, text, body_start, body_end, line_of, cls, out):
        body = text[body_start + 1:body_end]
        top = list(body)
        i = 0
        while i < len(body):
            if body[i] == '{':
                j = match_brace(body, i)
                head_from = max(0, i - 200)
                nested = None
                for m in CLASS_RE.finditer(body[head_from:i + 1]):
                    if m.end() == i - head_from + 1:
                        nested = m
                if nested:
                    self.members_of_body(body, i, j,
                                         (lambda k, off=body_start + 1: line_of(off + k)),
                                         nested.group(2), out)
                for k in range(i + 1, min(j, len(body))):
                    if body[k] != '\n':
                        top[k] = ' '
                i = j + 1
            else:
                i += 1
        top = ''.join(top)
        top = ACCESS_RE.sub(' ', top)
        top = QTMACRO_RE.sub(' ', top)
        pos = 0
        for m in re.finditer(r'[;}]', top):
            stmt, off = top[pos:m.start()], pos
            pos = m.end()
            s = stmt.strip()
            if not s or SKIP_STMT.match(s):
                continue
            if '{' in s or '=' in s:        # a default member initialiser, or a function body
                continue
            if '(' in s:
                fp = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', s)
                if fp:
                    out.append((cls, fp.group(1), 'fnptr',
                                line_of(off + body_start + 1), 'function pointer'))
                continue
            if '[' in s and ']' in s:
                s2, arr = re.sub(r'\[[^\]]*\]', '', s), True
            else:
                s2, arr = s, False
            s2 = re.sub(r'<[^<>]*(?:<[^<>]*>[^<>]*)*>',
                        lambda mm: mm.group(0).replace(',', '\x00').replace(' ', ''), s2)
            parts = [pp.replace('\x00', ',') for pp in s2.split(',')]
            m0 = re.match(r'^(.*?[\w>\*&]|.*?)\s*(\*+|&+)?\s*(\w+)$', parts[0].strip())
            if not m0:
                continue
            ty = m0.group(1) + (' ' + m0.group(2) if m0.group(2) else '')
            names = [(m0.group(3), m0.group(2) or '')]
            for pp in parts[1:]:
                mm = re.match(r'^\s*(\*+|&+)?\s*(\w+)\s*$', pp)
                if mm:
                    names.append((mm.group(2), mm.group(1) or ''))
            for nm, stars in names:
                if '&' in stars:
                    continue
                kind = 'pointer' if '*' in stars else self.kind_of(ty)
                if not kind:
                    continue
                out.append((cls, nm, kind + ('[]' if arr else ''),
                            line_of(off + body_start + 1), ty.strip()))

    def classes(self):
        """rel -> [(class, member, kind, line, type)] for the members with no DMI."""
        per_file = {}
        for rel, t in self.texts.items():
            nl = [0]
            for m in re.finditer('\n', t):
                nl.append(m.end())

            def line_of(k, nl=nl):
                return bisect.bisect_right(nl, k)

            out = []
            for m in CLASS_RE.finditer(t):
                j = match_brace(t, m.end() - 1)
                self.members_of_body(t, m.end() - 1, j, line_of, m.group(2), out)
            if out:
                per_file[rel] = out
        return per_file

    # --- constructor coverage ------------------------------------------------------
    def ctors_of(self, cls):
        """Every constructor of `cls` found anywhere in scope: its mem-initialiser names,
        the names its body assigns, whether it delegates, whether it is `= default`."""
        found = []
        pat = re.compile(r'(?<![\w:~])(?:(?:\w+::)*)' + re.escape(cls) + r'::' + re.escape(cls)
                         + r'\s*\(|(?<![\w:~])' + re.escape(cls) + r'\s*\(')
        for rel, t in self.texts.items():
            if (cls + '(') not in self.nospace[rel] and (cls + '::') not in t:
                continue
            for m in pat.finditer(t):
                # A DELEGATING constructor's own call (`Foo() : Foo(0) {}`) and a base's
                # mem-initialiser look exactly like a constructor definition from here:
                # a name, a parameter list and a `{`. What tells them apart is the token
                # before — a mem-initialiser follows the `:` that opens the list or a `,`
                # inside it, and an access label's `:` is the one `:` that does not.
                before = t[:m.start()].rstrip()
                if before.endswith(','):
                    continue
                if re.search(r'(?<!:):$', before) and not re.search(
                        r'\b(public|private|protected|signals|slots|Q_SIGNALS|Q_SLOTS)\s*'
                        r'(slots\s*)?:$', before):
                    continue
                i = m.end() - 1
                k, d = i, 0
                while k < len(t):
                    if t[k] == '(':
                        d += 1
                    elif t[k] == ')':
                        d -= 1
                        if d == 0:
                            break
                    k += 1
                rest = t[k + 1:k + 400]
                rm = re.match(r'\s*(?:noexcept(?:\([^)]*\))?|override|const|Q_DECL_\w+|'
                              r'__attribute__\(\([^)]*\)\))*\s*(=\s*default|=\s*delete|;|:|\{)',
                              rest)
                if not rm:
                    continue
                kind = rm.group(1)
                if kind == ';' or kind.replace(' ', '').startswith('=delete'):
                    continue
                if kind.startswith('='):
                    found.append({'defaulted': True, 'init': set(), 'assigned': set(),
                                  'delegating': False})
                    continue
                start = k + 1 + rm.start(1)
                init, delegating = set(), False
                if kind == ':':
                    d, q = 0, start + 1
                    while q < len(t):
                        if t[q] in '(<[':
                            d += 1
                        elif t[q] in ')>]':
                            d -= 1
                        elif t[q] == '{' and d == 0:
                            break
                        q += 1
                    for im in re.finditer(r'([\w:]+)\s*[({]', t[start + 1:q]):
                        nm = im.group(1).split('::')[-1]
                        if nm == cls:
                            delegating = True
                        init.add(nm)
                    b = q
                else:
                    b = start
                e = match_brace(t, b)
                body = t[b:e]
                assigned = set(re.findall(r'(?<![\w.>])(?:this->)?(\w+)\s*(?:=(?!=)|\+=|-=|\|=|&=)',
                                          body))
                assigned |= set(re.findall(r'(?<![\w.>])(?:this->)?(\w+)\s*\[[^\]]*\]\s*=(?!=)',
                                           body))
                if re.search(r'memset\s*\(\s*this', body):
                    assigned.add('*memset*')
                for am in re.finditer(r'memset\s*\(\s*&?\s*(?:this->)?(\w+)', body):
                    assigned.add(am.group(1))
                found.append({'defaulted': False, 'init': init, 'assigned': assigned,
                              'delegating': delegating})
        return found

    def flagged(self):
        """[(rel, line, class, member, kind, type)] — the rows this gate holds at zero."""
        per_file = self.classes()
        ctor_cache = {}
        rows, seen = [], set()
        for rel in sorted(per_file):
            for cls, nm, kind, line, ty in per_file[rel]:
                if cls not in ctor_cache:
                    ctor_cache[cls] = self.ctors_of(cls)
                real = [c for c in ctor_cache[cls] if not c['defaulted']]
                if real and all(c['delegating'] or nm in c['init'] or nm in c['assigned']
                                or '*memset*' in c['assigned'] for c in real):
                    continue
                key = (rel, cls, nm)
                if key in seen:
                    continue
                seen.add(key)
                rows.append((rel, line, cls, nm, kind, ty))
        rows.sort(key=lambda r: (r[0], r[1], r[2], r[3]))
        return rows


# --- the two files ----------------------------------------------------------------------
def read_rows(path):
    """`path Class::member  # why` -> {(path, Class, member): (lineno, why)}."""
    rows = {}
    if not os.path.exists(path):
        return rows
    with open(path) as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.split('#')[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 2 or '::' not in parts[1]:
                rows[('?', '?', 'malformed line %d' % n)] = (n, raw.strip())
                continue
            cls, mem = parts[1].split('::', 1)
            rows[(parts[0], cls, mem)] = (n, raw.split('#', 1)[1].strip() if '#' in raw else '')
    return rows


def phase_of(rel):
    if rel.startswith(('irisgl/core/', 'irisgl/document/', 'irisgl/import/', 'irisgl/mirror/')) \
            or rel.startswith('irisgl/irisgl'):
        return 1
    if rel.startswith('irisgl/engine/'):
        return 2
    if rel.startswith('src/modules/materials/') or rel.startswith('src/bridge/') \
            or os.path.basename(rel).startswith('assetwidget.'):
        return 4
    return 3


# THE SCANNER'S OWN GUARD. A gate whose scanner quietly stopped flagging passes every
# day and says nothing — so the fixtures run FIRST, on every invocation, and the tree
# scan is only reached once the scanner has proved it still sees all five kinds, the
# `some constructors only` case, the `= default` case, a nested class, and none of the
# nine compliant forms (gate_scope_rules.py guards gate-scope.py for the same reason).
SELF_TEST_FLAGGED = ['Flagged::count', 'Flagged::owner', 'Flagged::mode', 'Flagged::grid',
                     'Flagged::busy', 'SomeCtorsMiss::b', 'NoCtorAggregate::x',
                     'NoCtorAggregate::p', 'DefaultedOnly::n', 'Nested::deep']
SELF_TEST_COMPLIANT = 9
EXEMPT_REASON = "the maths types' Qt::Uninitialized overloads"


def self_test(root, here):
    rel = os.path.relpath(os.path.join(here, 'fixtures', 'member_init'), root)
    got = sorted('%s::%s' % (c, m) for _, _, c, m, _, _ in Scanner(root, [rel]).flagged())
    want = sorted(SELF_TEST_FLAGGED)
    if got != want:
        print('  FAIL: the scanner no longer reads its own fixtures '
              '(tests/hygiene/fixtures/member_init/fixture.h)')
        print('        missing: %s' % (sorted(set(want) - set(got)) or 'none'))
        print('        spurious: %s' % (sorted(set(got) - set(want)) or 'none'))
        return False
    print('  ok: the fixtures flag exactly the %d uninitialised members and none of the %d '
          'compliant ones' % (len(want), SELF_TEST_COMPLIANT))
    return True


def main(argv):
    root = os.path.abspath(argv[1] if len(argv) > 1 else '.')
    here = os.path.dirname(os.path.abspath(__file__))
    mode = argv[2] if len(argv) > 2 else ''

    if mode == '--self-test':
        if not self_test(root, here):
            print('source.member_init: SELF-TEST FAILED')
            return 1
        print('source.member_init: SELF-TEST PASSED')
        return 0

    if mode != '--emit-baseline' and not self_test(root, here):
        print('source.member_init: FAILED (1)')
        return 1

    rows = Scanner(root).flagged()

    if mode == '--emit-baseline':
        for rel, line, cls, nm, kind, ty in rows:
            print('%s %s::%s  # %s, %s:%d — phase %d'
                  % (rel, cls, nm, kind, rel, line, phase_of(rel)))
        return 0

    allow = read_rows(os.path.join(here, 'member_init_allow.txt'))
    base = read_rows(os.path.join(here, 'member_init_baseline.txt'))
    failures = []

    def fail(msg):
        failures.append(msg)
        print('  FAIL: %s' % msg)

    for key in sorted(k for k in list(allow) + list(base) if k[0] == '?'):
        fail('%s' % key[2])

    live = set()
    for rel, line, cls, nm, kind, ty in rows:
        key = (rel, cls, nm)
        live.add(key)
        if key in allow or key in base:
            continue
        fail('%s:%d %s::%s (%s) — add a default member initialiser '
             '(a member with no value is read before it is written the day someone '
             'adds a path to the class)' % (rel, line, cls, nm, kind))

    for key, (n, why) in sorted(base.items()):
        if key[0] == '?':
            continue
        if key not in live:
            fail('member_init_baseline.txt:%d %s %s::%s is initialised (or gone) — delete the '
                 'baseline row: this file can only shrink' % (n, key[0], key[1], key[2]))
    for key, (n, why) in sorted(allow.items()):
        if key[0] == '?':
            continue
        if key not in live:
            fail('member_init_allow.txt:%d %s %s::%s is initialised (or gone) — delete the '
                 'exemption rather than leaving a reason nobody needs' % (n, key[0], key[1], key[2]))

    if failures:
        print('source.member_init: FAILED (%d)' % len(failures))
        return 1
    if base:
        print('  ok: %d member(s) flagged by the rule, %d permanently exempt (%s), %d still on '
              'the baseline — delete the file when it empties'
              % (len(rows), len(allow), EXEMPT_REASON, len(base)))
    else:
        print('  ok: every scalar, pointer, enum, atomic and array member in scope carries a '
              'value; the %d the rule flags are the permanently exempt ones (%s) and there is '
              'no baseline left' % (len(rows), EXEMPT_REASON))
    print('source.member_init: PASSED')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
