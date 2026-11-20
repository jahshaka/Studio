#!/usr/bin/env python3
# source.bake_key_versioned (ATOM-HOUSE-1 stage 1; SPECS/V2_CROSS_REFERENCE.md section 2 "the bake") - THE FILES OF THE BAKE
# KEY'S CONTENT HASH CHANGE ONLY TOGETHER WITH kFormatVersion. The mesh bake's producer id is a SHA256 over the files
# irisgl/CMakeLists.txt lists in `_mesh_bake_hashed_sources` (meshbake.cpp/h, materialhelper.cpp, bakeshared.h,
# import/atomdag/*, four meshoptimizer files): one edit there, even a comment, re-bakes every library on every box. V2 edits
# them in ONE bake stage, with the one format bump; this row makes any other edit loud on any branch, in any clone.
#
# THE STATE, no branch and no base: irisgl/import/meshbake.key records `format <kFormatVersion>` and `producer <the id>`
# (that file is NOT on the hashed list, so the record is not part of what it records). The row computes the id exactly as
# CMake does (file(SHA256) of each listed file, `<hex>;` concatenated, SHA256 of that string) and reads kFormatVersion from
# irisgl/import/meshbake.cpp:
#   * the version equals the record's and the id differs  -> RED: a hashed file changed without a format bump (THE law);
#   * the version differs from the record's                -> RED: the bump's commit writes the new pair into the record;
#   * both equal                                           -> green.
# THE RECORD MOVES ONLY WITH THE VERSION: where git history is present, the last commit that MODIFIED meshbake.key (its
# creation is exempt) must change the kFormatVersion line too, and an uncommitted record edit needs an uncommitted
# version edit; without git this half is a note.
# THE LIST'S PARSE IS CHECKED, not trusted (the audit's finding d): `#` comments are dropped before the block is read, the
# block must be the list's ONLY definition (no second set(), no list(APPEND|INSERT|PREPEND|REMOVE_ITEM|FILTER|TRANSFORM)
# of it), every listed file must exist, and the id computed from the parse must equal CMake's own as the build recorded it
# (<build>/tests/seed-key-inputs.txt, `producer <id>`): a truncated or extended list cannot agree with CMake's.
# FIRST, THE NEGATIVE CONTROL: the parse of a block with a `)` in a comment, the mutation refusal, and the version law on
# three synthetic states (an edit without a bump red, a bump without a record red, a recorded pair green).
#
# argv: <studio source root> <build dir>
import hashlib
import os
import re
import subprocess
import sys

LIST = "_mesh_bake_hashed_sources"
VERSION_RE = re.compile(r"^constexpr int kFormatVersion\s*=\s*(\d+)\s*;", re.M)
RECORD = "import/meshbake.key"


def fail(msg):
    print("FAIL: " + msg)
    sys.stdout.flush()
    sys.exit(1)


def strip_comments(cmake_text):
    return "\n".join(line.split("#", 1)[0] for line in cmake_text.splitlines())


def hashed_list(cmake_text):
    """The list's files, or a string saying why the block cannot be trusted."""
    code = strip_comments(cmake_text)
    if re.search(r"\blist\s*\(\s*(APPEND|INSERT|PREPEND|REMOVE_ITEM|REMOVE_AT|FILTER|TRANSFORM|SORT)\s+" + LIST + r"\b",
                 code, re.I):
        return "the list is mutated by a list() call: this parse reads one set() block only - extend the row first"
    defs = re.findall(r"\bset\s*\(\s*" + LIST + r"\b([^)]*)\)", code)
    if len(defs) != 1:
        return "%d set(%s ...) blocks (exactly one expected)" % (len(defs), LIST)
    files = defs[0].split()
    if not files:
        return "the set() block lists no file"
    return files


def producer_id(root, files):
    blob = "".join(hashlib.sha256(open(os.path.join(root, f), "rb").read()).hexdigest() + ";" for f in files)
    return hashlib.sha256(blob.encode()).hexdigest()


def verdict(version, pid, record):
    """None when the state is lawful, else the red's text. `record` = (format, producer)."""
    rv, rp = record
    if version != rv:
        return ("kFormatVersion is %d and the record says %d: the bump's commit writes 'format %d' and 'producer %s' "
                "into irisgl/%s" % (version, rv, version, pid, RECORD))
    if pid != rp:
        return ("a file of the bake key's content hash changed WITHOUT a format bump (producer %s, recorded %s at "
                "format %d): revert the edit, or bump kFormatVersion and re-record (V2: the one bake stage)"
                % (pid[:16], rp[:16], version))
    return None


def read_record(path):
    fmt = pid = None
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].split()
        if len(line) == 2 and line[0] == "format" and line[1].isdigit():
            fmt = int(line[1])
        elif len(line) == 2 and line[0] == "producer" and re.fullmatch(r"[0-9a-f]{64}", line[1]):
            pid = line[1]
    return fmt, pid


def git(cwd, *args):
    p = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr.strip()


def main():
    if len(sys.argv) != 3:
        fail("usage: bake_key_versioned.py <studio source root> <build dir>")
    root, build = sys.argv[1], sys.argv[2]
    iris = os.path.join(root, "irisgl")

    # ---- the negative control ------------------------------------------------------------------------------------
    sample = ("set(" + LIST + "\n    import/a.cpp   # the serializer (see its header)\n    import/b.h)\n"
              "set(_other x)\n")
    if hashed_list(sample) != ["import/a.cpp", "import/b.h"]:
        fail("the negative control: a ')' in a comment truncated the parse: %s" % (hashed_list(sample),))
    if not isinstance(hashed_list(sample + "list(APPEND " + LIST + " import/c.h)\n"), str) or \
            not isinstance(hashed_list(sample + "set(" + LIST + " import/c.h)\n"), str):
        fail("the negative control: a mutated or redefined list was not refused")
    a, b = "a" * 64, "b" * 64
    if verdict(32, b, (32, a)) is None or verdict(33, b, (32, a)) is None or verdict(33, b, (33, b)) is not None:
        fail("the negative control: the version law misjudged an edit without a bump, a bump without a record, or a "
             "recorded pair")
    print("ok:   the negative control: the parse survives a ')' in a comment, a mutated list is refused, an edit "
          "without a bump and a bump without a record are red, a recorded pair is green")

    # ---- the list, and CMake's own id --------------------------------------------------------------------------
    try:
        files = hashed_list(open(os.path.join(iris, "CMakeLists.txt"), encoding="utf-8").read())
    except OSError as e:
        fail("cannot read irisgl/CMakeLists.txt: %s" % e)
    if isinstance(files, str):
        fail("irisgl/CMakeLists.txt: " + files)
    missing = [f for f in files if not os.path.isfile(os.path.join(iris, f))]
    if missing:
        fail("listed in the bake key but absent: %s" % ", ".join(missing))
    if RECORD in files:
        fail("irisgl/%s is on the hashed list: the record would be part of what it records" % RECORD)
    pid = producer_id(iris, files)
    seed = os.path.join(build, "tests", "seed-key-inputs.txt")
    cmake_pid = None
    if os.path.isfile(seed):
        for line in open(seed, encoding="utf-8"):
            w = line.split()
            if len(w) == 2 and w[0] == "producer":
                cmake_pid = w[1]
    if not cmake_pid:
        fail("no `producer <id>` in %s: CMake's own id is the parse's cross-check (configure the build)" % seed)
    if cmake_pid != pid:
        fail("the parse's id %s is not CMake's %s (%s): the list was read wrong, or a hashed file changed after the "
             "last configure - build first" % (pid[:16], cmake_pid[:16], seed))
    print("ok:   %d hashed files; the producer id %s = CMake's own (the build's seed-key inputs)" % (len(files), pid[:16]))

    # ---- the state ---------------------------------------------------------------------------------------------
    m = VERSION_RE.search(open(os.path.join(iris, "import", "meshbake.cpp"), encoding="utf-8").read())
    if not m:
        fail("no 'constexpr int kFormatVersion = N;' in irisgl/import/meshbake.cpp")
    version = int(m.group(1))
    rec = os.path.join(iris, RECORD)
    if not os.path.isfile(rec):
        fail("no record irisgl/%s (write 'format %d' and 'producer %s')" % (RECORD, version, pid))
    record = read_record(rec)
    if None in record:
        fail("irisgl/%s holds no 'format N' and 'producer <64 hex>' lines" % RECORD)
    bad = verdict(version, pid, record)
    if bad:
        fail(bad)
    print("ok:   format %d, producer %s = the record" % (version, pid[:16]))

    # ---- the record moves only with the version ---------------------------------------------------------------
    rc, _, _ = git(iris, "rev-parse", "--git-dir")
    if rc != 0:
        print("note: no git history: the record's history half is not judged")
    else:
        _, dirty_rec, _ = git(iris, "diff", "--name-only", "HEAD", "--", RECORD)
        _, dirty_ver, _ = git(iris, "diff", "-U0", "HEAD", "--", "import/meshbake.cpp")
        moved = re.search(r"^[-+]constexpr int kFormatVersion", dirty_ver, re.M)
        if dirty_rec.strip() and not moved:
            fail("irisgl/%s is edited and kFormatVersion is not: the record moves only with the version" % RECORD)
        _, last, _ = git(iris, "log", "-1", "--diff-filter=M", "--format=%H", "--", RECORD)
        last = last.strip()
        if last:
            _, diff, _ = git(iris, "show", "-U0", "--format=", last, "--", "import/meshbake.cpp")
            if not re.search(r"^[-+]constexpr int kFormatVersion", diff, re.M):
                fail("commit %s changed irisgl/%s without changing kFormatVersion" % (last[:12], RECORD))
            print("ok:   the record's last change (%s) moved kFormatVersion with it" % last[:12])
        else:
            print("ok:   the record has not changed since it was written")
    print("source.bake_key_versioned: ok")
    sys.stdout.flush()


if __name__ == "__main__":
    main()
