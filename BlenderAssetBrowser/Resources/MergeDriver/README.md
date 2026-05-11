# `.assetlib` Merge Driver

Three-way merge driver for the `.assetlib` JSON sidecar files used to sync
the library across team members via Git or Perforce.

## What it merges

`.assetlib` files contain four lists keyed by `id`:
`catalogs`, `tags`, `libraries`, `assets`. Each list is merged via union:

- New ids in either side: keep them.
- Same id modified on one side only: take that side.
- Same id modified on both: take "ours" (mirrors Git's default for binary
  files; in practice this is rare because per-asset rows change rarely).
- Id deleted on both: drop it.
- Id deleted on one side only: keep it (union — safer than losing data).

Refuses to merge if `schema_version` differs between any pair of base/ours/theirs.

## Install (Git)

```bash
git config --global merge.assetlib.name "BlenderAssetBrowser sidecar merge"
git config --global merge.assetlib.driver \
    "python /absolute/path/to/assetlib_merge.py %O %A %B"
```

Then in your repo's `.gitattributes`:

```
*.assetlib merge=assetlib
```

## Install (Perforce)

P4 doesn't have a native merge-driver mechanism, but you can wire this in
via a `p4 resolve` custom merger. See p4 docs for `P4MERGE` env var:

```bash
export P4MERGE="python /path/to/assetlib_merge.py"
```

P4 passes base/yours/theirs as positional args matching this script's order.

## Exit codes

- `0` — merge succeeded. `ours.assetlib` now contains the merged content;
  Git/P4 considers the conflict resolved.
- `1` — merge failed (schema mismatch, invalid JSON). User must resolve
  by hand.

## Security

The driver:
- Only operates on the three input files given by the VCS.
- Does NOT execute any code from the JSON content.
- Caps in-memory size implicitly via Python's JSON parser.
- Has no network access.

Still, it runs whatever Python interpreter Git/P4 invokes. If your team
uses this, audit the script before installing.
