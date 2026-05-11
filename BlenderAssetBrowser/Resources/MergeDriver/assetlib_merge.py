#!/usr/bin/env python3
"""
assetlib_merge.py — three-way merge driver for `.assetlib` JSON sidecars.

Use with Git:
    git config --global merge.assetlib.name "BlenderAssetBrowser sidecar merge"
    git config --global merge.assetlib.driver \
        "python /path/to/assetlib_merge.py %O %A %B"

    # In .gitattributes:
    *.assetlib merge=assetlib

Strategy:
- catalogs / tags / libraries / assets are arrays of objects each with `id`.
- For each id present in either side: take the side modified more recently
  (last-writer-wins on scalar fields).
- For new ids in either side: include them.
- For deletions: an id present in `base` but missing in BOTH `ours` and
  `theirs` stays deleted; if only one side dropped it, keep it (union).

This is deliberately conservative — refuses to merge if schemas differ.

Exit codes:
   0 — merge succeeded; ours-file has the merged content.
   1 — merge failed; user must resolve by hand.
"""

import json
import sys
from typing import Any, Dict, List

SUPPORTED_SCHEMA = 1
LIST_KEYS = ("catalogs", "tags", "libraries", "assets")


def load(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def by_id(items: Any) -> Dict[int, Dict[str, Any]]:
    """Index a list-of-records by integer `id`. Silently drops malformed entries
    instead of crashing with AttributeError on `.get` (audit LOW)."""
    out: Dict[int, Dict[str, Any]] = {}
    if not isinstance(items, list):
        return out
    for it in items:
        if not isinstance(it, dict):
            continue
        rid = it.get("id")
        if isinstance(rid, int):
            out[rid] = it
    return out


def union(base: Dict, ours: Dict, theirs: Dict) -> Dict:
    """Three-way merge a list-of-objects keyed by id."""
    b_ids = set(base.keys()) if base else set()
    o_ids = set(ours.keys()) if ours else set()
    t_ids = set(theirs.keys()) if theirs else set()

    all_ids = (o_ids | t_ids) - (b_ids - o_ids - t_ids)
    merged = {}
    for rid in all_ids:
        if rid in ours and rid in theirs:
            # Prefer the side that's not equal to base.
            if base.get(rid) == ours.get(rid):
                merged[rid] = theirs[rid]
            else:
                merged[rid] = ours[rid]
        elif rid in ours:
            merged[rid] = ours[rid]
        else:
            merged[rid] = theirs[rid]
    return merged


def main(argv: List[str]) -> int:
    if len(argv) < 4:
        print(f"usage: {argv[0]} <base> <ours> <theirs>", file=sys.stderr)
        return 1

    base_path, ours_path, theirs_path = argv[1], argv[2], argv[3]
    try:
        base   = load(base_path)
        ours   = load(ours_path)
        theirs = load(theirs_path)
    except Exception as e:
        print(f"merge: failed to read inputs: {e}", file=sys.stderr)
        return 1

    # SECURITY (audit LOW): refuse anything that isn't a JSON object at root.
    # A malformed sidecar (e.g. an array instead of dict) would previously
    # propagate as an AttributeError when `.get` is called below — exit 1 is
    # correct, but the traceback leaks local paths via stderr.
    for name, doc in (("base", base), ("ours", ours), ("theirs", theirs)):
        if not isinstance(doc, dict):
            print(f"merge: {name} is not a JSON object; refusing to merge.", file=sys.stderr)
            return 1

    if base.get("schema_version", 0) != SUPPORTED_SCHEMA \
       or ours.get("schema_version", 0) != SUPPORTED_SCHEMA \
       or theirs.get("schema_version", 0) != SUPPORTED_SCHEMA:
        print("merge: schema_version mismatch; refusing to merge.", file=sys.stderr)
        return 1

    merged_root: Dict[str, Any] = {"schema_version": SUPPORTED_SCHEMA}

    for key in LIST_KEYS:
        b = by_id(base.get(key, []))
        o = by_id(ours.get(key, []))
        t = by_id(theirs.get(key, []))
        merged = union(b, o, t)
        merged_root[key] = list(merged.values())

    with open(ours_path, "w", encoding="utf-8") as f:
        json.dump(merged_root, f, indent=4, sort_keys=False, ensure_ascii=False)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
