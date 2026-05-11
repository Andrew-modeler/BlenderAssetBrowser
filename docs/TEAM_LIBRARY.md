# Team Library Setup

The plugin's library data (catalogs, tags, libraries list, asset metadata)
is designed from day one to merge cleanly in a team setting. Choose the
workflow that matches your VCS.

## Architecture

Each project's plugin install owns its own SQLite DB at
`{ProjectSaved}/BlenderAssetBrowser/library.db`. The DB is NOT meant for
sharing — it's an editor-local cache.

For team sharing, use the `.assetlib` JSON sidecar:
- Export from any editor instance via `FAssetLibrarySidecar::ExportToFile`.
- Import into another via `FAssetLibrarySidecar::ImportFromFile`.
- The JSON is sorted-key, line-stable, and merges cleanly in three-way
  diffs (we ship a Python merge driver — see below).

The recommended layout is to keep one `.assetlib` per shared library folder.

## Git workflow

```bash
# In your team repo
git clone https://github.com/<your-org>/<asset-library-repo>
cd <repo>
# Each library has its own .assetlib at its root
ls Library/*.assetlib

# Install the bundled merge driver once per developer
git config --global merge.assetlib.name "BlenderAssetBrowser sidecar merge"
git config --global merge.assetlib.driver \
    "python /abs/path/to/BlenderAssetBrowser/Resources/MergeDriver/assetlib_merge.py %O %A %B"

# Register the driver for .assetlib files (in your repo's .gitattributes)
echo "*.assetlib merge=assetlib" >> .gitattributes
```

Now `git pull` / `git merge` apply the three-way merge automatically.
Conflicts only happen when the same field of the same row is edited on
both sides — the driver picks "ours" and warns. Per-side new entries are
always kept (union).

For large `.uasset` binaries, use **Git LFS**:
```bash
git lfs install
git lfs track "*.uasset"
git lfs track "*.umap"
```

## Perforce workflow

P4 doesn't have a built-in merge driver mechanism. Use a custom resolver:

```bash
# In your .p4ignore / typemap, mark .assetlib as text+merge
echo "text+x //...*.assetlib" >> typemap

# Then set the user-side merger
p4 set P4MERGE="python C:/Plugins/BAB/Resources/MergeDriver/assetlib_merge.py"
```

When `p4 resolve` encounters an `.assetlib` conflict, it'll invoke the
script. Same semantics as Git: union for new entries, prefer-ours on
true conflicts.

For asset locking, use P4's native `p4 lock`/`p4 edit` — the plugin
respects existing checkout state.

## What lives where

| File | Where | VCS Tracked? |
|---|---|---|
| `library.db`, `library.db-wal`, `library.db-shm` | `{ProjectSaved}/BlenderAssetBrowser/` | NO — local editor cache |
| `*.assetlib` | next to each library folder | YES — text, merges via driver |
| `Preview cache` | `{ProjectSaved}/BlenderAssetBrowser/PreviewCache/` | NO — regenerable |
| `Snapshots` | `{ProjectSaved}/BlenderAssetBrowser/Snapshots/` | NO — local rollback only |
| `Exchange` folder | `{ProjectSaved}/BlenderAssetBrowser/Exchange/` | NO — Blender bridge transient |
| Settings | `Config/EditorPerProjectUserSettings.ini` | YES — share library paths |

A sample `.gitignore` for a project with the plugin:

```
# Plugin runtime caches — never commit
**/Saved/BlenderAssetBrowser/library.db*
**/Saved/BlenderAssetBrowser/PreviewCache/
**/Saved/BlenderAssetBrowser/Snapshots/
**/Saved/BlenderAssetBrowser/Exchange/
```

## Round-trip example

1. Developer A creates 5 new catalogs and tags 100 assets locally.
2. Plugin auto-exports `.assetlib` next to each library folder (debounced).
3. Developer A commits `.assetlib`. Developer B pulls.
4. Plugin auto-imports `.assetlib` on next editor start; B sees the same
   catalogs and tags.

If A and B edited the same catalog name, the merge driver:
- Picks A's name (last-writer-wins on string field).
- Both their new catalogs are kept (union).
- Both their tag additions are merged into the asset's tag list.

## Conflict resolution policy

| Field type | Strategy |
|---|---|
| Scalar (name, color, rating, notes) | Last-writer-wins from "ours" |
| Catalog membership | Union |
| Tag assignment | Union |
| Deleted on one side, edited on other | Keep the edit (safer) |
| Deleted on both | Stays deleted |

If you want strict opt-in behavior, set `merge=union` in `.gitattributes`
instead of `merge=assetlib`. The plugin will reread the file and accept
whatever the union produced.
