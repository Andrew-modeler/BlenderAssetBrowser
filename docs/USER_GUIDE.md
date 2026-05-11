# BlenderAssetBrowser — User Guide

A practical, task-oriented guide. Skim the **Concepts** section once, then come
back to the chapter you need.

## Contents

1. [Install](#1-install)
2. [Concepts](#2-concepts)
3. [UI tour](#3-ui-tour)
4. [Adding assets to the library](#4-adding-assets-to-the-library)
5. [Browsing and filtering](#5-browsing-and-filtering)
6. [Search syntax](#6-search-syntax)
7. [Catalogs](#7-catalogs)
8. [Tags and AI auto-tagging](#8-tags-and-ai-auto-tagging)
9. [Cross-project libraries](#9-cross-project-libraries)
10. [Blender bridge](#10-blender-bridge)
11. [DCC integration (Substance, Photoshop, Max)](#11-dcc-integration-substance-photoshop-max)
12. [Snapshots and rollback](#12-snapshots-and-rollback)
13. [Fab update tracker](#13-fab-update-tracker)
14. [Quick Picker](#14-quick-picker)
15. [Naming linter](#15-naming-linter)
16. [Dependency-aware copy](#16-dependency-aware-copy)
17. [VCS sync (.assetlib sidecar)](#17-vcs-sync-assetlib-sidecar)
18. [Project Settings reference](#18-project-settings-reference)
19. [Troubleshooting](#19-troubleshooting)

---

## 1. Install

### Prerequisites

- Unreal Engine **5.3, 5.4, 5.5, or 5.6** (verified on 5.6).
- Windows 64-bit. Linux/Mac builds are not packaged yet.
- ~250 MB free disk for the plugin (including the SigLIP2 ONNX model).
- Visual Studio 2022 with the **Game Development with C++** workload, if you
  plan to rebuild the plugin yourself.

### Drop the plugin into your project

1. Close the Unreal Editor if it's open.
2. Open your project folder. Create a `Plugins/` subfolder if it doesn't exist.
3. Clone or copy the plugin in:

   ```powershell
   cd YourProject\Plugins
   git clone https://github.com/Andrew-modeler/BlenderAssetBrowser.git
   ```

   Result:
   ```
   YourProject\
   ├── YourProject.uproject
   ├── Source\
   └── Plugins\
       └── BlenderAssetBrowser\
           ├── BlenderAssetBrowser.uplugin
           ├── Source\
           ├── Resources\
           └── ThirdParty\
   ```

4. Right-click `YourProject.uproject` → **Generate Visual Studio project files**.
5. Open the resulting `.sln` in Visual Studio 2022, build **Development Editor**
   for **Win64**. The first build takes 5–15 minutes (compiles 6 modules,
   ~13 K lines of C++).
6. Open the project in Unreal — the plugin loads at editor startup.

### First launch

On first launch the plugin will:

- Create `YourProject\Saved\BlenderAssetBrowser\library.db` (the SQLite library).
- Run schema migrations to version 2 (creates 17 tables + FTS5 indexes).
- Load the SigLIP2 vision model from
  `Plugins\BlenderAssetBrowser\Resources\Models\siglip2_vision_fp16.onnx`
  and verify its SHA-1 hash. If the file is missing or tampered with, AI
  auto-tagging stays disabled but the rest of the plugin works fine.
- Load 189 pre-computed tag embeddings (~570 KB) for built-in vocabulary.

Watch the **Output Log** filtered by `BlenderAssetBrowser` — you should see:

```
LogBlenderAssetBrowser: Database opened: ...library.db
LogBlenderAssetBrowser: Schema migrated to version 2.
LogAITagging: SigLIPInference: model loaded (1 in / 2 out).
LogAITagging: AITagging module started (vocabulary: loaded; SigLIP inference: ready).
```

If you see those four lines, you're ready.

---

## 2. Concepts

These five words are everything you need to internalize.

### Asset

Any `.uasset` you've explicitly **marked** as part of your library. Marking
adds a row to the `assets` table and lets the plugin track metadata for it
(rating, notes, source, tags, etc.). Unmarked content is invisible to the
asset browser even though Unreal sees it in Content Browser.

### Catalog

A **folder** in the asset browser tree. Catalogs are independent of your UE
folder layout — you can have a catalog called `Foliage / Trees / Pine` that
collects assets scattered across `/Game/Megascans`, `/Game/Custom`, and
`/Game/Fab`. One asset can live in multiple catalogs at once.

### Tag

A short keyword attached to an asset. Examples: `wood`, `wet`, `tileable`,
`PBR-2K`. Tags come from three sources:

- **Manual** — you typed it.
- **AI** — SigLIP2 auto-tagging guessed it. Has a `confidence` score.
- **Bulk** — applied to many assets at once from a selection.

Tags are case-insensitive. Multi-word tags are allowed (`hand painted`).

### Library

A **library mount** is a folder of `.uasset` files outside `YourProject/Content`
that the plugin makes browsable. Useful when you have a shared
`Z:\TeamAssets\` folder or want to reuse a previous project's assets without
copying. Mounted libraries appear as virtual paths like
`/AssetLibraries/TeamAssets/`.

### Provenance

Where an asset came from. The plugin auto-detects:
- **[F]** — Fab marketplace asset (matched against Fab Vault Cache).
- **[M]** — Quixel Megascans (`/Game/Megascans/` prefix).
- **[L]** — Local (your own creation, no source URL).
- **[I]** — Imported (has `AssetImportData` but no source pack).
- **[C]** — Crafted (manually re-classified as custom).

You'll see these as prefixes in the asset name column.

---

## 3. UI tour

### Opening the asset browser

Three ways:

- **Window** menu → **Blender Asset Browser**.
- **Content Browser** → right-click empty area → **Blender Asset Browser →
  Open Window**.
- Hotkey: **Ctrl+Shift+Space** (default — see Settings to rebind). This opens
  the lightweight **Quick Picker** overlay, not the full window.

### Layout

```
┌─────────────────────────────────────────────────────────────┐
│  Toolbar  [ Refresh ]  [ List | Grid ]  [ ⚙ Settings ]      │
├──────────────┬─────────────────────────────┬────────────────┤
│              │                             │                │
│  Catalog     │     Asset list / grid       │   Inspector    │
│  tree        │                             │                │
│              │                             │                │
│  ▸ All       │   [F] M_Concrete_01         │   Name         │
│  ▸ Foliage   │   [M] SM_Rock_Granite_03    │   Path         │
│  ▾ Props     │   [L] BP_Door_Heavy         │   Type         │
│      Wooden  │                             │   Rating       │
│      Stone   │                             │   Tags         │
│              │                             │   Notes        │
└──────────────┴─────────────────────────────┴────────────────┘
```

- **Left:** Catalog tree. Drag a row onto another to reparent.
- **Center:** Either a sortable list or a thumbnail grid (toolbar toggle).
- **Right:** Inspector for the currently selected asset.

### List vs Grid

- **List** is fastest for triage — sortable, dense.
- **Grid** uses the same `FAssetThumbnail` renderer as the UE Content Browser,
  so thumbnails are identical. Thumbnails are cached on disk and invalidated
  by source-file mtime — the cache rebuilds automatically when you reimport.

---

## 4. Adding assets to the library

The plugin doesn't auto-index every `.uasset` in your project. That's a
deliberate choice — it lets you curate. You can always bulk-mark later.

### Mark a single asset

1. Right-click on the asset in the **Content Browser** (NOT the asset browser
   window).
2. Choose **Blender Asset Browser → Mark as Asset**.
3. The asset appears in the asset browser. Provenance is auto-detected.

### Mark a selection

Select multiple assets in Content Browser → right-click → **Mark as Asset**.
The plugin checks each against an allowlist of supported classes and skips
anything it doesn't recognize.

**Supported classes (16 total):**
StaticMesh · SkeletalMesh · Material · MaterialInstanceConstant ·
MaterialFunction · Texture2D · TextureCube · ParticleSystem · NiagaraSystem ·
Blueprint · AnimationAsset · SoundCue · SoundWave · DataAsset ·
DataTable · CurveBase.

If an asset isn't supported, the menu silently skips it (no error).

### Unmark

In the asset browser, select asset(s) → right-click → **Unmark**.
Removes the row from the `assets` table but does NOT delete the `.uasset`.

---

## 5. Browsing and filtering

### Filter by catalog

Click a catalog row in the left tree. The center list shows assets in that
catalog AND all descendants (subcatalogs).

Click **All** at the top to show every marked asset.

### Filter by tag

Use the search box: `tag:wood` shows only assets tagged `wood`. Combine with
text: `tag:wood rough` shows rough-grained wood.

### Filter by source

`source:fab` shows only Fab assets. Other values: `megascans`, `local`,
`imported`, `crafted`.

### Sort

Click a column header in List view. Default sort: `modified_at DESC`
(newest first).

---

## 6. Search syntax

The search box accepts a structured query language. It's case-insensitive
and forgiving — bad syntax just returns no results, never an error.

### Free text

`rock` — matches any asset whose name, notes, source pack, or source author
contains "rock". Uses FTS5 ranking (best matches first within the
modified_at sort order).

### Field filters (string match)

`field:value` — exact-equality filter on a column.

| Field | Maps to column | Example |
|---|---|---|
| `type` | `asset_type` | `type:StaticMesh` |
| `source` | `source_type` | `source:fab` |
| `license` | `source_license` | `license:CC0` |
| `author` | `source_author` | `author:Quixel` |
| `pack` | `source_pack_name` | `pack:Forest` |
| `engine` | `engine_version` | `engine:5.6.0` |
| `preview` | `preview_path` | `preview:thumb.png` |

### Numeric filters

`field:OP:value` where `OP` is one of `>` `<` `>=` `<=` `=`.
Combined with the operator without colons: `tris:<5000`.

| Field | Example |
|---|---|
| `tris` | `tris:<5000` |
| `verts` | `verts:>=10000` |
| `lod` | `lod:>=3` |
| `rating` | `rating:>=4` |
| `texture_res` | `texture_res:>2048` |
| `size` | `size:>50000000` (50 MB+) |
| `materials` | `materials:<3` |

### Boolean operators

Case-sensitive: `AND`, `OR`, `NOT`.

```
wood AND tileable
rock OR stone
NOT fab
oak AND NOT pine
```

Parentheses group: `(wood OR stone) AND tileable`.

### Combine everything

```
tag:foliage tris:<8000 source:megascans rating:>=4
type:StaticMesh AND NOT pack:Megascans
```

Search history is kept in the `search_history` table — useful if you forget
a query.

---

## 7. Catalogs

### Create

In the catalog tree, right-click a parent (or the **All** root) → **New Catalog**.
Enter a name. Done.

### Reparent

Drag a catalog row onto another in the tree. The destination becomes the new
parent. The plugin **rejects cycles** (you can't make a node its own ancestor)
via a BFS check with depth limit 64.

### Assign assets

Two ways:
- Drag an asset from the center list onto a catalog row.
- Right-click asset → **Add to catalog** → pick from menu.

One asset can belong to multiple catalogs.

### Smart catalogs

Auto-populated catalogs based on a query. Two built-in:

- **Outdated** — assets whose `update_state = 'update_available'`
  (Fab scraper found a newer version).
- **Source Changed** — assets whose source file mtime moved forward since
  last import.

You can't create custom smart catalogs through the UI yet — the schema is in
place (`is_smart`, `smart_query` columns) but the editor is on the roadmap.

---

## 8. Tags and AI auto-tagging

### Manual tagging

Select an asset → inspector → **Tags** field. Type a tag, press Enter.
Press Enter on an empty line to commit.

### Bulk add tag

Select multiple assets in the **Content Browser** → right-click →
**Blender Asset Browser → Bulk Add Tag**. Type a tag, press Enter
(or Apply). The tag is added to every selected asset. ESC = Cancel.

### AI auto-tagging

This is the headline feature. Right-click selected asset(s) in **Content
Browser** → **Blender Asset Browser → Auto-tag (AI)**.

What happens:

1. **Phase 1 (Game Thread, with progress bar):** for each asset, the plugin
   renders a 224×224 thumbnail and looks up the asset's DB id.
2. **Phase 2 (Background Thread, with progress bar + Cancel):** for each
   asset, the SigLIP2 vision encoder runs on the thumbnail. The 768-dim
   embedding is saved to `asset_embeddings`. Cosine similarity is computed
   against 189 pre-computed tag embeddings.
3. Tags with `sigmoid(scale × cos) ≥ threshold` are assigned (default
   threshold 0.30, scale 4.0).

You can interrupt at any time — partial progress is preserved.

### AI tag review modal

If **Settings → AI → AI tag review before apply** is on, after Phase 2 a
dialog opens per asset showing candidate tags with their confidence scores.
Check the ones you want, uncheck the rest, hit Apply. Skipped tags are not
stored.

### Confidence threshold

`Settings → AI → AI tag confidence threshold` (default 0.30, range 0.00–1.00).
Lower = more tags, more noise. Higher = fewer tags, more conservative.
The threshold is sigmoid-scaled (calibrated probability), not raw cosine.

### Vocabulary

The 189 built-in tags cover broad PBR/asset categories: materials (wood,
stone, metal), surfaces (rough, smooth, wet, dry), themes (sci-fi, medieval,
modern), and so on. See `Resources/TagVocabulary/default_tags.json`.

To add your own tags **with embeddings**: regenerate
`default_tag_embeddings.bin` using `Resources/TagVocabulary/build_embeddings.py`
(needs the SigLIP2 text encoder, ~565 MB, not shipped — download from
HuggingFace). Each tag's text is encoded by the SigLIP2 text tower; the
resulting 768-dim vector is what cosine compares against.

To add your own tags **without embeddings** (manual only, no AI): the
`tags` table supports any text; just add via UI → AI scoring will simply
skip tags it has no embedding for.

---

## 9. Cross-project libraries

A **library mount** maps an external folder of `.uasset` files to a virtual
UE path like `/AssetLibraries/<Name>/`. Assets inside become browsable
without being copied.

### Mount

`Settings → Libraries → Add Library`. Provide:
- **Name** — used as the virtual path component.
- **Absolute path** — a folder on disk (must contain `.uasset` files).
- **Engine version** (optional) — the version the library was authored in.

On editor restart, the plugin registers the mount. Assets appear under
`/AssetLibraries/<Name>/...`.

### Allowed roots

For safety, the plugin refuses to mount:
- System folders (`C:\Windows`, `C:\Program Files`, `/usr/bin`, etc.)
- Folders containing `..` in the path
- Paths exceeding the OS limit

### Engine version compatibility

If the library's `engine_version` is newer than your editor's, you'll see a
warning in the log:
```
LogBlenderAssetBrowser: Library 'TeamShared' was authored in UE 5.10 — newer than your editor 5.6. Cooked content may not load.
```
You can still browse, but loading individual assets may fail or downgrade.

---

## 10. Blender bridge

### Install the addon

1. Open Blender.
2. **Edit → Preferences → Add-ons → Install...**
3. Pick `Plugins/BlenderAssetBrowser/Resources/BlenderAddon/blender_asset_browser_bridge.py`.
4. Tick the checkbox to enable it.

### Send from Blender to UE

In Blender, select your meshes → **File → Export → Send to UE Asset Browser**.
The addon writes `<mesh>.fbx` and `<mesh>.meta.json` into
`<YourProject>/Saved/BlenderAssetBrowser/Exchange/incoming/`.

The plugin's DirectoryWatcher picks up the `.meta.json` (Blender writes it
last) and imports the FBX as a new `StaticMesh` at the path specified in
`target_ue_path` (e.g. `/Game/Imported/SM_Tree_01`).

### Round-trip

Right-click an existing UE asset in Content Browser → **Blender Asset
Browser → Send to Blender**. The plugin writes `<asset>.fbx` to
`Exchange/outgoing/`. The Blender addon polls that folder on a timer and
opens the file when it appears. Edits in Blender, saves back to
`incoming/`, and UE auto-reimports.

### Safety

- `target_ue_path` is validated: must start with `/Game/` or `/AssetLibraries/`,
  no `..`, no control chars.
- FBX file size is capped at 200 MB.
- The exchange folder is restricted to `Saved/BlenderAssetBrowser/Exchange/`
  — no writing outside.

---

## 11. DCC integration (Substance, Photoshop, Max)

The same reimport mechanism as Blender, generalized.

### Setup

`Settings → Bridge → External applications`:
- **Substance Painter Executable** — `C:\Program Files\Adobe\Adobe Substance 3D Painter\Adobe Substance 3D Painter.exe`
- **Photoshop Executable** — `C:\Program Files\Adobe\Adobe Photoshop 2024\Photoshop.exe`
- **3ds Max Executable** — `C:\Program Files\Autodesk\3ds Max 2024\3dsmax.exe`

### Open source in DCC

Right-click an asset that has a source file (Substance `.spp`, Photoshop
`.psd`/`.psb`, Max `.max`) → **Blender Asset Browser → Open in DCC**.
The plugin:
1. Validates the source path (no `..`, no control chars, no quotes).
2. Launches the matching DCC via `CreateProc` (argv-style, never shell).
3. Registers the source file with the **DCC Reimport Watcher**.

### Auto-reimport on save

The watcher polls source files by mtime. When you save in the DCC, the
watcher detects the mtime change within ~1 second and triggers
`FReimportManager::ReimportAsync` on the associated UE asset. You stay in
the DCC; the UE editor updates in the background.

---

## 12. Snapshots and rollback

A safety net before risky operations — bulk-rename, mass-delete, large
import. Snapshots store full file copies, not diffs, so they're heavy but
trivial to restore.

### Create

`Settings → Snapshots → Create Snapshot` (or call `FSnapshotManager::CreateSnapshot`
from a script). Pick the absolute paths to snapshot. The plugin copies them
into `Saved/BlenderAssetBrowser/Snapshots/<timestamp>/` with a `manifest.json`
listing the files.

### Restore

`Settings → Snapshots → Restore` → pick a snapshot row → **Restore**.
The plugin copies the files back to their original locations, overwriting
the current versions.

### Auto-evict

Old snapshots are evicted when total disk use exceeds the cap (configurable
in Settings → Snapshots → Storage cap, default 5 GB). Oldest snapshots go
first.

---

## 13. Fab update tracker

Watches your Fab-sourced assets for new versions on fab.com.

### Enable

`Settings → Updates → Enable Fab scraper` (off by default to avoid surprise
network traffic).

### How it works

Once a day (or manually triggered), the plugin:
1. Picks every asset with `source_type='fab'` and a valid `source_url`.
2. Fetches the Fab page (HTTPS only, 10s timeout, 2 MB body cap, 4 in flight).
3. Parses the version number from the page HTML.
4. If newer than `source_version`, sets `update_state='update_available'` and
   stores the new version in `latest_version`.

The **Outdated** smart catalog auto-populates with these assets.

### Safety

- Cross-host redirects are blocked mid-request (no MITM via redirect).
- Only `fab.com` and `www.fab.com` are accepted as start hosts.
- HTTP requests are HTTPS-only.

### Discord webhook

`Settings → Updates → Discord webhook URL` accepts a Discord webhook URL.
When an update is detected, the plugin posts:

```
:package: Update available for <Asset Name>: 1.0.3 → 1.1.0
```

The URL must start with `https://discord.com/api/webhooks/` or
`https://discordapp.com/api/webhooks/`. Anything else is rejected.

---

## 14. Quick Picker

A keyboard-driven overlay for inserting an asset into the active viewport.

### Open

**Ctrl+Shift+Space** (default). Rebind in `Settings → Quick Picker → Hotkey`.

### Use

1. Type to fuzzy-search the library.
2. Arrow up/down to navigate, Enter to commit.
3. The selected asset is **marked as recent** and (in a future build) spawned
   at the viewport cursor. Today, Enter logs the selection and you place
   manually.

ESC closes the overlay without selecting.

---

## 15. Naming linter

Catches inconsistent prefixes and reports/fixes them in bulk.

### Default rules (18)

| Class | Required prefix |
|---|---|
| `StaticMesh` | `SM_` |
| `SkeletalMesh` | `SK_` |
| `Material` | `M_` |
| `MaterialInstanceConstant` | `MI_` |
| `MaterialFunction` | `MF_` |
| `Texture2D` | `T_` |
| `Blueprint` | `BP_` |
| `ParticleSystem` | `PS_` |
| `NiagaraSystem` | `NS_` |
| `AnimationAsset` | `A_` |
| `SoundCue` | `SC_` |
| `SoundWave` | `SW_` |
| `DataAsset` | `DA_` |
| `DataTable` | `DT_` |
| `CurveBase` | `CV_` |
| `Widget` | `WBP_` |
| `Material instance dynamic` | `MID_` |
| `RenderTarget` | `RT_` |

### Run a scan

`Settings → Naming Linter → Scan`. Pick the scope (whole library, a catalog,
a specific path). The plugin lists every asset whose name violates its
class's prefix.

### Batch rename

Pick rows, click **Rename Selected**. The plugin uses
`FAssetTools::RenameAssets` so references are updated automatically.

---

## 16. Dependency-aware copy

Copying a `Material` without its textures is a classic mistake. This feature
walks the package dependency closure and copies everything.

### Trigger

Right-click asset → **Blender Asset Browser → Copy with Dependencies...**
A modal opens showing:
- The dependency closure (depth-bounded at 10 000 packages).
- The total bytes that would be copied.

Three buttons:
- **Copy with dependencies** — duplicates the full graph under a chosen
  destination folder via `AssetTools::DuplicateAsset`.
- **Copy this asset only** — just the root, references will be broken.
- **Cancel** — close.

---

## 17. VCS sync (.assetlib sidecar)

Sharing the SQLite library across team members via Git requires a sidecar
because SQLite binary files don't merge.

### Export

`Settings → VCS → Export sidecar`. The plugin writes a JSON file (the
`.assetlib` sidecar) containing every catalog, tag, asset row,
`asset_catalogs` and `asset_tags` relations, and optionally
`asset_embeddings_index` (without the heavy blobs).

### Import (three-way merge)

In Git, set up the merge driver once:

```bash
git config merge.assetlib.driver "python Resources/MergeDriver/assetlib_merge.py %A %O %B"
echo "*.assetlib merge=assetlib" >> .gitattributes
```

After that, when two people edit the sidecar at once, Git invokes the merge
driver which:
- Adds rows present in either side.
- Removes rows present in `O` but missing on both sides.
- Conflicts on rows edited on both sides → prints a `<<<<<<<` block, you
  resolve manually.

See `docs/TEAM_LIBRARY.md` for the full Git LFS + Perforce setup.

---

## 18. Project Settings reference

`Edit → Project Settings → Plugins → Blender Asset Browser`.

| Group | Setting | Default | Purpose |
|---|---|---|---|
| General | DB path | `Saved/BlenderAssetBrowser/library.db` | Where the SQLite library lives. Project-relative or absolute. |
| General | Theme | `Dark` | UI theme (Dark / Light). |
| Hotkeys | Quick Picker | `Ctrl+Shift+Space` | Opens the lightweight overlay. |
| AI | Confidence threshold | 0.30 | Min sigmoid-scored cosine to assign a tag automatically. |
| AI | Sigmoid scale | 4.0 | SigLIP2 calibration factor. Don't change unless you know what you're doing. |
| AI | AI tag review before apply | off | When on, opens the review modal before storing AI tags. |
| Libraries | Library list | empty | One row per mounted external library. |
| Bridge | Blender executable | (auto-detect) | Path to `blender.exe`. |
| Bridge | Exchange subfolder | `BlenderAssetBrowser/Exchange` | Relative to `Saved/`. |
| Bridge | Substance Painter exe | (empty) | Required for `.spp` opening. |
| Bridge | Photoshop exe | (empty) | Required for `.psd`/`.psb` opening. |
| Bridge | 3ds Max exe | (empty) | Required for `.max` opening. |
| Updates | Enable Fab scraper | off | Opt-in. Daily HTTPS polls. |
| Updates | Discord webhook URL | (empty) | `https://discord.com/api/webhooks/...` only. |
| Updates | Update poll interval (hours) | 24 | How often the scraper runs. |
| Snapshots | Storage cap (MB) | 5120 | Total size cap. Oldest evicted on overflow. |

---

## 19. Troubleshooting

### Plugin doesn't load

Check the Output Log filtered by `LogPluginManager`. Common causes:

- **EngineVersion mismatch:** the `.uplugin` doesn't constrain engine
  versions, but if you opened a project built for a different version
  without rebuilding, UE may refuse to load. Solution: rebuild
  (`Build.bat YourProjectEditor Win64 Development -NoUBA`).
- **DLL load failure:** look for `Failed to load module` in the log. Most
  often `onnxruntime.dll` or `assimp-vc143-mt.dll` is missing from
  `Binaries/Win64/`. Rebuild — the `.Build.cs` files copy them as
  `RuntimeDependencies`.

### SigLIP model fails hash check

```
LogAITagging: Error: SigLIPInference: model hash mismatch.
  expected=defa6894832bd83a5eeb6936cca2ef58e90998aa
  actual=  <other>
  -> refusing to load.
```

The on-disk `siglip2_vision_fp16.onnx` is different from what the plugin
expects. Causes:
- Git LFS didn't pull the real bytes — only the LFS pointer. Run
  `git lfs pull` in the plugin folder.
- File got corrupted. Re-clone the plugin.
- You replaced the model with a different one. Update `kExpectedHash` in
  `SigLIPInference.cpp` to the new SHA-1, rebuild.

### Thumbnails are blank in grid view

This was a bug in early builds (`FSlateDynamicImageBrush::CreateWithImageData`
ignored the file path). Fixed in current code — grid uses the same
`FAssetThumbnail` + `FAssetThumbnailPool` as the Content Browser.

If you still see blanks:
- Click **Refresh** in the toolbar.
- Delete `Saved/BlenderAssetBrowser/PreviewCache/` to force regeneration.

### Auto-tag freezes the editor

Should not happen with current code — AutoTag is wrapped in two
`FScopedSlowTask` phases (Render and Infer) and the inference runs on a
worker thread. You'll see a progress dialog with a Cancel button.

If you see an actual freeze, please open an issue with the Output Log
attached.

### Catalog drag-drop does nothing

Drag-drop fires `FCatalogManager::Reparent`, which rejects the move if it
would create a cycle. Check the log:
```
LogBlenderAssetBrowser: Reparent rejected: would create cycle (cat=5 parent=12)
```
Solution: drop on a different parent that isn't a descendant of the moved
catalog.

### Fab scraper does nothing

Check that:
- **Enable Fab scraper** is on in Settings.
- The asset's `source_type='fab'` and `source_url` is a valid `https://fab.com/...` URL.
- The 24-hour poll interval hasn't been reached yet — force a manual poll
  via the toolbar.

### Discord webhook silent

Check that the URL matches the strict allowlist (`https://discord.com/api/webhooks/...`).
Anything else — including `http://`, custom subdomains, or other webhook
services — is rejected.

Also check the log for HTTP status codes:
```
LogUpdateChecker: Discord webhook POST returned status 404
```
404 usually means the webhook was deleted in Discord.

### Reimport-on-save doesn't trigger

The DCC reimport watcher checks `mtime` once per tick. Some DCCs (Substance
Painter especially) write a temp file and rename it on save. If the mtime
isn't updated atomically, the watcher may miss the change. Workaround: wait
2 seconds after saving, the next tick will catch it.

---

## Appendix: Hotkeys reference

| Action | Default hotkey | Where |
|---|---|---|
| Quick Picker | Ctrl+Shift+Space | Global, configurable |
| Refresh asset browser | F5 | Asset browser window |
| Toggle List/Grid | Ctrl+G | Asset browser window |
| Mark selected as Asset | (no default) | Content Browser context menu |
| Auto-tag selected (AI) | (no default) | Content Browser context menu |

---

## Appendix: File locations

| What | Where |
|---|---|
| Plugin source | `YourProject/Plugins/BlenderAssetBrowser/` |
| SQLite library | `YourProject/Saved/BlenderAssetBrowser/library.db` |
| Thumbnail cache | `YourProject/Saved/BlenderAssetBrowser/PreviewCache/` |
| Blender exchange folder | `YourProject/Saved/BlenderAssetBrowser/Exchange/` |
| Snapshots | `YourProject/Saved/BlenderAssetBrowser/Snapshots/` |
| Reference board | `YourProject/Saved/BlenderAssetBrowser/references.json` |
| Tag vocabulary (built-in) | `Plugins/BlenderAssetBrowser/Resources/TagVocabulary/default_tags.json` |
| SigLIP2 model | `Plugins/BlenderAssetBrowser/Resources/Models/siglip2_vision_fp16.onnx` |

---

## Appendix: Security posture (short version)

The plugin handles untrusted inputs at several points (mounted libraries,
imported FBX files, Fab page HTML, Blender JSON, Discord URLs). Defenses
in place:

- **SQL:** prepared statements only. Zero string concatenation into SQL.
- **Paths:** all FS-touching helpers run `IsPathInsideRoot` / `IsSafeUEPath`
  + `..` rejection.
- **JSON:** size cap (50 MB) before deserialization. Depth cap enforced by
  UE's JSON parser.
- **FBX/Assimp:** 200 MB file cap, 2 M vertex/face cap.
- **HTTP:** HTTPS-only, cross-host redirect block, 10s timeout, 2 MB body
  cap, 4 in-flight throttle.
- **DLL loading:** allowlist of system folders blocks loading from
  Windows/Program Files.
- **ONNX model:** SHA-1 hash check before loading.
- **SQLite:** compiled with `DQS=0`, `OMIT_LOAD_EXTENSION`,
  `OMIT_AUTHORIZATION`, `trusted_schema=OFF`.

See the source if you need to audit specifics.

---

If you hit something not covered here, open an issue at
[github.com/Andrew-modeler/BlenderAssetBrowser/issues](https://github.com/Andrew-modeler/BlenderAssetBrowser/issues).
