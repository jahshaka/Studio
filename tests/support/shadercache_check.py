#!/usr/bin/env python3
"""shadercache_check.py - IS A SHADER CACHE DIRECTORY ONE GENERATION? (URGENT #117, HARNESS-4, 2026-10-29)

    shadercache_check.py <cache-dir>     exit 0: every file its manifest names is there with the manifest's length and
                                         hash ("consistent: N files"); 1: torn - the first file that is not, and why
                                         ("<name>: ..."); 2: no manifest (or a malformed one) - nothing to judge
    shadercache_check.py --hash <file>   the file's hash as a manifest line carries it (a toy cache's writer)
    shadercache_check.py --selftest      this script's own hash against the engine's on fixed data: exit 0 equal,
                                         1 different, 3 the engine's library not found (only the script's own ran)

WHY. The engine verifies every file of its shader cache against the manifest before it loads a byte
(irisgl/engine/src/OgreShaderCache.cpp readVerified: the length, then MurmurHash3_x64_128 seeded 0x9E3779B9 as 32 hex
digits, cachefile::hex128) and on ONE mismatch drops the whole generation and boots COLD. BUGS-8's 152-row run
(1c18baa53, 2026-10-29 11:56-12:05) seeded eight rtreflect rows in turn from a SHARED engine cache whose hlms.4.bin
failed that check (same length, other bytes): each logged "checksum mismatch / verification failed - starting cold",
compiled everything under the 60-s cap and timed out, and a timed-out row promotes nothing, so the torn cache stood
until a green row replaced it - while scripts/warm-app-cache.sh, which read only the manifest's layer keys, called it
warm. So the harness asks the engine's own question: fresh_home.sh never promotes a home whose cache fails it, and
warm-app-cache.sh reads a shared cache that fails it as cold (its -off boot then replaces it).

THE HASH IS THE ENGINE's: Ogre::MurmurHash3_x64_128 out of the tree's libOgreNextMain.so (the build's CMakeCache.txt
OGRE_NEXT_OgreNextMain_LIB, found by walking up from the directory; or JAH_OGRE_MAIN_LIB) through ctypes - 34 ms for a
9 MB pipeline cache. Without it (a toy tree) the same function in Python (~10 MB/s; --selftest holds the two equal).
"""
import ctypes
import os
import struct
import sys

SEED = 0x9E3779B9
_M = (1 << 64) - 1


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & _M


def _fmix(k):
    k ^= k >> 33
    k = (k * 0xff51afd7ed558ccd) & _M
    k ^= k >> 33
    k = (k * 0xc4ceb9fe1a85ec53) & _M
    k ^= k >> 33
    return k


def murmur_py(data, seed=SEED):
    """MurmurHash3_x64_128 (Austin Appleby's reference; Ogre's OgreMurmurHash3.cpp) as the engine prints it."""
    n = len(data)
    h1 = h2 = seed
    c1, c2 = 0x87c37b91114253d5, 0x4cf5ad432745937f
    for i in range(n // 16):
        k1, k2 = struct.unpack_from("<QQ", data, i * 16)
        k1 = _rotl((k1 * c1) & _M, 31)
        h1 ^= (k1 * c2) & _M
        h1 = (_rotl(h1, 27) + h2) & _M
        h1 = (h1 * 5 + 0x52dce729) & _M
        k2 = _rotl((k2 * c2) & _M, 33)
        h2 ^= (k2 * c1) & _M
        h2 = (_rotl(h2, 31) + h1) & _M
        h2 = (h2 * 5 + 0x38495ab5) & _M
    tail = data[(n // 16) * 16:]
    k1 = k2 = 0
    for i in range(len(tail) - 1, 7, -1):
        k2 ^= tail[i] << (8 * (i - 8))
    if len(tail) > 8:
        k2 = _rotl((k2 * c2) & _M, 33)
        h2 ^= (k2 * c1) & _M
    for i in range(min(len(tail), 8) - 1, -1, -1):
        k1 ^= tail[i] << (8 * i)
    if tail:
        k1 = _rotl((k1 * c1) & _M, 31)
        h1 ^= (k1 * c2) & _M
    h1 ^= n
    h2 ^= n
    h1 = (h1 + h2) & _M
    h2 = (h2 + h1) & _M
    h1, h2 = _fmix(h1), _fmix(h2)
    h1 = (h1 + h2) & _M
    h2 = (h2 + h1) & _M
    return "%016x%016x" % (h1, h2)


def _find_lib(start):
    lib = os.environ.get("JAH_OGRE_MAIN_LIB")
    if lib:
        return lib
    d = os.path.abspath(start)
    while True:
        cc = os.path.join(d, "CMakeCache.txt")
        if os.path.isfile(cc):
            with open(cc, errors="replace") as f:
                for line in f:
                    if line.startswith("OGRE_NEXT_OgreNextMain_LIB:"):
                        return line.split("=", 1)[1].strip()
        up = os.path.dirname(d)
        if up == d:
            return None
        d = up


def engine_hash(start):
    """The engine's hex128 through its own Ogre library, or None when that library is not found."""
    lib = _find_lib(start)
    if not lib or not os.path.isfile(lib):
        return None
    try:
        so = ctypes.CDLL(lib)
        fn = getattr(so, "_ZN4Ogre19MurmurHash3_x64_128EPKvijPv")
    except (OSError, AttributeError):
        return None
    fn.restype = None
    fn.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_uint32, ctypes.c_void_p]

    def h(data):
        out = (ctypes.c_uint64 * 2)()
        fn(data, len(data), SEED, out)
        return "%016x%016x" % (out[0], out[1])
    return h


def check(d):
    """(exit code, line) for the cache directory `d`."""
    man = os.path.join(d, "cache-manifest.txt")
    try:
        with open(man, errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        return 2, "no manifest in %s" % d
    files = []
    for line in lines:
        p = line.split()
        if p and p[0] == "file":
            if len(p) < 5 or not p[2].isdigit():
                return 2, "a malformed manifest line: %r" % line
            files.append((p[1], int(p[2]), p[3]))
    h = engine_hash(d) or murmur_py
    for name, size, want in files:
        try:
            with open(os.path.join(d, name), "rb") as f:
                data = f.read()
        except OSError:
            return 1, "%s: named by the manifest, not there" % name
        if len(data) != size:
            return 1, "%s: %d bytes, the manifest says %d" % (name, len(data), size)
        got = h(data)
        if got != want:
            return 1, "%s: its hash %s, the manifest says %s (same length, other bytes)" % (name, got, want)
    return 0, "consistent: %d files" % len(files)


def main(argv):
    if len(argv) == 3 and argv[1] == "--hash":
        with open(argv[2], "rb") as f:
            data = f.read()
        print((engine_hash(os.path.dirname(os.path.abspath(argv[2]))) or murmur_py)(data))
        return 0
    if len(argv) == 2 and argv[1] == "--selftest":
        h = engine_hash(os.getcwd())
        if h is None:
            print("shadercache_check: the engine's library not found (JAH_OGRE_MAIN_LIB / a CMakeCache.txt above the cwd)")
            return 3
        bad = [n for n in (0, 1, 7, 8, 9, 15, 16, 17, 31, 4096, 100003)
               if h(bytes((i * 131 + n) & 255 for i in range(n))) != murmur_py(bytes((i * 131 + n) & 255 for i in range(n)))]
        print("shadercache_check: the script's hash %s the engine's%s" % ("DIFFERS from" if bad else "equals",
                                                                           " at lengths %s" % bad if bad else ""))
        return 1 if bad else 0
    if len(argv) != 2:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 64
    rc, line = check(argv[1])
    print(line)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
