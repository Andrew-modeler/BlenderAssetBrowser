# BlenderAssetBrowser — TODO

What's left, organized by priority and effort.

## Tier 1 — small fixes (≤ 1 day each, no external blockers)

### 1.1 Wire Quick Picker to a hotkey ✓ DONE
Registered as `Ctrl+Shift+Space` via `FAssetBrowserCommands` + appended to the
Level Editor's `GetGlobalLevelEditorActions()`. Runtime log confirms.

### 1.2 "Auto-tag selected" command ✓ DONE
"Auto-tag (AI)" entry in Content Browser context menu. End-to-end:
thumbnail → SigLIP embedding → `asset_embeddings` blob.

### 1.3 Spawn-at-cursor for Quick Picker ✓ DONE
`SQuickPicker::Spawn` now spawns StaticMesh actors near the active level
viewport camera. Reuses `GCurrentLevelEditingViewportClient`.

### 1.4 Catalog drag-drop in the tree ✓ DONE
`STableRow` for catalog rows exposes `OnDragDetected` (creates
`FCatalogDragDropOp` carrying the catalog id), `OnAcceptDrop` (calls
`FCatalogManager::Reparent`, which already does cycle detection), and
`OnCanAcceptDrop` (only accepts our payload).

### 1.5 Asset list grid view ✓ DONE
Toolbar "List/Grid" button switches the content area between `SListView`
and `SWrapBox`. Grid cards render real **thumbnails** loaded via
`FPreviewCacheManager::GetOrRender` + `FSlateDynamicImageBrush`. Label-
only fallback when the asset's UObject can't be resolved.

### 1.6 Catalog filter for asset list ✓ DONE
Implemented via `UAssetLibrarySubsystem::GetAssetsInCatalog`. When a catalog
is selected, the asset list shows only members.

### 1.7 Inspector — full metadata ✓ DONE
Inspector now shows: name/type/path/rating, technical block (tris, verts,
LODs, max texture res, materials, collision, disk size, engine version),
source block (origin, pack, author, license, version, URL), update state,
tags (comma-separated), and notes.

### 1.8 Settings hotkey customization ✓ DONE
`UAssetLibrarySettings` exposes `QuickPickerKey` + Ctrl/Shift/Alt flags.
`FAssetBrowserCommands::RegisterCommands` reads them at registration time.

---

## Tier 2 — medium features (1-3 days each, mostly self-contained)

### 2.1 Pre-compute tag embeddings (unlocks real AI auto-tag)
**Why it matters:** without this, the AI infrastructure is plumbing-only.
With it, the plugin can suggest tags from a thumbnail in ~50 ms. **✓ DONE.**

- Downloaded `text_model_fp16.onnx` (565 MB) once locally.
- `Resources/TagVocabulary/build_embeddings.py` runs the text encoder over
  the 189 built-in tags and emits `default_tag_embeddings.bin` (570 KB).
- Text encoder is **not** shipped; only the `.bin` file is.
- `FTagVocabulary::LoadEmbeddings` parses the file at startup.
- `FContentBrowserMenuExtension::ExecuteAutoTag` now runs the full pipeline:
  thumbnail → embedding → cosine vs. vocab → tags ≥ threshold assigned to
  the asset (with `source='ai'` and confidence).

### 2.2 Naming convention linter ✓ DONE
`FNamingLinter::Scan` + `SNamingLinterWindow` UI. Window opens from a
button on the asset browser toolbar; user picks which violations to fix
and the dialog calls `FAssetTools::RenameAssets` (atomic, fixes references).

### 2.3 Bulk operations ✓ DONE (partial)
Context menu in the Content Browser, when multi-select:
- Bulk: Set Rating (0..5 sub-menu).
- Bulk: Add Tag... (text-entry dialog).

Still missing: batch rename (regex), batch reimport. Those slot in next
to the existing bulk handlers.

### 2.4 Provenance badge ✓ DONE (text-prefix)
Asset list rows now show `[F]` (Fab) / `[M]` (Megascans) / `[L]` (Local) /
`[I]` (Imported) / `[C]` (Custom) / `[?]` (Unknown) before the asset name.
Inspector pane shows full source block. **Icon overlay still pending** —
needs PNG icons in `Content/Icons/`.

### 2.5 "Outdated" smart filter ✓ DONE
Two built-in smart catalogs are auto-created on first DB open:
- **Outdated** (`update:available`) — red `#cc4422`.
- **Source Changed** (`update:source_changed`) — orange `#dd9911`.

Smart-query → SQL is wired through the `SearchEngine` query parser.

### 2.6 Library import — engine-version compat warning ✓ DONE
`FExternalLibraryMount::Mount` looks for `.assetlib_meta.json` in the
mounted folder, reads its `engine_version`, and logs a `Warning` when it's
newer than the running editor. Gated by `bWarnOnNewerEngineVersion` in
settings.

### 2.7 Drag from OS Explorer into library ✓ DONE
`SAssetBrowserWindow` overrides `OnDragOver` + `OnDrop` to handle
`FExternalDragOperation`. Files import into `/Game/ImportedFromBrowser/`
via `AssetTools::ImportAssetsAutomated`. The asset list refreshes
automatically.

---

## Tier 3 — large features (3-7 days each)

### 3.1 External FBX preview (Phase 2.2) ✓ DONE
Assimp 6.0.5 built from source with MSVC 14.44 (VS 17.14) — pass
`-T host=x64,version=14.44.35207` + `VCToolsVersion=14.44.35207` to
override CMake's default toolchain pick.

`FExternalPreviewRenderer` loads an FBX/OBJ/GLTF/STL via Assimp with
strict caps (max 2M verts, 2M faces, max-weights 4), flattens to a
transient `UStaticMesh`, then routes through `FAssetPreviewRenderer`
to use UE's ThumbnailTools — no Filament needed.

DLL (`assimp-vc143-mt.dll`, ~1.7 MB) is deployed to `Binaries/Win64/`
via `RuntimeDependencies`. The plugin reports BAB_HAS_ASSIMP=1 at compile
time and the editor launches with Assimp linked.

**Polish ideas:**
- Wire OS-Explorer drop of .fbx into the asset browser to call
  ExternalPreviewRenderer for an in-place preview (no project import yet).
- Add material/texture transfer from the aiScene to the transient mesh.

### 3.2 Fab update scraper ✓ DONE
`FFabUpdateChecker` runs a daily HTTPS check (opt-in via
`bEnableFabUpdateCheck`). HTTPS-only, 2 MB body cap, 10 s timeout,
4-in-flight throttle, User-Agent identifies plugin. Tolerant version
pattern scanner (no HTML parser), component-wise compare with
`source_version`. Newer → `update_state='update_available'` and
`latest_version` populated.

(historical reference for the scraper plan:
- For each asset with `source_type='fab'` and a valid `source_url`, fetch
  the URL via UE's `IHttpRequest` (HTTPS only).
- Parse the response HTML to extract the latest version + changelog.
- Compare with stored `source_version`. Mark `update_state='update_available'`
  if newer.
- Run once per editor session, then daily via a background ticker.
- **Risk:** fab.com may change page structure or block scraping. Wrap the
  parser in try/catch and log "unable to determine version" rather than
  crash.

### 3.3 Pose Library ✓ DONE (scaffolding)
`FPoseLibrary::CapturePose` captures local bone transforms keyed by bone
name into a JSON blob stored as a `PoseAsset`-type row. `GetAll` / `DeletePose`
work. `ApplyPose` parses the JSON and validates bone matching — **actual
in-viewport apply requires a Persona AnimNode hook** (Tier 5).

### 3.4 Actor collections as assets ✓ DONE (scaffolding)
`FCollectionManager::CreateFromActors` packs a selection into the existing
`collections` + `collection_items` tables with per-actor relative transforms
(vs. group centroid). `SpawnIntoWorld` replays. **Content Browser UI menu
entry and viewport-drop integration are the polish step.**

### 3.5 Material presets ✓ DONE (capture)
`FMaterialPresetManager::CaptureFromMIC` extracts scalar / vector / texture
parameter values from a `UMaterialInstanceConstant` into a JSON delta blob.
Stored in the asset row's `notes` field with the `PRESET_v1` header. **A
proper preset table (schema v3) and UI for applying presets is the polish
step.**

### 3.6 Dependency-aware copy ✓ DONE (helper)
`FDependencyCopyHelper::Compute` builds the transitive package closure
via AssetRegistry, depth-bounded to 10000. `CopyClosure` duplicates the
graph via `AssetTools::DuplicateAsset`. `CopySingle` is the "no deps"
variant. **UI dialog (show closure size, choose mode) is next polish.**

### 3.7 Snapshot + rollback ✓ DONE
`FSnapshotManager::CreateSnapshot` flat-copies absolute paths into
`Saved/BlenderAssetBrowser/Snapshots/{ts}/`. `RestoreSnapshot` copies
back. `EvictOldest` enforces a disk cap. **UI integration (inspector
rollback button) is the next polish step.**

### 3.8 In-editor reference board
- A docked panel with reference images. Drag from web / desktop.
- Per-asset references (pin to inspector) or free-floating board.

---

## Tier 4 — V2 features (1-2 weeks each)

### 4.1 Team / shared library (Phase 5.1)
- Library lives in Git LFS or Perforce. The `.assetlib` is human-readable
  JSON (already implemented), so VCS merges work.
- Custom merge driver `Resources/MergeDriver/assetlib_merge.py` already
  ships — needs Git config docs in this repo's README.
- Optional: P4 lock integration so users can claim a library asset for edit.

### 4.2 Versioning within library (Phase 5.2)
- Per-asset snapshot history kept in `{Library}/.versions/`.
- "Snapshot" button in inspector; "Roll back to version N" command.
- Diff view of two thumbnails side-by-side.

### 4.3 Global find references (Phase 5.3)
- Cross-project index: which UE projects use this library asset and where.
- Requires registering each user's project list in a shared config.
- "Used in 3 projects" badge.

### 4.4 DCC integration beyond Blender (Phase 4.6)
**✓ DONE (launch only).** `FDCCLauncher::OpenInDCC` opens a file in
Substance Painter / Photoshop / 3ds Max based on extension; exe paths
configurable in Settings. **Two-way reimport bridge for these DCCs is
the next polish step** — reuse the `FBlenderBridgeManager`
`DirectoryWatcher` pattern.

(historical reference:
- Same pattern as Blender bridge but for Substance Painter (.spp),
  Photoshop (.psd), 3ds Max (.max).
- Reuse `DirectoryWatcher` + manifest schema.)

### 4.5 AI tagging UX (Phase 3.1 second pass)
**End-to-end inference now works** (`ExecuteAutoTag` calls SigLIP + scores
against pre-computed vocab + assigns top-N tags). Remaining polish:
- Review panel: "AI suggested these 7 tags with confidence 0.4-0.9.
  Accept which?" — currently we just write all above threshold.
- Per-asset confidence threshold override (global threshold is in Settings).
- Tag-vocabulary editor: add/remove custom tags. Recomputing embeddings
  for added custom tags requires bundling the text encoder OR a one-time
  download-and-run flow.

---

## Tier 5 — polish / nice-to-haves

- Light / dark / compact UI themes via `FSlateStyleSet` registration.
- Font-size setting (referenced in research as a community pain point).
- Sound previews with play button + waveform thumbnail.
- Niagara preview gif loop in the grid.
- Material preview-mesh dropdown (Sphere / Cube / Plane / Cylinder / Custom).
- Animation hover-scrubber.
- "Recent searches" panel.
- Discord webhook for update notifications.

---

## Known issues / gotchas to keep in mind

1. **UE 5.6 UBA crashes rc.exe** on this machine — always pass `-NoUBA`.
   Logged in memory under `feedback_build_workarounds.md`.

2. **UE 5.6 ships its own onnxruntime.dll** (v1.20) inside the NNE plugin.
   Our `AITagging.Build.cs` deliberately links against ORT **1.20.1** so the
   Windows loader resolves to UE's already-loaded DLL. Bumping ORT version
   without checking what UE bundles will crash `Ort::Env::Env()`.

3. **`.uplugin` `EngineVersion`** field blocks unattended loads with an
   "incompatible plugin" dialog. We deliberately omit it.

4. **`UnrealEditor-Cmd.exe`** sometimes lingers after `-ExecCmds="quit"` and
   locks our DLLs. Kill it before rebuilds:
   `Get-Process *UnrealEditor-Cmd* | Stop-Process -Force`.

5. **Filament prebuilt** is 762 MB unzipped (most of it is /MTd /MDd debug
   variants). We keep only `lib/x86_64/md/` = 430 MB. When integrating
   Filament linking, link only the libs you actually use; the full set
   pulls in ~38 static libs.

6. **Assimp release zip from GitHub** is runtime-only (DLL + PDB). No
   import library, no headers. To link from C++ you must either build from
   source or use vcpkg. See `ThirdParty/assimp/README.md`.

7. **`USTRUCT` requires `.generated.h`** — our `AssetLibraryTypes` types
   are plain `struct` because they don't need UE reflection. Don't add
   `USTRUCT()` without also adding `#include "*.generated.h"` and a UHT
   manifest entry — otherwise the linker dies on missing `StaticStruct`.

8. **`BLENDERASSETBROWSER_API`** must be on every type/function used from
   outside the BlenderAssetBrowser module (e.g. `BAB::FRow`). Missing it
   shows up as `LNK2019: unresolved external symbol` at editor-module link.

---

## Source-file inventory (by responsibility)

| File | Responsibility | Status |
|---|---|---|
| `BlenderAssetBrowser/AssetLibraryDatabase.{h,cpp}` | SQLite wrapper, prepared statements, WAL | DONE |
| `BlenderAssetBrowser/AssetLibrarySubsystem.{h,cpp}` | CRUD API for catalogs/assets/tags/libs/favs | DONE |
| `BlenderAssetBrowser/AssetLibraryTypes.{h,cpp}` | POD types + Validate() input guards | DONE |
| `BlenderAssetBrowser/AssetLibrarySettings.{h,cpp}` | Project Settings, input-validated paths | DONE |
| `BlenderAssetBrowser/CatalogManager.{h,cpp}` | Reparent with cycle detection | DONE |
| `BlenderAssetBrowser/SearchEngine.{h,cpp}` | FTS5 + structured query parser | DONE |
| `BlenderAssetBrowser/ExternalLibraryMount.{h,cpp}` | Mount external folder as virtual content path | DONE |
| `BlenderAssetBrowser/ProvenanceTracker.{h,cpp}` | Fab / Megascans / Local detection | DONE |
| `BlenderAssetBrowser/AssetLibrarySidecar.{h,cpp}` | JSON `.assetlib` export/import for VCS | DONE |
| `BlenderAssetBrowser/NamingLinter.{h,cpp}` | Prefix-rule scanner | DONE (scanner; UI pending) |
| `BlenderAssetBrowserEditor/BlenderAssetBrowserEditorModule.{h,cpp}` | Tab spawner, menu setup, hotkey binding | DONE |
| `BlenderAssetBrowserEditor/AssetBrowserCommands.{h,cpp}` | FUICommandList — Ctrl+Shift+Space | DONE |
| `BlenderAssetBrowserEditor/ContentBrowserMenuExtension.{h,cpp}` | "Mark as Asset" / Unmark / Auto-tag (AI) | DONE |
| `BlenderAssetBrowserEditor/SAssetBrowserWindow.{h,cpp}` | Main window UI with catalog filter + full inspector | DONE (grid view pending) |
| `BlenderAssetBrowserEditor/SQuickPicker.{h,cpp}` | Spotlight overlay, hotkey-bound | DONE (spawn-at-cursor pending) |
| `BlenderAssetBrowserEditor/AssetBrowserDragDrop.{h,cpp}` | Viewport drop with material slot | DONE |
| `AssetPreview/AssetPreviewRenderer.{h,cpp}` | uasset thumbnails via ThumbnailTools | DONE |
| `AssetPreview/PreviewCacheManager.{h,cpp}` | SHA1-keyed disk cache, LRU | DONE |
| `AssetPreview/ExternalPreviewRenderer.{h,cpp}` | External FBX preview | STUB — Assimp blocked |
| `AITagging/AITaggingModule.{h,cpp}` | Module + startup probe | DONE |
| `AITagging/TagVocabulary.{h,cpp}` | Load built-in tags JSON | DONE |
| `AITagging/SigLIPInference.{h,cpp}` | ORT C++ session, FP16/FP32, cosine | DONE (vocab-embeddings missing) |
| `BlenderBridge/BlenderBridgeManager.{h,cpp}` | Exchange folder + DirectoryWatcher | DONE |
| `UpdateChecker/LocalSourceWatcher.{h,cpp}` | mtime/hash watcher | DONE |
| `UpdateChecker/FabUpdateChecker.{h,cpp}` | Daily HTTP scraper | NOT WRITTEN |
| `Resources/BlenderAddon/blender_asset_browser_bridge.py` | Two-way Blender addon | DONE |
| `Resources/MergeDriver/assetlib_merge.py` | Git three-way merge | DONE |
| `Resources/TagVocabulary/default_tags.json` | 200+ built-in tags | DONE |
| `Resources/TagVocabulary/default_tag_embeddings.bin` | Pre-computed text embeddings | NOT YET |
| `Resources/Models/siglip2_vision_fp16.onnx` | SigLIP2 vision encoder (178 MB) | DONE |
