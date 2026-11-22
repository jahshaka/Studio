#!/usr/bin/env python3
# library.scale's COLUMN COMPARISON (LARGE-LIBRARY-1, the spec's call 5; stage 1's spikes/large-library-1/stage1/columns.py):
# one row a REAL import wrote against one row the generator wrote (tests/support/libraryfixture.h), column by column —
# per column the CLASS of the value (NULL / a discrete value / int / id / text / BLOB / JSON and its keys), the properties'
# nested keys, the store rows (role, ext, refcount) and the dependency edges. A difference fixes the HELPER, never the law.
#   usage: library_columns.py <ref.db> <gen.db> <kind>=<refguid>:<genguid>...     exit = the number of differences
import sqlite3, sys, json
ref = sqlite3.connect(sys.argv[1]); gen = sqlite3.connect(sys.argv[2])
pairs = [p.split("=") for p in sys.argv[3:]]   # kind=refguid:genguid
DISCRETE = {"type", "collection", "times_used", "view_filter", "origin", "listed", "desktop", "role", "ext",
            "depender_type", "dependee_type", "refcount", "version"}
def klass(col, v):
    if v is None: return "NULL"
    if isinstance(v, bytes):
        try:
            j = json.loads(v)
            if isinstance(j, dict): return "JSON keys " + ",".join(sorted(j.keys()))
        except Exception: pass
        return "BLOB" + (" PNG" if v[:4] == b"\x89PNG" else "")
    if col in DISCRETE: return repr(v)
    if isinstance(v, int): return "int"
    if col in ("guid", "asset_guid", "depender", "dependee", "id", "project_guid", "parent", "oid", "memberOf"): return "id"
    return "text"
def size(v): return f"{len(v)} B" if isinstance(v, (bytes, str)) else ("" if v is None else str(v))
def rows(c, sql, args):
    cur = c.execute(sql, args); cols = [d[0] for d in cur.description]; return [dict(zip(cols, r)) for r in cur.fetchall()]
diffs = 0
def compare(title, r, g):
    global diffs
    print(f"  [{title}]")
    for col in (r[0].keys() if r else g[0].keys() if g else []):
        a = r[0].get(col) if r else None; b = g[0].get(col) if g else None
        ka, kb = klass(col, a), klass(col, b)
        same = ka == kb
        if col == "parent" and a is not None and b is not None: same = True
        diffs += 0 if same else 1
        print(f"    {'same' if same else 'DIFF':4}  {col:14} ref {ka} {size(a) if ka.startswith(('BLOB','JSON','text')) else ''} | gen {kb} {size(b) if kb.startswith(('BLOB','JSON','text')) else ''}")
    if len(r) != len(g):
        diffs += 1; print(f"    DIFF  rows: ref {len(r)} gen {len(g)}")
for kind, guids in pairs:
    rg, gg = guids.split(":")
    print(f"=== {kind}  ref {rg}  gen {gg}")
    t = "projects" if kind == "project" else "assets"
    compare(t, rows(ref, f"SELECT * FROM {t} WHERE guid = ?", (rg,)), rows(gen, f"SELECT * FROM {t} WHERE guid = ?", (gg,)))
    if t == "assets":
        for col in ("properties",):
            a = rows(ref, "SELECT properties FROM assets WHERE guid = ?", (rg,))[0]["properties"]
            b = rows(gen, "SELECT properties FROM assets WHERE guid = ?", (gg,))[0]["properties"]
            if a and b:
                ja, jb = json.loads(a), json.loads(b)
                for key in sorted(set(ja) | set(jb)):
                    if isinstance(ja.get(key), dict) or isinstance(jb.get(key), dict):
                        ka = sorted((ja.get(key) or {}).keys()); kb = sorted((jb.get(key) or {}).keys())
                        same = ka == kb; diffs += 0 if same else 1
                        print(f"    {'same' if same else 'DIFF':4}  properties.{key} keys ref {ka} | gen {kb}")
        q = "SELECT a.role, a.name, f.ext, f.refcount FROM asset_files a JOIN files f ON f.oid = a.oid WHERE a.asset_guid = ? ORDER BY a.role"
        ra, ga = rows(ref, q, (rg,)), rows(gen, q, (gg,))
        roles = lambda rs: [(x["role"], x["ext"], x["refcount"]) for x in rs]
        same = roles(ra) == roles(ga); diffs += 0 if same else 1
        print(f"    {'same' if same else 'DIFF':4}  store (role, ext, refcount) ref {roles(ra)} | gen {roles(ga)}")
        q = "SELECT depender_type, dependee_type, project_guid IS NULL AS intrinsic, depender = ? AS outgoing FROM dependencies WHERE depender = ? OR dependee = ? ORDER BY outgoing, depender_type, dependee_type"
        da = [tuple(x.values()) for x in rows(ref, q, (rg, rg, rg))]; dg = [tuple(x.values()) for x in rows(gen, q, (gg, gg, gg))]
        same = da == dg
        if kind == "assetsHomeMaterial": same = len(dg) >= 1 and all(d == (1, 2, 1, 1) for d in dg) and all(d == (1, 2, 1, 1) for d in da)
        diffs += 0 if same else 1
        print(f"    {'same' if same else 'DIFF':4}  edges (depender_type, dependee_type, intrinsic, outgoing) ref {da} | gen {dg}")
print(f"TOTAL DIFF {diffs}")
sys.exit(min(diffs, 255))
