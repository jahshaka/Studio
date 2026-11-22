#!/usr/bin/env python3
"""source.no_qt_below - NO Qt BELOW THE EDITOR's UI, AND WHAT IS LEFT OF IT ONLY FALLS (NOQT-CORE-1 stage 0; V2 plan 0.12).

The owner's ruling (2026-11-22): nothing below the editor's UI carries Qt. Until the programme ends, what is left is held at
CEILINGS, one line per directory and kind, in no_qt_below_ceilings.txt beside this file (taken at photon-7 a87874341):
  kind qt     every Qt TOKEN and every Qt INCLUDE line - the irisgl/* directories in full (a C++ file under irisgl/, the
              vendored thirdparty/ apart, that sits in no line is held at 0);
  kind qtsql  QSql* tokens and QSql*/QtSql include lines only - one line per src/* directory (a src/ file in no line is held
              at 0); the src/** general ceilings come with the services' own specs.
A TOKEN is a match of TOKEN below (Q-types, q-functions called, Q_/QT_ macros, the q-integer types, qreal, signals:/slots:)
in the text with its comments and its string and character literals blanked; an INCLUDE is a comment-free #include of a
Qt header. THE LAW, both ways, per line and per measure (tokens, includes):
  count > ceiling  RED - every site of that line and measure is printed (which one is new needs history);
  count < ceiling  RED - "lower the ceiling to M in the same commit", so a fall is recorded and cannot creep back;
  a line at 0 stays 0; a line whose paths are gone is RED (delete the line).
THE Qt-FREE TARGET, IrisCore (irisgl/core/CMakeLists.txt), read from the build:
  T1 its compile lines in build.ninja (at least one) name no Qt include directory, no Qt define and no Qt code generator's
     output (an _autogen directory) - so no Qt header can be reached;
  T2 `nm -C` of its archive lists at least one defined symbol (it read an archive) and names no Qt symbol;
  T3 no source of it includes, transitively, a header outside irisgl/core/ and the standard library.
THE APP's Qt-CONVERSION HEADER, src/app/qtstd.h: no C++ file under irisgl/ includes it (it lives above the line).
Run: no_qt_below.py <source-root> <build-dir> [--counts]   (--counts prints the measured lines, never fails: a builder's aid)
The self-test runs first, always: the stripper's cases, every token and include form counted and its commented and quoted
copies not, the law both ways, an unheld file, T1-T3 on fixtures, the qtstd.h rule.
"""
import bisect
import os
import re
import subprocess
import sys

ROW = 'source.no_qt_below'
TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'no_qt_below_ceilings.txt')
CPP = ('.h', '.hpp', '.hh', '.cpp', '.cc', '.cxx', '.c', '.inl', '.mm')
UNIVERSE = {'qt': 'irisgl', 'qtsql': 'src'}     # every C++ file under it is held by a line of the kind, else at 0
VENDORED = {'thirdparty'}                       # directory names never read (vendored trees are not ours)

TOKEN = re.compile(r'\bQ[A-Z][a-z]\w*|\bq[A-Z]\w*\s*\(|\bQ_[A-Z_]+|\bQT_[A-Z_]+|\bqu?int(?:8|16|32|64)\b|\bqreal\b'
                   r'|\b(?:signals|slots)\s*:')
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*<(?:Q[A-Za-z0-9_/]*|qt[A-Za-z0-9_/]*|q[a-z]+\.h)>', re.M)
SQL_TOKEN = re.compile(r'\bQSql\w*')
SQL_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*<(?:QSql\w*|QtSql\b[^>]*)>', re.M)
KINDS = {'qt': (TOKEN, INCLUDE), 'qtsql': (SQL_TOKEN, SQL_INCLUDE)}
ANY_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"\n]+)[>"]', re.M)
QTSTD = re.compile(r'(?:^|/)qtstd\.h$')

# ---- the stripper: comments blanked (newlines kept, so file:line stays true); literals blanked or kept ----------------
_LEX = re.compile(r'R"([^ ()\\\t\n"]{0,16})\((?:.|\n)*?\)\1"'      # a raw string literal
                  r'|//[^\n]*'
                  r'|/\*(?:.|\n)*?(?:\*/|\Z)'
                  r'|"(?:\\.|[^"\\\n])*"'
                  r"|'(?:\\.|[^'\\\n])*'")


def strip(text, keep_literals=False):
    def repl(m):
        t = m.group(0)
        if t.startswith('//'):
            return ''
        if t.startswith('/*'):
            return ' ' + '\n' * t.count('\n')
        if keep_literals:
            return t
        return ("''" if t.startswith("'") else '""') + '\n' * t.count('\n')
    return _LEX.sub(repl, text)


STRIP_CASES = [   # (text, the stripped text: literals blanked)
    ('a // QString x\nb', 'a \nb'),
    ('a /* QString\n y */ b', 'a  \n b'),
    ('s = "// not a comment"; // gone', 's = ""; '),
    ('s = "QString"; QList', 's = ""; QList'),
    ("c = '\"'; d = 'Q'; // gone", "c = ''; d = ''; "),
    ('s = "a\\"// b"; x', 's = ""; x'),
    ('r = R"x(QString "q" )x"; y', 'r = ""; y'),
    ('r = R"(a\nQMap)"; z', 'r = ""\n; z'),
    ('/* a // b */ c', '  c'),
    ('// a /* b\nc */ d', '\nc */ d'),
]


def sites_of(text, kind):
    """(tokens, includes) of one file's text: lists of (line, what), comments and literals blanked."""
    tok_re, inc_re = KINDS[kind]
    s = strip(text)
    nl = [i for i, c in enumerate(s) if c == '\n']
    def line(pos):
        return bisect.bisect_left(nl, pos) + 1
    return ([(line(m.start()), m.group(0).strip()) for m in tok_re.finditer(s)],
            [(line(m.start()), m.group(0).strip()) for m in inc_re.finditer(s)])


# ---- the table -------------------------------------------------------------------------------------------------------
def read_table(path=TABLE):
    """[(line, kind, tokens ceiling, includes ceiling, [paths])] from the ceilings file (| columns, # comments)."""
    rows = []
    for n, raw in enumerate(open(path, encoding='utf-8'), 1):
        raw = raw.split('#', 1)[0].strip()
        if not raw:
            continue
        cols = [c.strip() for c in raw.split('|')]
        if len(cols) < 5 or cols[1] not in KINDS:
            raise SystemExit('FAIL %s:%d: a line is  name | kind (qt|qtsql) | tokens | includes | paths ... : %r'
                             % (path, n, raw))
        rows.append((cols[0], cols[1], int(cols[2]), int(cols[3]), cols[4].split()))
    return rows


def owner(rel, kind, table):
    """the table line of `kind` holding the file at `rel` (the longest matching path), or None"""
    best, blen = None, -1
    for row in table:
        if row[1] != kind:
            continue
        for p in row[4]:
            if (rel == p or rel.startswith(p.rstrip('/') + '/')) and len(p) > blen:
                best, blen = row, len(p)
    return best


def read_tree(root):
    """{relative path: text} of every C++ file under the universes' roots (vendored trees skipped)"""
    files = {}
    for top in sorted(set(UNIVERSE.values())):
        for r, ds, fs in os.walk(os.path.join(root, top)):
            ds[:] = sorted(d for d in ds if d not in VENDORED)
            for f in sorted(fs):
                if f.endswith(CPP):
                    p = os.path.join(r, f)
                    files[os.path.relpath(p, root).replace(os.sep, '/')] = open(p, encoding='utf-8',
                                                                              errors='replace').read()
    return files


def census(files, table):
    """{(line, kind): ([token sites], [include sites], file count)}; the unheld files' sites under (None, kind)"""
    got = {(row[0], row[1]): ([], [], 0) for row in table}
    for kind, top in UNIVERSE.items():
        got[(None, kind)] = ([], [], 0)
    for rel, text in files.items():
        for kind, top in UNIVERSE.items():
            if not rel.startswith(top + '/'):
                continue
            row = owner(rel, kind, table)
            key = (row[0], kind) if row else (None, kind)
            toks, incs, n = got[key]
            t, i = sites_of(text, kind)
            toks += [(rel, ln, w) for ln, w in t]
            incs += [(rel, ln, w) for ln, w in i]
            got[key] = (toks, incs, n + 1)
    return got


def judge(got, table):
    """the RED lines of the law, both ways, over the census"""
    red = []
    for name, kind, ceil_t, ceil_i, paths in table:
        toks, incs, nfiles = got[(name, kind)]
        if nfiles == 0:
            red.append('RED  %s [%s]: its paths (%s) hold no C++ file - the directory is gone: delete the line'
                       % (name, kind, ' '.join(paths)))
            continue
        for what, sites, ceiling in (('tokens', toks, ceil_t), ('includes', incs, ceil_i)):
            if len(sites) > ceiling:
                red.append('RED  %s [%s] %s: ceiling %d, found %d - every site:' % (name, kind, what, ceiling, len(sites)))
                red += ['  %s:%d  %s' % s for s in sites]
            elif len(sites) < ceiling:
                red.append('RED  %s [%s] %s: ceiling %d, found %d - lower the ceiling to %d in the same commit (%s)'
                           % (name, kind, what, ceiling, len(sites), len(sites), os.path.basename(TABLE)))
    for kind, top in UNIVERSE.items():
        toks, incs, _ = got[(None, kind)]
        for rel, ln, w in sorted(toks + incs):
            red.append('RED  %s:%d  %s - [%s] under %s/ in no line of the table: held at 0' % (rel, ln, w, kind, top))
    return red


def qtstd_includers(files):
    """[(rel, line, include)] of the files under irisgl/ that include the app's qtstd.h"""
    out = []
    for rel, text in files.items():
        if not rel.startswith('irisgl/'):
            continue
        s = strip(text, keep_literals=True)
        for m in ANY_INCLUDE.finditer(s):
            if QTSTD.search(m.group(2).strip()):
                out.append((rel, s.count('\n', 0, m.start()) + 1, m.group(0).strip()))
    return out


# ---- THE Qt-FREE TARGET (T1-T3) ------------------------------------------------------------------------------------
TARGET = 'IrisCore'
QT_ARG = re.compile(r'/[qQ]t[56]?(?:/|$)|/Qt[A-Z][A-Za-z0-9]*(?:/|$)|/Qt/\d|_autogen(?:/|$)|^-DQT_|^-DQ_')
QT_SYMBOL = re.compile(r'\bQ[A-Z][a-z]\w*|\bQ_[A-Z_]+|\bQT_\w+|\bqt_\w+')
DEFINED = re.compile(r'^\s*(?:[0-9a-fA-F]+\s+)?[TtDdBbRrWwVvGgSs]\s+\S')
STD_HEADERS = set('''algorithm any array atomic barrier bit bitset cassert cctype cerrno cfenv cfloat charconv chrono cinttypes
climits clocale cmath codecvt compare complex concepts condition_variable coroutine csetjmp csignal cstdarg cstddef cstdint
cstdio cstdlib cstring ctime cuchar cwchar cwctype deque exception execution expected filesystem format forward_list fstream
functional future initializer_list iomanip ios iosfwd iostream istream iterator latch limits list locale map memory
memory_resource mutex new numbers numeric optional ostream queue random ranges ratio regex scoped_allocator semaphore set
shared_mutex source_location span sstream stack stdexcept stop_token streambuf string string_view syncstream system_error
thread tuple type_traits typeindex typeinfo unordered_map unordered_set utility valarray variant vector version'''.split())
# the platform's own C headers a core source may need (none yet: stage 1's files.cpp names its POSIX headers here)
SYSTEM_HEADERS = set()


def ninja_target(text, target=TARGET):
    """([(source, {var: value})], archive path or None) of `target`'s compile blocks and static link line in build.ninja"""
    blocks, archive, cur = [], None, None
    rule_c = re.compile(r'^build ([^:]+?\.o(?:bj)?): CXX_COMPILER__%s_\S*\s+(\S+)' % re.escape(target))
    rule_a = re.compile(r'^build ([^:]+?): CXX_STATIC_LIBRARY_LINKER__%s_' % re.escape(target))
    for line in text.split('\n'):
        m = rule_c.match(line)
        if m:
            cur = {}
            blocks.append((m.group(2), cur))
            continue
        m = rule_a.match(line)
        if m:
            archive, cur = m.group(1), None
            continue
        if cur is not None and line.startswith('  ') and ' =' in line:
            k, _, v = line.strip().partition(' =')
            cur[k] = v.strip()
        elif not line.startswith('  '):
            cur = None
    return blocks, archive


def t1_bad(blocks):
    """[(source, arg)] of the Qt arguments on the target's compile lines"""
    bad = []
    for src, var in blocks:
        args = ' '.join(var.get(k, '') for k in ('DEFINES', 'INCLUDES', 'FLAGS')).split()
        bad += [(src, a) for a in args if QT_ARG.search(a)]
    return bad


def t2_read(nm_text):
    """(defined symbol count, [Qt symbol lines]) of `nm -C` text"""
    lines = [l for l in nm_text.split('\n') if l.strip() and not l.endswith(':')]
    return sum(1 for l in lines if DEFINED.match(l)), [l.strip() for l in lines if QT_SYMBOL.search(l)]


def t3_bad(sources, core_root, read):
    """[(file, line, include, why)] of the includes, transitively from `sources`, that leave core/ and the standard
    library; `read(path)` gives a file's text or None; quoted and core/ angle includes resolve against the including
    file's directory, then the IrisCore include root (the parent of core_root)"""
    bad, seen, todo = [], set(), list(sources)
    inc_root = os.path.dirname(core_root)
    while todo:
        path = todo.pop()
        if path in seen:
            continue
        seen.add(path)
        text = read(path)
        if text is None:
            bad.append((path, 0, '', 'not readable'))
            continue
        s = strip(text, keep_literals=True)
        for m in ANY_INCLUDE.finditer(s):
            ln, name = s.count('\n', 0, m.start()) + 1, m.group(2).strip()
            if m.group(1) == '<' and not name.startswith('core/'):
                if name not in STD_HEADERS and name not in SYSTEM_HEADERS:
                    bad.append((path, ln, '<%s>' % name, 'not the standard library'))
                continue
            hits = [p for p in (os.path.normpath(os.path.join(os.path.dirname(path), name)),
                                os.path.normpath(os.path.join(inc_root, name))) if read(p) is not None]
            if not hits:
                bad.append((path, ln, name, 'resolves to no file'))
            elif not hits[0].startswith(core_root.rstrip('/') + '/'):
                bad.append((path, ln, name, 'outside core/ (%s)' % hits[0]))
            else:
                todo.append(hits[0])
    return bad


def check_target(root, build):
    """the RED lines of T1-T3, and the controls' counts"""
    red = []
    ninja = os.path.join(build, 'build.ninja')
    if not os.path.isfile(ninja):
        return ['RED  T1: no %s - the row reads a configured tree' % ninja], 0, 0
    blocks, archive = ninja_target(open(ninja, encoding='utf-8', errors='replace').read())
    if not blocks:
        red.append('RED  T1: build.ninja holds no compile line of %s (the target is gone, or renamed)' % TARGET)
    for src, a in t1_bad(blocks):
        red.append('RED  T1: %s compiles %s with %s - a Qt include directory, define or generator on IrisCore'
                   % (TARGET, src, a))
    ndef = 0
    apath = os.path.join(build, archive) if archive else None
    if not apath or not os.path.isfile(apath):
        red.append('RED  T2: %s\'s archive (%s) is not built - build the tree first' % (TARGET, archive))
    else:
        nm = subprocess.run(['nm', '-C', apath], capture_output=True, text=True)
        ndef, qt = t2_read(nm.stdout)
        if nm.returncode != 0 or ndef == 0:
            red.append('RED  T2: nm read no defined symbol from %s (rc %d): the control reads nothing' % (apath, nm.returncode))
        red += ['RED  T2: %s holds a Qt symbol: %s' % (apath, q) for q in qt]
    core = os.path.join(root, 'irisgl', 'core')

    def read(p):
        try:
            return open(p, encoding='utf-8', errors='replace').read()
        except OSError:
            return None
    srcs = [s if os.path.isabs(s) else os.path.normpath(os.path.join(build, s)) for s, _ in blocks]
    red += ['RED  T3: %s:%d includes %s - %s' % b for b in t3_bad(srcs, core, read)]
    return red, len(blocks), ndef


# ---- the self-test ---------------------------------------------------------------------------------------------------
NINJA_FIXTURE = '''build irisgl/core/CMakeFiles/IrisCore.dir/geometry/plane.cpp.o: CXX_COMPILER__IrisCore_unscanned_Debug /s/irisgl/core/geometry/plane.cpp || x
  DEFINES = %s
  FLAGS = -g -fPIC -std=gnu++17
  INCLUDES = -I/s/build/generated -I/s/irisgl %s
build irisgl/CMakeFiles/IrisGL.dir/x.cpp.o: CXX_COMPILER__IrisGL_unscanned_Debug /s/irisgl/x.cpp
  INCLUDES = -isystem /usr/include/x86_64-linux-gnu/qt6/QtCore
build irisgl/core/libIrisCored.a: CXX_STATIC_LIBRARY_LINKER__IrisCore_Debug irisgl/core/CMakeFiles/IrisCore.dir/geometry/plane.cpp.o
'''


def self_test():
    bad = []
    for text, want in STRIP_CASES:
        if strip(text) != want:
            bad.append('the stripper: %r gave %r, not %r' % (text, strip(text), want))
    # every token and include form counts once, its commented and quoted copies not at all
    forms = ['QString a;', 'x = qBound (1, 2, 3);', 'Q_OBJECT', 'QT_BEGIN_NAMESPACE', 'quint32 n;', 'qint64 n;',
             'qreal r;', 'public slots:', 'signals :', 'QSqlQuery q;']
    for f in forms:
        t, _ = sites_of(f + '\n', 'qt')
        q, _ = sites_of('// ' + f + '\n/* ' + f + ' */ s = "' + f.replace('"', '') + '";\n', 'qt')
        if len(t) != 1 or q:
            bad.append('the token form %r counted %d (want 1), its commented/quoted copies %d (want 0)' % (f, len(t), len(q)))
    for f, kind, want in (('#include <QString>', 'qt', 1), ('  #  include <QtCore/QList>', 'qt', 1),
                          ('#include <qmath.h>', 'qt', 1), ('#include <vector>', 'qt', 0), ('#include "QFoo.h"', 'qt', 0),
                          ('// #include <QString>', 'qt', 0), ('#include <QSqlQuery>', 'qtsql', 1),
                          ('#include <QtSql/QSqlError>', 'qtsql', 1), ('#include <QString>', 'qtsql', 0)):
        _, i = sites_of(f + '\n', kind)
        if len(i) != want:
            bad.append('the include form %r [%s] counted %d, not %d' % (f, kind, len(i), want))
    t, _ = sites_of('QString s; QSqlDatabase d; // QSqlQuery\n', 'qtsql')
    if [w for _, w in t] != ['QSqlDatabase']:
        bad.append('the qtsql kind counted %r, not the one QSqlDatabase' % t)
    if sites_of('int a;\n\nQString b;\n', 'qt')[0] != [(3, 'QString')]:
        bad.append('a site\'s line number is wrong: %r' % (sites_of('int a;\n\nQString b;\n', 'qt')[0],))
    # the law both ways, over a synthetic tree and table
    table = [('irisgl/core', 'qt', 3, 1, ['irisgl/core']), ('irisgl/document', 'qt', 0, 0, ['irisgl/document', 'irisgl/f.h']),
             ('src/data', 'qtsql', 1, 0, ['src/data'])]
    tree = {'irisgl/core/a.h': '#include <QString>\nQString a();\nQList<int> b;\n', 'irisgl/document/d.cpp': 'int d;\n',
            'irisgl/f.h': '// QString\n', 'src/data/x.cpp': 'QString s; QSqlQuery q;\n', 'src/ui/y.cpp': 'QString s;\n'}
    if judge(census(tree, table), table):
        bad.append('the synthetic tree at its ceilings red: %r' % judge(census(tree, table), table)[:3])
    over = dict(tree, **{'irisgl/core/b.cpp': 'void f() { qDebug() << 1; }\n'})
    red = judge(census(over, table), table)
    if not red or 'ceiling 3, found 4' not in red[0] or not any('irisgl/core/b.cpp:1  qDebug(' in r for r in red):
        bad.append('one token over the ceiling did not red with its site: %r' % red[:4])
    under = dict(tree, **{'irisgl/core/a.h': '#include <QString>\nQString a();\n'})
    red = judge(census(under, table), table)
    if len(red) != 1 or 'lower the ceiling to 2' not in red[0]:
        bad.append('one token under the ceiling did not red "lower the ceiling to 2": %r' % red)
    held0 = dict(tree, **{'irisgl/f.h': 'QString f();\n'})
    red = judge(census(held0, table), table)
    if not red or 'ceiling 0, found 1' not in red[0]:
        bad.append('a token in a line at 0 (a second path of a line) did not red: %r' % red)
    unheld = dict(tree, **{'irisgl/newdir/n.cpp': '#include <QMap>\n'})
    red = judge(census(unheld, table), table)
    if len(red) != 2 or not all('irisgl/newdir/n.cpp:1' in r and 'held at 0' in r for r in red):
        bad.append('Qt in an irisgl directory with no line did not red as held at 0: %r' % red)
    if judge(census(dict(tree, **{'src/ui/z.cpp': 'QString s; QWidget w;\n'}), table), table):
        bad.append('general Qt in an unheld src/ directory redded (only QtSql is held there)')
    if not judge(census(dict(tree, **{'src/ui/z.cpp': 'QSqlQuery q;\n'}), table), table):
        bad.append('QtSql in a src/ directory with no line did not red')
    gone = {k: v for k, v in tree.items() if not k.startswith('src/data/')}
    if not any('delete the line' in r for r in judge(census(gone, table), table)):
        bad.append('a line whose directory is gone did not red')
    # the qtstd.h rule
    inc = qtstd_includers({'irisgl/document/a.cpp': '#include "app/qtstd.h"\n', 'irisgl/core/b.h': '// #include "app/qtstd.h"\n',
                           'src/services/c.cpp': '#include "app/qtstd.h"\n', 'irisgl/import/d.cpp': '#include "qtstdx.h"\n'})
    if [r for r, _, _ in inc] != ['irisgl/document/a.cpp']:
        bad.append('the qtstd.h rule found %r, not irisgl/document/a.cpp alone' % inc)
    # T1: the clean fixture passes, a qt6 -isystem, a QT_ define and an autogen directory each red; IrisGL's own Qt line never
    clean, archive = ninja_target(NINJA_FIXTURE % ('-DIRIS=1', ''))
    if len(clean) != 1 or t1_bad(clean) or archive != 'irisgl/core/libIrisCored.a':
        bad.append('T1 read the clean fixture wrong: %r %r %r' % (clean, t1_bad(clean), archive))
    for d, i in (('', '-isystem /usr/include/x86_64-linux-gnu/qt6/QtCore'), ('', '-isystem /usr/include/x86_64-linux-gnu/qt6'),
                 ('', '-I/opt/Qt/6.10.0/gcc_64/include'), ('-DQT_CORE_LIB', ''),
                 ('', '-I/s/build/irisgl/core/IrisCore_autogen/include')):
        if len(t1_bad(ninja_target(NINJA_FIXTURE % (d, i))[0])) != 1:
            bad.append('T1 did not red on %r %r' % (d, i))
    # T2: a Qt symbol, defined or needed, reds; the defined count is the control
    nm = ('\nplane.cpp.o:\n0000000000000000 T iris::Plane::Plane()\n                 U memcpy\n'
          '0000000000000010 W iris::Vec3::x() const\n')
    if t2_read(nm) != (2, []):
        bad.append('T2 read the clean nm text as %r' % (t2_read(nm),))
    if len(t2_read(nm + '                 U QString::QString(char const*)\n')[1]) != 1 or \
            len(t2_read(nm + '0000000000000020 T iris::f(QList<int> const&)\n')[1]) != 1:
        bad.append('T2 did not red a needed or a defined Qt symbol')
    # T3: core/ and the standard library pass; a Qt header, a header outside core/, a missing header each red
    fs = {'/r/irisgl/core/a.cpp': '#include "core/b.h"\n#include <vector>\n', '/r/irisgl/core/b.h': '#include "c.h"\n',
          '/r/irisgl/core/c.h': '#include <cmath>\n', '/r/irisgl/document/d.h': '',
          '/r/irisgl/core/e.cpp': '#include <QString>\n#include "document/d.h"\n#include "core/none.h"\n// #include <QList>\n'}
    rd = fs.get
    if t3_bad(['/r/irisgl/core/a.cpp'], '/r/irisgl/core', rd):
        bad.append('T3 redded a clean source: %r' % t3_bad(['/r/irisgl/core/a.cpp'], '/r/irisgl/core', rd))
    got = t3_bad(['/r/irisgl/core/e.cpp'], '/r/irisgl/core', rd)
    if [g[2] for g in got] != ['<QString>', 'document/d.h', 'core/none.h']:
        bad.append('T3 did not red the Qt, the outside and the missing include: %r' % got)
    fs['/r/irisgl/core/c.h'] = '#include <QMap>\n'
    if [g[2] for g in t3_bad(['/r/irisgl/core/a.cpp'], '/r/irisgl/core', rd)] != ['<QMap>']:
        bad.append('T3 did not follow a core header to its Qt include')
    return bad


def main():
    if len(sys.argv) < 3:
        print('usage: no_qt_below.py <source-root> <build-dir> [--counts]')
        return 2
    root, build = sys.argv[1], sys.argv[2]
    if not os.path.isdir(os.path.join(root, 'irisgl', 'core')):
        print('FAIL: no %s/irisgl/core - the row reads the tree root (the irisgl submodule checked out)' % root)
        print('%s: FAILED (1)' % ROW)
        return 1
    table = read_table()
    files = read_tree(root)
    got = census(files, table)
    if '--counts' in sys.argv:
        for name, kind, ct, ci, _ in table:
            toks, incs, n = got[(name, kind)]
            print('%-22s %-6s %5d files  %6d tokens (ceiling %d)  %4d includes (ceiling %d)'
                  % (name, kind, n, len(toks), ct, len(incs), ci))
        for kind, top in UNIVERSE.items():
            toks, incs, n = got[(None, kind)]
            print('%-22s %-6s %5d files  %6d tokens  %4d includes   (in no line: held at 0)'
                  % ('(%s/ unheld)' % top, kind, n, len(toks), len(incs)))
        return 0
    bad = self_test()
    for b in bad:
        print('FAIL the self-test: ' + b)
    if bad:
        print('%s: FAILED (%d)' % (ROW, len(bad)))
        return 1
    print('ok   the self-test: the stripper (%d cases), the token and include forms, the law both ways, an unheld file, '
          'a line gone, the qtstd.h rule, T1-T3 on fixtures' % len(STRIP_CASES))
    red = judge(got, table)
    red += ['RED  %s:%d  %s - src/app/qtstd.h is the app\'s; nothing below the editor includes it' % s
            for s in qtstd_includers(files)]
    tred, nblocks, ndef = check_target(root, build)
    red += tred
    # the non-zero control, every run: each line prints what it read (a line with a ceiling above 0 that read 0 reds above)
    for name, kind, ct, ci, _ in table:
        toks, incs, n = got[(name, kind)]
        print('line %-22s [%s] %4d files  %5d tokens / %d  %4d includes / %d' % (name, kind, n, len(toks), ct, len(incs), ci))
    print('%s: %d compile line(s) read, %d defined symbols in its archive' % (TARGET, nblocks, ndef))
    for r in red:
        print(r)
    nred = sum(1 for r in red if r.startswith('RED'))
    print('%d files read, %d lines held' % (len(files), len(table)))
    print('%s: FAILED (%d)' % (ROW, nred) if nred else '%s: PASSED' % ROW)
    return 1 if nred else 0


if __name__ == '__main__':
    sys.exit(main())
