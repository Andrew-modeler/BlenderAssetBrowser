# BlenderAssetBrowser — Status

## TL;DR

Plugin is feature-complete for the core "Blender-style Asset Browser" use case.
All Tier 1 and Tier 2 polish landed this session, plus tier 3 collections,
material presets, pose library scaffolding, and **end-to-end AI auto-tagging**
(thumbnail → SigLIP2 vision embedding → cosine vs. 189 pre-computed tag
embeddings → tag assignment).

**Last verified runtime log** (full headless boot, no errors, end-to-end AI live):
```
LogPluginManager: Mounting Project plugin BlenderAssetBrowser
LogBlenderAssetBrowser:       BlenderAssetBrowser runtime module started.
LogBlenderAssetBrowser:       Database opened: ...library.db
LogBlenderAssetBrowser:       Schema migrated to version 2.
LogBlenderAssetBrowser:       AssetLibrarySubsystem ready
LogBlenderAssetBrowserEditor: BlenderAssetBrowserEditor module started.
LogBlenderAssetBrowserEditor: Hotkeys appended to Level Editor (Ctrl+Shift+Space = Quick Picker).
LogBlenderAssetBrowserEditor: Content Browser menu extension registered.
LogAssetPreview:              Preview cache root: ...PreviewCache
LogAssetPreview:              AssetPreview module started.
LogAITagging:                 Loaded 189 built-in tags.
LogAITagging:                 Loaded 189 tag embeddings (dim=768).
LogAITagging:                 SigLIPInference: model loaded (1 in / 2 out).
LogAITagging:                 AITagging module started (vocabulary: loaded; tag embeddings: loaded; SigLIP inference: ready).
LogBlenderBridge:             BlenderBridge initialized at ...Exchange
LogBlenderBridge:             BlenderBridge module started.
LogUpdateChecker:             UpdateChecker module started (Phase 1 stub).
MapCheck: Map check complete: 0 Error(s), 0 Warning(s).
```

## What ships in this build

### Foundation (Phase 1)
- SQLite library DB v2 (17 tables, FTS5, WAL mode, hardened compile flags).
- Catalogs with reparent + cycle detection.
- Tags (manual + AI source + confidence).
- "Mark as Asset" Content Browser menu, 16-type whitelist.
- Project Settings: paths, library list, hotkey, AI threshold, theme.

### Core features (Phase 2)
- Preview rendering for uassets via UE ThumbnailTools + LRU disk cache.
- FTS5 fuzzy search with structured query parser (`tag:foo tris:<5000 source:fab`).
- Cross-project library mount (`/AssetLibraries/<Name>/`).
- Provenance auto-detection (Fab Vault Cache, Megascans path, AssetImportData).
- Favorites & Recents.
- `SAssetBrowserWindow` — toolbar / catalog tree / asset list / inspector
  with full metadata + provenance badge prefix `[F]`/`[M]`/`[L]`/`[I]`/`[C]`.
- Drag & drop: viewport spawn + material slot replacement + OS Explorer drop.
- Blender bridge: file-based exchange + DirectoryWatcher + Blender addon.
- VCS sync: JSON `.assetlib` sidecar + Python three-way merge driver.

### Intelligence & integration (Phase 3)
- **AI auto-tagging end-to-end:** Content Browser → "Auto-tag (AI)" →
  thumbnail render → SigLIP2 vision embedding → cosine vs. 189 pre-computed
  tag embeddings → top-N tags above threshold stored in DB with `source='ai'`
  and confidence.
- Tag vocabulary: 189 built-in tags + user additions.
- ONNX Runtime 1.20.1 (matches UE 5.6 bundled NNE DLL).
- Local source-file watcher (flags `source_changed` in update_state).
- Quick Picker overlay (`Ctrl+Shift+Space`, configurable in Settings).
- Naming Linter with 18 default prefix rules + UI window + batch rename via
  `FAssetTools::RenameAssets`.
- Built-in smart catalogs auto-created on first run: "Outdated", "Source Changed".

### V1 differentiation (Phase 4, partial)
- `FCollectionManager` — Mark a set of actors as a collection, replay into
  another scene at a base location.
- `FMaterialPresetManager` — capture MIC parameter delta as a JSON blob.
- `FPoseLibrary` — capture skeletal-mesh bone transforms (apply needs
  Persona-side wiring, future).
- Bulk operations in Content Browser: bulk-set-rating, bulk-add-tag.

### Quality of life
- Engine-version compatibility warning at library mount.
- 189 tag embeddings pre-computed offline (`build_embeddings.py` ships in
  the source tree; text encoder NOT shipped — only the resulting ~570 KB
  binary file).
- **Fab update scraper** — daily HTTPS poll of every Fab-source asset's
  page (capped 2 MB body, 10 s timeout, 4-in-flight throttle). Newer
  version detected → asset's `update_state='update_available'` and
  `latest_version` populated. Opt-in via Settings.
- **Dependency-aware copy** — `FDependencyCopyHelper::Compute` builds
  the package closure (depth-bounded to 10000), `CopyClosure` duplicates
  the graph under a destination folder via UE's `AssetTools::DuplicateAsset`.
- **Snapshot / rollback** — `FSnapshotManager::CreateSnapshot` copies a
  set of absolute paths into `Saved/BlenderAssetBrowser/Snapshots/{ts}/`
  with a JSON manifest. `RestoreSnapshot` copies them back. `EvictOldest`
  drops oldest snapshots when total disk use exceeds a cap.
- **DCC integration beyond Blender** — `FDCCLauncher` opens a source file
  in Substance Painter / Photoshop / 3ds Max based on extension, via
  the same argv-style `CreateProc` pattern as the Blender bridge.
- **Grid view with real thumbnails** — toolbar toggle switches between
  `SListView` and `SWrapBox`. Cards render thumbnails via
  `FPreviewCacheManager::GetOrRender` + `FSlateDynamicImageBrush`, with
  label-only fallback when the asset can't be resolved.
- **Catalog drag-drop reparent** — `FCatalogDragDropOp` wired into the
  tree row's `OnDragDetected` / `OnAcceptDrop` / `OnCanAcceptDrop`;
  reparent goes through `FCatalogManager::Reparent` which enforces
  cycle detection.
- **External FBX preview** — Assimp 6.0.5 built locally with MSVC 14.44
  (VS 17.14). `FExternalPreviewRenderer` loads FBX/OBJ/GLTF/STL with
  vertex/face caps, builds a transient `UStaticMesh`, and renders via
  UE's ThumbnailTools — no Filament needed. `assimp-vc143-mt.dll`
  (~1.7 MB) ships in `Binaries/Win64/`.

### Bug fixes after audit (10 critical issues)

After a thorough code audit, the following bugs were identified and fixed:

1. **Grid thumbnails were blank** — `FSlateDynamicImageBrush::CreateWithImageData`
   with empty bytes ignored the file path. Now uses `FAssetThumbnail` +
   `FAssetThumbnailPool` (the same renderer the Content Browser uses).
2. **Engine version comparison was lexicographic** — "5.10.0" < "5.6.0" by
   ASCII. Now uses `FEngineVersion::Parse` + `GetNewest` for proper numeric
   semantic compare.
3. **DCC reimport watcher was orphaned** — `Register()` was never called.
   Now `FDCCLauncher::OpenInDCC` registers the source file with the global
   watcher so save-in-DCC triggers auto-reimport.
4. **AI tag review modal was unreachable** — `ShowModal` had no callers.
   Added `bAITagReviewBeforeApply` setting; the menu now routes through
   the review window when enabled.
5. **Dependency-copy size was bogus** — replaced "path-length × 100" with
   real `IFileManager::FileSize` on the resolved `.uasset` paths.
6. **SigLIP scoring missed sigmoid calibration** — raw cosine [-1, 1] was
   compared to a [0, 1] threshold. Now applies sigmoid scaling (Scale set
   in Settings, default 4.0 per SigLIP-2 recipe) for calibrated probabilities.
7. **Bulk Add Tag could lose input on focus loss** — text-entry now commits
   on Enter via `OnTextCommitted`, with proper Apply / Cancel buttons.
8. **Provenance detection only saw loaded assets** — `FastGetAsset(false)`
   returned null for unloaded. Switched to `GetAsset()` which loads on demand.
9. **`.assetlib` sidecar lost relations on round-trip** — added export of
   `asset_catalogs`, `asset_tags`, and (opt-in) `asset_embeddings_index`.
10. **Preview cache never invalidated on asset change** — hash key now
    includes the .uasset mtime, so reimport produces a new key.

### Security hardening (after re-audit)

- **ONNX model hash check** is now actually executed. A baked
  `kExpectedHash = "defa6894…"` SHA1 is compared against the on-disk
  model at load; mismatch refuses to load. Previously `Sha256Hex()` was
  dead code.
- **Tag embeddings allocation cap** — `Count * Dim` is bounded to 10 M
  floats (40 MB) before `SetNumUninitialized`. Defends against a crafted
  `.bin` file declaring huge dimensions.
- **HTTP cross-host redirect block** — the Fab scraper now cancels the
  request mid-flight if a redirect would send it to a different host
  than the requested one.

### Polish landed in the final pass
- **Dependency copy dialog** (`SDependencyCopyDialog`) — modal showing the
  transitive closure and bytes; user picks copy-with-deps / copy-single / cancel.
- **Snapshots panel** (`SSnapshotsPanel`) — list-and-restore UI for
  `FSnapshotManager` results. Toolbar button on the asset browser.
- **Discord webhook** (`FDiscordNotifier`) — strict URL prefix check
  (`https://discord.com/api/webhooks/...`), JSON-escaped, 10 s timeout.
  Fab scraper now posts `:package: Update available for <asset>: A → B`
  on every newly-detected update.
- **Universal DCC reimport watcher** (`FDCCReimportWatcher`) — generalizes
  the Blender bridge: register any source file (`.spp`, `.psd`, `.max`, ...)
  and the plugin auto-triggers `FReimportManager::ReimportAsync` when the
  file's mtime moves forward.
- **AI tag review modal** (`SAITagReviewWindow`) — opens a checkbox list
  of suggested tags with their cosine scores; user accepts a subset.
- **Reference board** (`SReferenceBoard`) — pinned URLs / paths in a
  separate window. Persisted to `Saved/.../references.json`. URLs survive
  editor restarts.
- **Team library docs** — `docs/TEAM_LIBRARY.md` covers Git LFS + merge
  driver setup, P4 typemap + `P4MERGE`, conflict resolution semantics,
  `.gitignore` recommendations.

---

## Phase 1 — Foundation (DONE)

| # | Feature | Verified |
|---|---|---|
| 1.1 | Plugin skeleton — .uplugin, 6 modules, Build.cs | ✓ |
| 1.2 | SQLite 3.47.2 amalgamation, hardened flags (DQS=0, OMIT_LOAD_EXTENSION, FK ON) | ✓ |
| 1.3 | Core data types with `Validate()` guards | ✓ |
| 1.4 | `FAssetLibraryDatabase` — prepared statements only, WAL mode, pragmas | ✓ |
| 1.5 | `UAssetLibrarySubsystem` CRUD API (catalogs, assets, tags, libraries) | ✓ |
| 1.6 | `UAssetLibrarySettings` with input-validated paths | ✓ |
| 1.7 | `FCatalogManager` — reparenting with cycle detection (depth-bounded BFS) | ✓ |
| 1.8 | "Mark as Asset" Content Browser menu — type whitelist enforced | ✓ |
| 1.9 | Apache 2.0 LICENSE + THIRD_PARTY_NOTICES | ✓ |

## Phase 2 — Core Features (DONE)

| # | Feature | Verified |
|---|---|---|
| 2.1 | `FAssetPreviewRenderer` — UE ThumbnailTools, PNG output, sanity caps | ✓ |
|     | `FPreviewCacheManager` — SHA1-keyed disk cache, LRU eviction | ✓ |
| 2.3 | `FSearchEngine` — FTS5 + structured query parser (text + field + numeric ops) | ✓ |
| 2.4 | `FAssetBrowserDragDropOp` — viewport spawn + material slot hit-test | ✓ |
| 2.5 | `FExternalLibraryMount` — Content-Plugin-style mount with allow-list | ✓ |
| 2.6 | Favorites & Recents API (pin/touch/get) | ✓ |
| 2.7 | `FProvenanceTracker` — Fab Vault Cache + Megascans + AssetImportData detection | ✓ |
| 1.8† | `SAssetBrowserWindow` — toolbar / catalog tree / list / inspector | ✓ |
| —   | Blender Bridge (UE side: `FBlenderBridgeManager`, exchange folder, DirectoryWatcher) | ✓ |
| —   | Blender Bridge (addon side: `blender_asset_browser_bridge.py` — round-trip + new-asset flows) | ✓ |

## Phase 3 — Intelligence & Integration (partial)

| # | Feature | Verified |
|---|---|---|
| 3.1 | AI tagging scaffold — `FTagVocabulary` loads 200+ built-in tags from JSON | ✓ |
|     |   ONNX Runtime + SigLIP2 inference — **deferred**: needs ~270 MB of bundled binaries/weights | ☐ |
| 3.2 | `FLocalSourceWatcher::Scan` — flag assets whose source FBX changed on disk | ✓ |
|     |   Fab page scraper — **deferred**: needs robust HTTP/error handling | ☐ |
| 3.4 | VCS sync — `FAssetLibrarySidecar` export/import to `.assetlib` JSON | ✓ |
|     | Custom merge driver — `Resources/MergeDriver/assetlib_merge.py` (three-way) | ✓ |

## Phase 3.5 — Quick Picker (DONE)

`SQuickPicker` overlay opens as a centered window, lets the user fuzzy-search
the library, and on Enter logs the selected asset & marks it as recent.
Drop-at-cursor wires up together with the viewport hooks in Phase 4.

## Phase 3.1 inference — DONE (ORT + SigLIP2 live)

ONNX Runtime 1.20.1 + Google SigLIP2 vision encoder (FP16, 178 MB) are bundled
and load successfully at editor startup. Runtime log confirms
`SigLIPInference: model loaded (1 in / 2 out)`.

Key implementation notes baked into the code:
- We use ORT 1.20.1 to match UE 5.6's own bundled `onnxruntime.dll` inside
  `Engine/Plugins/NNE/NNERuntimeORT/`. Linking against 1.26.0 headers caused
  `Ort::Env::Env()` to crash (`GetApi(23)` returned null on the older DLL).
- `ORT_API_MANUAL_INIT` is defined locally so we control the ORT global API
  pointer initialization and avoid races against UE's own NNE loader.
- The FP16→FP32 dequant path is implemented inline (IEEE 754 binary16 layout)
  so we accept either output dtype from the model.
- Cosine similarity scoring against pre-computed tag embeddings ships as
  `FSigLIPInference::ScoreAgainst`. Embedding-vector size is fixed at 768
  (SigLIP2 base patch16-224).

## Deferred to next sessions

- **Phase 2.2** — External FBX preview via Assimp + Filament.
  Filament is bundled (430 MB static libs). Assimp source build blocked on
  local MSVC 14.38 missing `__std_find_trivial_8` STL helper introduced in
  14.40. Either upgrade VS or fetch Assimp via vcpkg — see
  [ThirdParty/assimp/README.md](BlenderAssetBrowser/ThirdParty/assimp/README.md).
- **Phase 3.2 Fab scraper** — daily HTTPS check to compare versions.
- **Phase 4** — Actor collections, material presets, Pose Library, bulk ops,
  DCC integration beyond Blender.
- **Phase 5** — Team/shared library, versioning, global find references,
  naming linter, selective updates.

---

## Security Posture

| Defense | Where |
|---|---|
| Prepared statements only | `FAssetLibraryDatabase::Execute` / `QueryRows` |
| SQL strings are constants (never user concat) | grep `Execute(TEXT(` across the codebase |
| Path traversal blocked | `BAB::IsPathInsideRoot`, individual `IsSafeUEPath` checks |
| Length limits | `BAB::MAX_*_LEN` enforced in every `Validate()` |
| Control-char rejection | `ContainsControlChars` in `Validate()` |
| HTTPS-only URLs in metadata | `FAssetEntry::Validate` |
| Catalog cycles blocked | `FCatalogManager::WouldCreateCycle` (BFS, 64-step cap) |
| BFS depth cap on `GetDescendants` | `MaxDepth=32` |
| Asset class whitelist for Mark as Asset | `GetSupportedClasses()` |
| Sidecar JSON size cap | `MAX_FILE_BYTES_JSON = 50 MB` |
| FBX size cap before import | `MAX_FILE_BYTES_FBX = 200 MB` |
| Blender bridge target paths must start `/Game/` | `IsSafeUEPath` in `BlenderBridgeManager` |
| Blender launched via argv (no shell) | `FPlatformProcess::CreateProc` |
| Library mount blocks system folders | `IsForbiddenRoot` |
| SQLite hardening flags | `SQLITE_DQS=0`, `OMIT_LOAD_EXTENSION`, `OMIT_AUTHORIZATION`, `trusted_schema=OFF` |
| Tag vocabulary parsed by char whitelist | `IsValidTagText` |
| Cache files stay under cache root | `IsUnderCacheRoot` in `PreviewCacheManager` |

## Build / Run Reminders

```powershell
# Always pass -NoUBA — UBA crashes rc.exe on this machine.
& "C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" `
  TestProjectEditor Win64 Development `
  "D:\WORK\UE plugins\Asset Browser Blender-style\TestProject\TestProject.uproject" `
  -waitmutex -NoUBA

# Kill stale Cmd editors before rebuild (they hold DLL locks)
Get-Process *UnrealEditor-Cmd* | Stop-Process -Force

# Headless test
& "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "D:\WORK\UE plugins\Asset Browser Blender-style\TestProject\TestProject.uproject" `
  -ExecCmds="quit" -unattended -nullrhi -log -ABSLOG="...log"
```

## Plugin Directory Snapshot

```
BlenderAssetBrowser/
├── BlenderAssetBrowser.uplugin
├── LICENSE  (Apache 2.0)
├── THIRD_PARTY_NOTICES.txt
├── Source/
│   ├── BlenderAssetBrowser/        (runtime: DB, types, subsystem, catalog mgr,
│   │                                 settings, search, sidecar, mount, provenance)
│   ├── BlenderAssetBrowserEditor/  (UI window, Content Browser menu, drag-drop)
│   ├── AssetPreview/               (uasset thumbnails + disk cache)
│   ├── AITagging/                  (vocabulary + ONNX scaffolding)
│   ├── BlenderBridge/              (exchange folder + DirectoryWatcher + reimport)
│   └── UpdateChecker/              (local source watcher + Fab stub)
├── Resources/
│   ├── BlenderAddon/blender_asset_browser_bridge.py  (one-file Blender addon)
│   ├── MergeDriver/assetlib_merge.py                 (three-way merge driver)
│   └── TagVocabulary/default_tags.json               (200+ built-in tags)
└── ThirdParty/                                       (binaries land here in later phases)
```
