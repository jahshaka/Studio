"""gate_graph — what a change CAN REACH, read from the build's own graph (MODULAR-GATE-1).

gate-scope.py's selectors, as a library (no CLI of its own):

  NinjaGraph   the build dir's ninja graph — every build edge of build.ninja plus the header
               dependencies ninja recorded for every object (`ninja -t deps`). `reach(path)` is
               ninja's own question turned into an answer: which EXECUTABLES would relink (or,
               for a shared library, load new code) if this file's bytes changed. A compiled
               suite runs iff one of its executables is in that set.
  header_identifiers
               the identifiers a C/C++ hunk touches — the names declared or used on the changed
               lines and the named scope (struct / class / enum / function) each changed line sits
               in — with comments and whitespace stripped first, so a comment-only hunk touches
               nothing.
  cmake_reach  what an edit to a CMake file names: the ctest rows, the build targets, the source
               files, the sub-directories, the helper functions and the variables its changed
               commands carry; a directory-scope setting reaches every target under that directory.

Nothing here is a list a human maintains: the graph, the deps log, the ctest inventory and the
diff are read fresh on every run (the parsed graph is cached in the build dir, keyed on the
mtimes of build.ninja and .ninja_deps).
"""
import collections
import os
import pickle
import re
import subprocess
import sys

CXX_EXT = (".cpp", ".cc", ".cxx", ".c", ".h", ".hh", ".hpp", ".hxx", ".inl", ".mm", ".ipp")
HEADER_EXT = (".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp")
_CACHE_VERSION = 5
# the nm cache's own version: bumped whenever what a cached entry means changes (2: the refusal
# of H1 — no entry may hold an nm failure's empty tables)
SYMS_VERSION = 2


def _split_ninja(s):
    """Split a ninja path list honouring `$ ` (escaped space), `$:` and `$$`."""
    out, cur, i = [], [], 0
    while i < len(s):
        c = s[i]
        if c == "$" and i + 1 < len(s):
            n = s[i + 1]
            if n in " :$":
                cur.append(n); i += 2; continue
        if c == " ":
            if cur: out.append("".join(cur)); cur = []
            i += 1; continue
        cur.append(c); i += 1
    if cur: out.append("".join(cur))
    return out


def _find_unescaped(s, token):
    """Index of `token` in s that is not preceded by a `$` escape, or -1."""
    i = 0
    while True:
        j = s.find(token, i)
        if j < 0: return -1
        # count the `$` characters right before j: an odd count escapes it
        k, d = j - 1, 0
        while k >= 0 and s[k] == "$": d += 1; k -= 1
        if d % 2 == 0: return j
        i = j + 1


class GraphError(RuntimeError):
    """The build graph cannot be read honestly: the selector refuses rather than under-select."""


def require_nm():
    import shutil
    if shutil.which("nm") is None:
        raise GraphError("`nm` (binutils) is not on PATH: the link graph cannot be read")


class NinjaGraph:
    """The build graph of one configured, BUILT build dir.

    rev[node]  -> the build outputs that list `node` as an explicit or implicit input, or (from the
                  deps log) the objects whose compile read `node` as a header. Order-only inputs
                  are ignored: they never make ninja rebuild anything.
    exes       -> every executable a link edge produces.
    app_driving-> executables compiled with a JAHSHAKA_BINARY define: the harnesses that spawn
                  bin/Jahshaka (open.responsive, avatar.responsive, ...). Their rows reach the app.
    """

    def __init__(self, build):
        self.build = os.path.abspath(build)
        self.rev = collections.defaultdict(set)
        self.exes = set()
        self.kind = {}              # output -> "exe" | "shared" | "static" | "object" | "other"
        self.app_driving = set()
        self.deps_objects = 0
        self.targets = {}           # cmake target name -> artefact (abs)
        self._memo = {}
        self.link_inputs = {}       # artefact -> its link edge's inputs (objects, archives, libs)
        self.obj_src = {}           # object -> the source its compile edge compiles
        self._syms = None           # lazily: object/exe -> (defined, undefined) symbol sets
        self._closure = {}

    # -- loading ---------------------------------------------------------------------------
    @classmethod
    def load(cls, build):
        build = os.path.abspath(build)
        bn = os.path.join(build, "build.ninja")
        dl = os.path.join(build, ".ninja_deps")
        if not os.path.isfile(bn):
            return None
        key = (_CACHE_VERSION, os.path.getmtime(bn), os.path.getmtime(dl) if os.path.exists(dl) else 0)
        cache = os.path.join(build, ".gate-scope-graph.pickle")
        try:
            with open(cache, "rb") as f:
                k, g = pickle.load(f)
            if k == key:
                g._load_syms()
                return g
        except Exception:
            pass
        g = cls(build)
        g._parse_build_ninja(bn)
        g._parse_deps()
        g._memo = {}
        g._syms = None
        try:
            tmp = cache + ".tmp"
            with open(tmp, "wb") as f:
                pickle.dump((key, g), f, protocol=pickle.HIGHEST_PROTOCOL)
            os.replace(tmp, cache)
        except OSError:
            pass
        g._load_syms()
        return g

    # the nm answers persist across runs (keyed per file on its mtime): a lane's second selection
    # pays only for the objects its build rewrote
    def _load_syms(self):
        self._syms_dirty = False
        self._libdefs = None
        self._members_all = None
        self._maps = None
        # VERSIONED, and an empty member answer never survives a load (the second Fable read, (c)):
        # a cache written by a tool that could cache nm's failure as empty tables is discarded
        self._syms = {}
        try:
            with open(os.path.join(self.build, ".gate-scope-syms.pickle"), "rb") as f:
                data = pickle.load(f)
            if isinstance(data, dict) and data.get("version") == SYMS_VERSION:
                self._syms = {k: v for k, v in data["syms"].items()
                              if not (k[1] is False and v[1][0] == frozenset() and v[1][1] == frozenset()
                                      and k[0] in self._member_set())}
        except Exception:
            self._syms = {}

    def save_syms(self):
        if not getattr(self, "_syms_dirty", False) or self._syms is None: return
        p = os.path.join(self.build, ".gate-scope-syms.pickle")
        try:
            with open(p + ".tmp", "wb") as f:
                pickle.dump({"version": SYMS_VERSION, "syms": self._syms}, f, protocol=pickle.HIGHEST_PROTOCOL)
            os.replace(p + ".tmp", p)
            self._syms_dirty = False
        except OSError:
            pass

    def _abs(self, p):
        return os.path.normpath(p if os.path.isabs(p) else os.path.join(self.build, p))

    def _parse_build_ninja(self, path):
        lines = open(path, errors="replace").read().replace("$\n", "").split("\n")
        cur_out = None
        for line in lines:
            if line.startswith("build "):
                body = line[6:]
                c = _find_unescaped(body, ":")
                if c < 0:
                    cur_out = None; continue
                outs_s, rest = body[:c], body[c + 1:]
                bar = _find_unescaped(outs_s, "|")
                outs = _split_ninja(outs_s[:bar] if bar >= 0 else outs_s)
                imp_outs = _split_ninja(outs_s[bar + 1:]) if bar >= 0 else []
                oo = _find_unescaped(rest, "||")
                if oo >= 0: rest = rest[:oo]
                ib = _find_unescaped(rest, "|")
                expl = _split_ninja(rest[:ib] if ib >= 0 else rest)
                impl = _split_ninja(rest[ib + 1:]) if ib >= 0 else []
                if not expl:
                    cur_out = None; continue
                rule, ins = expl[0], expl[1:] + impl
                all_outs = [self._abs(o) for o in outs + imp_outs]
                if "EXECUTABLE_LINKER" in rule: k = "exe"
                elif "SHARED_LIBRARY_LINKER" in rule or "MODULE_LIBRARY_LINKER" in rule: k = "shared"
                elif "STATIC_LIBRARY_LINKER" in rule: k = "static"
                elif "_COMPILER_" in rule: k = "object"
                elif rule == "phony": k = "phony"
                else: k = "other"
                for o in all_outs:
                    self.kind.setdefault(o, k)
                    if k == "exe": self.exes.add(o)
                if rule == "phony" and len(outs) == 1 and len(ins) == 1 and "/" not in outs[0]:
                    # `build <target>: phony <artefact>` — CMake's name for the artefact
                    self.targets[outs[0]] = self._abs(ins[0])
                src = [self._abs(i) for i in ins]
                if k in ("exe", "shared", "static") and all_outs:
                    self.link_inputs[all_outs[0]] = [self._abs(i) for i in expl[1:] + impl]
                elif k == "object" and all_outs and len(expl) > 1:
                    self.obj_src[all_outs[0]] = self._abs(expl[1])
                for i in src:
                    for o in all_outs:
                        self.rev[i].add(o)
                cur_out = all_outs[0] if all_outs else None
                cur_kind = k
            elif line.startswith("  DEFINES") and cur_out and cur_kind == "object":
                if "JAHSHAKA_BINARY=" in line:
                    self.app_driving.add(cur_out)      # an object; resolved to its exes below
            elif line and not line.startswith(" "):
                cur_out = None
        # objects compiled with JAHSHAKA_BINARY -> the executables they link into
        objs = set(self.app_driving)
        self.app_driving = set()
        for o in objs:
            self.app_driving |= self._forward_exes(o)

    def _parse_deps(self):
        try:
            r = subprocess.run(["ninja", "-C", self.build, "-t", "deps"], capture_output=True, text=True)
        except OSError:
            return
        if r.returncode != 0:
            return
        cur = None
        for line in r.stdout.splitlines():
            if not line:
                cur = None; continue
            if not line.startswith(" "):
                cur = self._abs(line.split(":", 1)[0])
                self.deps_objects += 1
                continue
            if cur:
                self.rev[self._abs(line.strip())].add(cur)

    # -- queries ---------------------------------------------------------------------------
    def _forward_exes(self, node):
        seen, stack, exes = {node}, [node], set()
        while stack:
            n = stack.pop()
            for o in self.rev.get(n, ()):
                if o in seen: continue
                seen.add(o)
                if o in self.exes: exes.add(o)
                stack.append(o)
        return exes

    def known(self, path_abs):
        """True when the graph has the file as an input (a compiled source, a header some object
        read, a custom command's input)."""
        return path_abs in self.rev

    def reach(self, path_abs):
        """The executables whose bytes (or whose shared libraries' bytes) depend on this file."""
        if path_abs not in self._memo:
            self._memo[path_abs] = frozenset(self._forward_exes(path_abs))
        return self._memo[path_abs]

    def includers(self, header_abs):
        """The objects whose compile read this header (the deps log)."""
        return {o for o in self.rev.get(header_abs, ()) if self.kind.get(o) == "object"}

    # -- the link graph inside a shared library ---------------------------------------------
    # A shared library is loaded whole, but its code runs only where something reaches it: the
    # executable's own references (its dynamic undefined symbols), the library's static
    # initialisers, and from there every symbol one object of the library takes from another.
    # That is the linker's own reference graph, read with `nm` — so an object of libIrisGL that
    # no reference chain from a test executable reaches (the mesh bake, for a document test)
    # cannot change what that executable does. Measured on the tree this landed on: the bake's
    # object is in 24 of the 154 executables that load libIrisGL.

    def libdefs(self):
        """Every symbol a member of one of the tree's own libraries defines (the only names a
        reference needs to be kept for: a Qt or Ogre symbol links nothing of ours)."""
        if getattr(self, "_libdefs", None) is None:
            libs = [a for a, k in self.kind.items() if k in ("shared", "static") and a in self.link_inputs]
            ds = set()
            for L in libs:
                for o in self.members(L):
                    ds |= self._nm(o, member=True)[0]
            if not ds:
                raise GraphError("the tree's libraries define no symbol by `nm`: the link graph is unreadable")
            self._libdefs = ds
        return self._libdefs

    def _nm(self, path, dynamic=False, member=None):
        """(defined, undefined, has_static_init) for an object or (dynamic=True) an executable.
        Kept small on purpose (a Debug tree's full symbol tables pickled to 380 MB): names are
        interned, a library member keeps its definitions, and every other file keeps only the
        references that some library of ours can answer."""
        if self._syms is None: self._syms = {}
        if member is None:
            member = path in self._member_set()
        try:
            mt = os.path.getmtime(path)
        except OSError:
            return set(), set(), False
        k = (path, dynamic)
        c = self._syms.get(k)
        if c and c[0] == mt: return c[1]
        args = ["nm", "-P", "--no-sort"] + (["-D"] if dynamic else []) + [path]
        # NO nm, NO ANSWER (the Fable read, H1): an empty symbol table would give the link graph no
        # roots, an engine change would reach no compiled row — silently, and cached. Refuse.
        try:
            r = subprocess.run(args, capture_output=True, text=True)
        except OSError as e:
            raise GraphError(f"`nm` could not run ({e}): the link graph cannot be read")
        if r.returncode != 0:
            raise GraphError(f"`nm` failed on {path} ({r.returncode}): {r.stderr.strip()[:200]}")
        out = r.stdout
        d, u, init = set(), set(), False
        intern = sys.intern
        for line in out.splitlines():
            f = line.split()
            if len(f) < 2: continue
            name, t = f[0], f[1]
            if t == "U": u.add(intern(name))
            elif member and t in "TDBRVWuiv": d.add(intern(name))
            elif t == "t" and name.startswith("_GLOBAL__sub_I_"): init = True
        if not member:
            u &= self.libdefs()
        v = (frozenset(d), frozenset(u), init)
        if member and not d and not u:
            return v                       # never cache an empty member: re-read it next time
        self._syms[k] = (mt, v)
        self._syms_dirty = True
        return v

    def _member_set(self):
        if getattr(self, "_members_all", None) is None:
            ms = set()
            for a, k in self.kind.items():
                if k in ("shared", "static") and a in self.link_inputs:
                    ms |= self.members(a)
            self._members_all = ms
        return self._members_all

    def members(self, lib):
        """The objects a library is linked from, archive members included (an archive linked into
        a shared library contributes the members something references — the closure decides)."""
        out, stack, seen = set(), [lib], set()
        while stack:
            a = stack.pop()
            if a in seen: continue
            seen.add(a)
            for i in self.link_inputs.get(a, ()):
                if self.kind.get(i) == "object": out.add(i)
                elif self.kind.get(i) == "static": stack.append(i)
        return out

    def _lib_maps(self, lib):
        """symbol -> defining members, member -> its references, members with static init."""
        if not hasattr(self, "_maps") or self._maps is None: self._maps = {}
        if lib not in self._maps:
            defs, und, inits = collections.defaultdict(set), {}, set()
            for o in self.members(lib):
                d, u, init = self._nm(o, member=True)
                und[o] = u
                if init: inits.add(o)
                for s_ in d: defs[s_].add(o)
            self._maps[lib] = (defs, und, inits)
        return self._maps[lib]

    def closure(self, exe, lib):
        """The objects of `lib` that `exe` can reach through symbol references."""
        key = (exe, lib)
        if key in self._closure: return self._closure[key]
        mem = self.members(lib)
        defs, und, inits = self._lib_maps(lib)
        _, eu, _ = self._nm(exe, dynamic=True)
        direct = set(self.link_inputs.get(lib, ()))
        roots = {o for s_ in eu for o in defs.get(s_, ())} | {o for o in inits if o in direct}
        seen, stack = set(roots), list(roots)
        while stack:
            o = stack.pop()
            for s_ in und.get(o, ()):
                for d_ in defs.get(s_, ()):
                    if d_ not in seen: seen.add(d_); stack.append(d_)
        self._closure[key] = seen
        return seen

    def closure_static(self, exe, archive):
        """The members of `archive` the linker extracts into `exe`: reached by reference from the
        executable's own objects (and, conservatively, from every member of the other archives it
        links)."""
        key = ("static", exe, archive)
        if key in self._closure: return self._closure[key]
        defs, und, _ = self._lib_maps(archive)
        roots = set()
        for i in self.link_inputs.get(exe, ()):
            k = self.kind.get(i)
            srcs = [i] if k == "object" else (list(self.members(i)) if k == "static" and i != archive else [])
            for o in srcs:
                for s_ in self._nm(o)[1]:
                    roots |= defs.get(s_, set())
        seen, stack = set(roots), list(roots)
        while stack:
            o = stack.pop()
            for s_ in und.get(o, ()):
                for d_ in defs.get(s_, ()):
                    if d_ not in seen: seen.add(d_); stack.append(d_)
        self._closure[key] = seen
        return seen

    def reach_objects(self, objs):
        """Executables reached from these objects. Through a library, only where the link graph
        says the executable reaches the object: an archive member must be extracted (closure_
        static), a shared library's object must be referenced from the executable or from the
        library's static initialisers (closure) — an archive linked INTO a shared library is
        judged by that library's closure."""
        exes = set()
        for o in objs:
            seen, stack = {o}, [o]
            while stack:
                n = stack.pop()
                for x in self.rev.get(n, ()):
                    if x in seen: continue
                    seen.add(x)
                    if x in self.exes:
                        exes.add(x); continue
                    k = self.kind.get(x)
                    if k in ("static", "shared") and o in self.members(x):
                        for c in self.rev.get(x, ()):
                            if c in seen: continue
                            if c in self.exes:
                                inside = self.closure_static(c, x) if k == "static" else self.closure(c, x)
                                if o in inside: exes.add(c)
                            elif self.kind.get(c) == "shared" and o in self.members(c):
                                seen.add(c)
                                for e in self.rev.get(c, ()):
                                    if e in self.exes:
                                        if o in self.closure(e, c): exes.add(e)
                                    elif e not in seen:
                                        seen.add(e); stack.append(e)
                            else:
                                seen.add(c); stack.append(c)
                        continue
                    stack.append(x)
        return exes

    def reach_refined(self, path_abs):
        """reach(), with the shared-library link graph applied to the objects compiled from or
        including this file."""
        k = ("refined", path_abs)
        if k in self._memo: return self._memo[k]
        objs = {o for o in self.rev.get(path_abs, ()) if self.kind.get(o) == "object"}
        if not objs:
            r = self.reach(path_abs)
        else:
            r = frozenset(self.reach_objects(objs))
            # anything else the file feeds (a generated header, a custom command) stays unrefined
            others = {x for x in self.rev.get(path_abs, ()) if self.kind.get(x) != "object"}
            for x in others:
                r = r | self._forward_exes(x) | ({x} if x in self.exes else set())
        self._memo[k] = frozenset(r)
        return self._memo[k]

    def first_party_referrers(self, lib_static, vendored_prefixes):
        """The first-party objects that take a symbol from a (vendored) static library's members
        — the way its code enters ours."""
        mem = self.members(lib_static)
        dset = set()
        for o in mem: dset |= self._nm(o)[0]
        out = set()
        for consumer in (x for x in self.rev.get(lib_static, ()) if self.kind.get(x) in ("shared", "exe")):
            for o in self.members(consumer):
                src = self.obj_src.get(o, "")
                if not src or any(v in src for v in vendored_prefixes): continue
                if self._nm(o)[1] & dset: out.add(o)
        return out

    def target_reach(self, name):
        a = self.targets.get(name)
        if not a: return None
        s = set(self._forward_exes(a))
        if a in self.exes: s.add(a)
        return s


def ctest_inventory(build):
    """`ctest --show-only=json-v1` for a build dir, run in a COPY of its CTestTestfile.cmake tree.
    ctest opens <build>/Testing/Temporary/LastTest.log even for a listing, which TRUNCATES the log
    of a gate still running in that dir (the suites audit proved it, GATE_AND_RIG) — and the
    selector's own guards list the inventory from inside a gate. The copy answers the same
    (backtraces and commands are absolute paths) and leaves the build dir's log alone.
    Returns (stdout, returncode)."""
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp(prefix="gate-inventory-")
    try:
        for dp, _, fs in os.walk(build):
            if "CTestTestfile.cmake" in fs:
                rel = os.path.relpath(dp, build)
                os.makedirs(os.path.join(tmp, rel), exist_ok=True)
                shutil.copy2(os.path.join(dp, "CTestTestfile.cmake"), os.path.join(tmp, rel, "CTestTestfile.cmake"))
        r = subprocess.run(["ctest", "--show-only=json-v1"], cwd=tmp, capture_output=True, text=True)
        return r.stdout, r.returncode
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# -- C/C++ text ------------------------------------------------------------------------------
_CXX_KEYWORDS = set("""
alignas alignof and asm auto bool break case catch char char8_t char16_t char32_t class const
consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default
delete do double dynamic_cast else enum explicit export extern false float for friend goto if
inline int long mutable namespace new noexcept not nullptr operator or private protected public
register reinterpret_cast requires return short signed sizeof static static_assert static_cast
struct switch template this thread_local throw true try typedef typeid typename union unsigned
using virtual void volatile wchar_t while override final include define ifdef ifndef endif pragma
elif undef error defined once std size_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t
int64_t nullptr_t QString QStringList QVector QList QHash QMap QVariant QObject Q_OBJECT Q_INVOKABLE
Q_PROPERTY signals slots emit
""".split())
# Generic words a changed line carries that name nothing a family owns: an identifier is only a
# SELECTOR when it is specific enough. Names shorter than 4 characters are dropped as well.
_GENERIC = set("""
value values type name size data count index result other first second enabled mode kind flags
width height depth begin end item items node nodes list get set update reset clear init create
destroy apply valid state desc info text path file key keys id ids min max out args arg self
""".split())

_TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def strip_cxx(text):
    """Remove comments and collapse whitespace per line, keeping line structure. String and char
    literals are kept (a changed string is a code change)."""
    out, i, n = [], 0, len(text)
    buf = []
    state = None
    while i < n:
        c = text[i]
        if state is None:
            if c == "/" and i + 1 < n and text[i + 1] == "/":
                j = text.find("\n", i)
                i = n if j < 0 else j
                continue
            if c == "/" and i + 1 < n and text[i + 1] == "*":
                j = text.find("*/", i + 2)
                seg = text[i:(n if j < 0 else j + 2)]
                buf.append("\n" * seg.count("\n"))
                i = n if j < 0 else j + 2
                continue
            if c == '"' or c == "'":
                # a raw string R"x( ... )x" is rare in our tree; treat as a normal literal
                state = c
            buf.append(c); i += 1
            continue
        # inside a literal
        buf.append(c)
        if c == "\\" and i + 1 < n:
            buf.append(text[i + 1]); i += 2; continue
        if c == state or c == "\n":
            state = None
        i += 1
    for line in "".join(buf).split("\n"):
        out.append(" ".join(line.split()))
    return out


def _scopes(lines):
    """For every stripped line, the named scopes (struct/class/enum/union/function names) it sits
    in — innermost last. Namespaces are skipped (too broad to select anything)."""
    res, stack, header = [], [], []
    decl = re.compile(r"\b(?:struct|class|union|enum(?:\s+class|\s+struct)?)\s+(?:\w+\s+)*?([A-Za-z_]\w*)\s*(?:final\s*)?(?::[^{]*)?$")
    for ln in lines:
        res.append([s for s in stack if s])
        for ch in re.split(r"([{};])", ln):
            if ch == "{":
                h = " ".join(header).strip()
                name = None
                if not re.match(r"^(?:inline\s+)?namespace\b", h) and not h.startswith("extern \"C\""):
                    m = decl.search(h)
                    if m:
                        name = m.group(1)
                    else:
                        # a function: the identifier right before the first top-level '('
                        m = re.search(r"([A-Za-z_~][\w:~]*)\s*\(", h)
                        if m and m.group(1).split("::")[-1] not in _CXX_KEYWORDS:
                            name = m.group(1).split("::")[-1]
                stack.append(name)
                header = []
            elif ch == "}":
                if stack: stack.pop()
                header = []
            elif ch == ";":
                header = []
            elif ch:
                header.append(ch)
    return res


_DECL_FN = re.compile(r"([A-Za-z_~][\w]*)\s*\(")
_DECL_VAR = re.compile(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*(?:=[^;]*|\{[^;]*\})?\s*[;,]\s*$")
_DECL_TYPE = re.compile(r"\b(?:struct|class|union|enum(?:\s+class|\s+struct)?)\s+(?:\w+\s+)*?([A-Za-z_]\w*)")
_DECL_USING = re.compile(r"\busing\s+([A-Za-z_]\w*)\s*=")
_DECL_TYPEDEF = re.compile(r"\btypedef\b.*?([A-Za-z_]\w*)\s*;\s*$")
_NAMESPACE = re.compile(r"^(?:inline\s+)?namespace\b|^using\s+namespace\b|^extern\s+\"C\"")
_DECL_DEFINE = re.compile(r"^#\s*define\s+([A-Za-z_]\w*)")
_DECL_ENUMERATOR = re.compile(r"^([A-Za-z_]\w*)\s*(?:=\s*[^,]*)?,?$")


def declared_names(line, in_function):
    """(names, carries_scope): the names a stripped line DECLARES — a function, a member or
    variable, a type, a macro, an enumerator — and whether the enclosing named scope must select
    too. A member's or an enumerator's change moves its struct's layout or defaults for every
    user of the struct, so the struct selects; a method's declaration selects by the method.
    A line inside a function body declares nothing a family can name: its function selects."""
    if in_function: return set(), True
    if _NAMESPACE.match(line):
        return set(), False          # a namespace names every file in it: never a selector
    m = _DECL_DEFINE.match(line)
    if m: return {m.group(1)}, False
    m = _DECL_USING.search(line) or _DECL_TYPEDEF.search(line)
    if m: return {m.group(1)}, False
    out = set(m.group(1) for m in _DECL_TYPE.finditer(line))
    if out: return out, False
    m = _DECL_FN.search(line)
    if m and m.group(1) not in _CXX_KEYWORDS:
        return {m.group(1).lstrip("~")}, False
    m = _DECL_VAR.search(line)
    if m: return {m.group(1)}, True
    m = _DECL_ENUMERATOR.match(line)
    if m: return {m.group(1)}, True
    return set(), True


def _statement_starts(lines):
    """For every stripped line, the index of the line its statement began on: a declaration's
    parameter list continued over several lines belongs to the line that names the function."""
    res, start, depth = [], 0, 0
    for k, ln in enumerate(lines):
        if depth == 0 and not ln.strip():
            res.append(k); start = k + 1; continue
        res.append(start if depth > 0 else k)
        if depth == 0: start = k
        for ch in ln:
            if ch == "(": depth += 1
            elif ch == ")": depth = max(0, depth - 1)
        if depth == 0 and re.search(r"[;{}]\s*$", ln):
            start = k + 1
    return res


_ACCESS = re.compile(r"^(?:public|private|protected|signals|slots|Q_SIGNALS|Q_SLOTS|public slots|private slots|protected slots)\s*:$")


def hunks_of(diff_u0):
    """[(old_start, old_count, new_start, new_count)] of a -U0 diff (1-based starts)."""
    out = []
    for line in diff_u0.splitlines():
        m = re.match(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", line)
        if m:
            out.append((int(m.group(1)), int(m.group(2) or 1), int(m.group(3)), int(m.group(4) or 1)))
    return out


def cxx_changed_identifiers(old_text, new_text, hunks=None):
    """The identifiers a C/C++ change touches. Empty when the change is comments/whitespace only.

    Returns (identifiers, changed_line_count). The identifiers are what the changed lines
    DECLARE (a function, member, variable, type, macro or enumerator name) and, where the
    change is to what a struct's users get without naming anything (a member's type or
    default MODIFIED, a statement inside a function body), the named scope it sits in — never
    the names a line merely USES (a parameter's type, a call inside a body): selecting by those
    turned one header edit into a thousand files. A member or enumerator that is only ADDED or
    REMOVED selects by its own name: code that does not name it compiles to the same meaning.
    Keywords, common Qt/std words and generic member words are dropped.

    `hunks` are git's (-U0); without them the two texts are diffed here (difflib: slow on a
    5,000-line header, so gate-scope always passes git's)."""
    a = strip_cxx(old_text or "")
    b = strip_cxx(new_text or "")
    if hunks is None:
        import difflib
        ops = [(t, i1, i2, j1, j2) for t, i1, i2, j1, j2 in
               difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes() if t != "equal"]
    else:
        ops = []
        for os_, oc, ns, nc in hunks:
            i1 = os_ - 1 if oc else os_
            j1 = ns - 1 if nc else ns
            i2, j2 = i1 + oc, j1 + nc
            # a hunk whose code (comments and whitespace stripped) is unchanged changes nothing
            if [x for x in a[i1:i2] if x.strip()] == [x for x in b[j1:j2] if x.strip()]:
                continue
            ops.append(("replace" if oc and nc else ("insert" if nc else "delete"), i1, i2, j1, j2))
    sa, sb = _scopes(a), _scopes(b)
    fa, fb = _function_depth(a), _function_depth(b)
    ta, tb = _statement_starts(a), _statement_starts(b)
    ids, changed = set(), 0
    for tag, i1, i2, j1, j2 in ops:
        for lines, scopes, fdep, starts, lo, hi in ((a, sa, fa, ta, i1, i2), (b, sb, fb, tb, j1, j2)):
            for k in range(lo, min(hi, len(lines))):
                ln = lines[k].strip()
                if not ln: continue
                changed += 1
                if _ACCESS.match(ln): continue
                head = lines[starts[k]] if starts[k] < k else ln
                names, with_scope = declared_names(head, fdep[k])
                ids |= names
                if with_scope and (tag == "replace" or fdep[k] or not names):
                    ids |= set(scopes[k][-1:])
    ids = {t for t in ids if len(t) >= 4 and t not in _CXX_KEYWORDS and t.lower() not in _GENERIC}
    return ids, changed


def _function_depth(lines):
    """For every stripped line, True when it sits inside a function body."""
    res, stack, header = [], [], []
    decl = re.compile(r"\b(?:struct|class|union|enum|namespace)\b|^extern \"C\"")
    for ln in lines:
        res.append(any(stack))
        for ch in re.split(r"([{};])", ln):
            if ch == "{":
                h = " ".join(header).strip()
                is_fn = bool(re.search(r"\)\s*(?:const|noexcept|override|final|->[^{]*|\s)*$", h)) and not decl.search(h)
                stack.append(is_fn or (bool(stack) and stack[-1]))
                header = []
            elif ch == "}":
                if stack: stack.pop()
                header = []
            elif ch == ";":
                header = []
            elif ch:
                header.append(ch)
    return res


def files_naming(root, identifiers, roots=("src", "irisgl", "tests"), exclude=("irisgl/thirdparty",)):
    """Files under the given roots whose text names any of the identifiers (whole word).
    One `git grep` — the tracked tree only, fast."""
    if not identifiers: return {}
    pats = []
    for t in sorted(identifiers):
        pats += ["-e", t]
    hits = collections.defaultdict(set)
    for r in roots:
        cwd = os.path.join(root, r) if r == "irisgl" else root
        spec = ["."] if r == "irisgl" else [r]
        try:
            p = subprocess.run(["git", "grep", "-w", "-o", "-I"] + pats + ["--"] + spec,
                               cwd=cwd, capture_output=True, text=True)
        except OSError:
            continue
        for line in p.stdout.splitlines():
            f, _, tok = line.partition(":")
            if r == "irisgl": f = "irisgl/" + f
            if any(f.startswith(x) for x in exclude): continue
            # code and the tests' scripts only: a CMake file, a doc or a bundled JS library that
            # happens to carry the word names no family (measured: a vendored three.js matched
            # a member name and pulled in the export rule)
            if not (f.endswith(CXX_EXT) or (f.startswith("tests/") and f.endswith((".js", ".js.in")))):
                continue
            hits[f].add(tok)
    return hits


# -- CMake ------------------------------------------------------------------------------------
_CMD = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def cmake_commands(text):
    """Parse a CMake file into commands: (name, first_line, last_line, args_text, function_scope).
    Comments stripped; parentheses balanced; `function`/`macro` bodies carry the function name."""
    lines = text.split("\n")
    clean = []
    for l in lines:
        # strip a # comment outside quotes (bracket comments are rare in our tree)
        out, q = [], False
        for i, ch in enumerate(l):
            if ch == '"' and (i == 0 or l[i - 1] != "\\"): q = not q
            if ch == "#" and not q: break
            out.append(ch)
        clean.append("".join(out))
    cmds, i, fn = [], 0, []
    flat = "\n".join(clean)
    pos = 0
    line_of = []
    ln = 1
    for ch in flat:
        line_of.append(ln)
        if ch == "\n": ln += 1
    while True:
        m = _CMD.search(flat, pos)
        if not m: break
        # must start a line (only whitespace before it on its line)
        ls = flat.rfind("\n", 0, m.start()) + 1
        if flat[ls:m.start()].strip():
            pos = m.end(); continue
        depth, j = 1, m.end()
        while j < len(flat) and depth:
            if flat[j] == "(": depth += 1
            elif flat[j] == ")": depth -= 1
            j += 1
        name = m.group(1).lower()
        args = flat[m.end():j - 1]
        first, last = line_of[m.start()], line_of[max(m.start(), j - 1)]
        if name in ("function", "macro"):
            a = args.split()
            fn.append(a[0].lower() if a else "?")
            cmds.append((name, first, last, args, None))
        elif name in ("endfunction", "endmacro"):
            if fn: fn.pop()
            cmds.append((name, first, last, args, None))
        else:
            cmds.append((name, first, last, args, fn[-1] if fn else None))
        pos = j
    return cmds


DIRECTORY_SCOPE = {"add_compile_options", "add_definitions", "add_compile_definitions",
                   "include_directories", "link_directories", "link_libraries", "project",
                   "cmake_minimum_required", "cmake_policy", "find_package", "include",
                   "set_property", "set_directory_properties", "enable_testing", "enable_language"}
TARGET_CMDS = {"target_sources", "target_link_libraries", "target_compile_options",
               "target_compile_definitions", "target_include_directories", "target_compile_features",
               "target_link_options", "target_precompile_headers", "set_target_properties",
               "add_executable", "add_library", "add_dependencies", "add_custom_target"}
