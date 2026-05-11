# BlenderAssetBrowser — Session Context

Read this first when picking the project back up. Self-contained orientation.

---

## What this is

Free Apache-2.0 plugin for Unreal Engine 5.3+ (verified on 5.6). Brings
Blender-Asset-Browser ergonomics to UE: virtual catalogs, cross-project
library, tags, fuzzy search, AI auto-tagging (SigLIP2), Blender bridge,
provenance tracking, snapshots/rollback, Fab update awareness.

Plugin source lives at:
```
D:\WORK\UE plugins\Asset Browser Blender-style\BlenderAssetBrowser\
```

Test project (junction-links the plugin into Plugins/):
```
D:\WORK\UE plugins\Asset Browser Blender-style\TestProject\
```

---

## Status as of the last session

Plugin builds and boots clean in UE 5.6 headless. All 6 modules load, DB
schema migrates to v2, SigLIP2 model passes its hash check, 189 tag
embeddings load, BlenderBridge + DCC reimport watcher live, Fab scraper
opt-in.

**Last verified runtime log** (full headless boot, 0 errors / 0 warnings):

```
LogPluginManager:             Mounting Project plugin BlenderAssetBrowser
LogBlenderAssetBrowser:       BlenderAssetBrowser runtime module started.
LogBlenderAssetBrowser:       Database opened: ...library.db
LogBlenderAssetBrowser:       Schema migrated to version 1.
LogBlenderAssetBrowser:       Schema migrated to version 2.
LogBlenderAssetBrowser:       AssetLibrarySubsystem ready
LogBlenderAssetBrowserEditor: BlenderAssetBrowserEditor module started.
LogBlenderAssetBrowserEditor: Hotkeys appended to Level Editor (Ctrl+Shift+Space = Quick Picker).
LogBlenderAssetBrowserEditor: Content Browser menu extension registered.
LogAssetPreview:              Preview cache root: ...PreviewCache
LogAssetPreview:              AssetPreview module started.
LogAITagging:                 Loaded 189 tag embeddings (dim=768)
LogAITagging:                 SigLIPInference: model loaded (1 in / 2 out).
LogAITagging:                 AITagging module started (vocabulary: loaded; tag embeddings: loaded; SigLIP inference: ready).
LogBlenderBridge:             BlenderBridge initialized at ...Exchange
LogBlenderBridge:             BlenderBridge module started (Blender + DCC reimport watcher live).
LogUpdateChecker:             UpdateChecker module started (Fab scraper enabled by user setting).
```

---

## Build + test loop

```powershell
# Build  — ALWAYS use -NoUBA; UnrealBuildAccelerator crashes rc.exe locally.
& "C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" `
  TestProjectEditor Win64 Development `
  "D:\WORK\UE plugins\Asset Browser Blender-style\TestProject\TestProject.uproject" `
  -waitmutex -NoUBA

# Headless verify — kill stale Cmd editors first (they hold DLL locks)
Get-Process *UnrealEditor-Cmd* | Stop-Process -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse `
  "D:\WORK\UE plugins\Asset Browser Blender-style\TestProject\Saved\BlenderAssetBrowser" `
  -ErrorAction SilentlyContinue
& "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "D:\WORK\UE plugins\Asset Browser Blender-style\TestProject\TestProject.uproject" `
  -ExecCmds="quit" -unattended -nullrhi -log `
  -ABSLOG="D:\WORK\UE plugins\Asset Browser Blender-style\editor_test.log"

# Look for the 16-line boot sequence above, no ERROR lines
Get-Content "D:\WORK\UE plugins\Asset Browser Blender-style\editor_test.log" |
  Select-String "BlenderAssetBrowser|module started|SigLIP|UpdateChecker|ERROR"
```

---

## Critical decisions baked into the codebase (do NOT change without thinking)

| Decision | Reason |
|---|---|
| ORT **1.20.1**, not 1.26 | Matches UE 5.6's bundled `NNERuntimeORT/onnxruntime.dll`. Mismatch makes `Ort::Env::Env()` crash on `GetApi(23) == nullptr`. |
| `ORT_API_MANUAL_INIT` defined locally | Lets us call `Ort::InitApi()` *after* UE's NNE plugin loads, avoiding init order races. |
| No `PublicDelayLoadDLLs` for ONNX | C++ wrapper's static init runs before any manual `GetDllHandle`; delay-load crashes immediately. Plain `PublicAdditionalLibraries` + `RuntimeDependencies` works. |
| `EngineVersion` **omitted** from .uplugin | Otherwise `-unattended` headless boot answers No to the incompat dialog and aborts. |
| `-NoUBA` on every build | UnrealBuildAccelerator crashes rc.exe with code -1 on this machine. Sequential executor builds clean. |
| MSVC **14.44** to compile Assimp from source | Default UBT pick is 14.38, which lacks `__std_find_trivial_8` SIMD STL helpers needed by Assimp 6.0.5. Force with `VCToolsVersion=14.44.35207` + `-T host=x64,version=14.44.35207`. |
| Plain `struct` (no `USTRUCT`) for internal types | UHT not needed; avoids `.generated.h` churn. Save USTRUCT for things that must appear in Project Settings UI. |
| `BLENDERASSETBROWSER_API` / `UPDATECHECKER_API` on cross-module types | Missing the macro produces `LNK2019` at editor-module link. |
| Filament was bundled, then removed | The External FBX preview path now builds a transient `UStaticMesh` and routes through UE ThumbnailTools — Filament added 430 MB of static libs for no benefit. |
| Plain `struct FCatalogEntry` etc. exported via `BLENDERASSETBROWSER_API` | Cross-module C++ usage requires explicit export on Windows DLLs. |

---

## File layout

```
Asset Browser Blender-style/
├── README.md                          Quick navigation
├── PHASE_STATUS.md                    What's built + runtime log + 10-bug audit history
├── TODO.md                            Bookmarked-with-checkmarks backlog
├── CHECKLIST.md                       Threat model + per-phase test plan
├── CONTEXT.md                         This file
├── IMPLEMENTATION_PLAN.md             Original full plan (mostly historical)
├── Asset Browser Blender-style.md     Original research doc
├── docs/TEAM_LIBRARY.md               Git LFS / P4 workflow for shared libraries
│
├── BlenderAssetBrowser/               THE PLUGIN
│   ├── BlenderAssetBrowser.uplugin
│   ├── LICENSE                        Apache 2.0
│   ├── THIRD_PARTY_NOTICES.txt        ONNX (MIT) / SigLIP2 (Apache) / Assimp (BSD-3) / SQLite (PD)
│   ├── Source/
│   │   ├── BlenderAssetBrowser/       Runtime (data layer, 15 .h/.cpp)
│   │   ├── BlenderAssetBrowserEditor/ UI layer (13 .h/.cpp)
│   │   ├── AssetPreview/              Thumbnail rendering (4 .h/.cpp)
│   │   ├── AITagging/                 SigLIP inference (3 .h/.cpp)
│   │   ├── BlenderBridge/             Blender + DCC integrations (4 .h/.cpp)
│   │   └── UpdateChecker/             Fab/local update tracking (4 .h/.cpp)
│   ├── Resources/
│   │   ├── BlenderAddon/blender_asset_browser_bridge.py
│   │   ├── MergeDriver/assetlib_merge.py
│   │   ├── Models/siglip2_vision_fp16.onnx        (~178 MB)
│   │   └── TagVocabulary/
│   │       ├── default_tags.json
│   │       ├── default_tag_embeddings.bin         (~570 KB, 189×768 float32)
│   │       └── build_embeddings.py                (Python; runs locally with text encoder)
│   ├── ThirdParty/
│   │   ├── assimp/                    Headers + lib + dll (built locally, MSVC 14.44)
│   │   └── onnxruntime/extracted/onnxruntime-win-x64-1.20.1/
│   └── Config/
│
└── TestProject/                       UE 5.6 host project (Plugins/BlenderAssetBrowser is a junction)
    ├── TestProject.uproject
    └── Source/TestProject/
```

---

## Module responsibility map

| Module | Owns |
|---|---|
| `BlenderAssetBrowser` | SQLite + schema migrations, asset/catalog/tag/collection/preset/pose data managers, search engine, sidecar, dependency-copy helper, naming linter, provenance, external library mount |
| `BlenderAssetBrowserEditor` | The main `SAssetBrowserWindow` (toolbar / catalog tree / list+grid / inspector), all Slate dialogs (Naming, Snapshots, Dep Copy, AI Review, Reference Board, Quick Picker), Content Browser context menu, drag-drop payloads, command-list with hotkeys |
| `AssetPreview` | Renders thumbnails for uassets via UE ThumbnailTools, renders external FBX via Assimp → transient UStaticMesh → ThumbnailTools, disk-cache with mtime-based invalidation |
| `AITagging` | ONNX Runtime wrapper for the SigLIP2 vision encoder, sigmoid-scaled cosine scoring, tag vocabulary + pre-computed text embeddings loader |
| `BlenderBridge` | File-exchange folder, manifest.json + DirectoryWatcher → auto-reimport, Blender addon launching, DCC launcher (Substance/Photoshop/Max), DCC reimport watcher (mtime-based) |
| `UpdateChecker` | Fab daily HTTPS scrape with cross-host-redirect block, local-source mtime watcher, Snapshot/restore manager, Discord webhook |

---

## Bug history (10 issues found in last audit, all fixed)

Each entry: short id → file → root cause → fix. Useful when re-reading code
to understand why something is shaped the way it is.

| # | Where | Was | Now |
|---|---|---|---|
| 1 | `SAssetBrowserWindow.cpp` grid mode | `FSlateDynamicImageBrush::CreateWithImageData(path, size, {})` produced blank brushes | `FAssetThumbnail` + `FAssetThumbnailPool` per card |
| 2 | `ExternalLibraryMount.cpp` engine compat | string `>` on version strings; "5.10" < "5.6" by ASCII | `FEngineVersion::Parse` + `GetNewest` |
| 3 | `DCCReimportWatcher::Register` was orphan | never called → save-in-DCC did nothing | `FDCCLauncher::OpenInDCC` registers, BlenderBridgeModule publishes via `SetGlobalWatcher` |
| 4 | `SAITagReviewWindow::ShowModal` was orphan | no callers → AI never gave user a chance to review | `bAITagReviewBeforeApply` setting routes auto-tag through the modal |
| 5 | `DependencyCopyHelper::Compute` size | `PathLen * 100` placeholder | `IFileManager::FileSize` on resolved `.uasset` |
| 6 | `SigLIPInference::ScoreAgainst` | raw cosine [-1, 1] vs threshold [0, 1] | sigmoid scaling: `sigmoid(SigmoidScale × cos)`; `SigmoidScale` configurable, default 4.0 |
| 7 | Bulk-Add-Tag modal | ESC lost typed input | `OnTextCommitted` commits on Enter, ESC routes to Cancel |
| 8 | `ProvenanceTracker::Detect` | `FastGetAsset(false)` returned null for unloaded → no provenance | `GetAsset()` loads on demand |
| 9 | `AssetLibrarySidecar::ExportToFile` | dropped catalog/tag relations on round-trip | exports `asset_catalogs`, `asset_tags`, opt-in `asset_embeddings_index` |
| 10 | `PreviewCacheManager::HashKey` | SHA1 of `(path, size)` only → stale thumbnails after reimport | hash includes `.uasset` mtime |

## Security hardening (3 real issues, fixed)

| Issue | Severity | Fix |
|---|---|---|
| ONNX model not verified before load | 🔴 Critical | `kExpectedHash = "defa6894…"` SHA1 baked into `SigLIPInference.cpp`. Mismatch refuses to load. |
| Tag-embedding allocation unbounded | 🟠 High | `Count × Dim` capped at 10 M floats (40 MB) before `SetNumUninitialized` |
| Fab HTTP allowed cross-host redirect | 🟡 Medium | `OnHeaderReceived` compares request host vs response host; mismatch → `CancelRequest` |

If the SigLIP model on disk is replaced or corrupted, expect:
```
LogAITagging: Error: SigLIPInference: model hash mismatch.
  expected=defa6894832bd83a5eeb6936cca2ef58e90998aa
  actual=  <other>
  -> refusing to load.
```
To replace the model: update `kExpectedHash` in `SigLIPInference.cpp`
to the new SHA1.

---

## Security posture (full table)

| Defense | Where |
|---|---|
| Prepared statements only | `FAssetLibraryDatabase::Execute/QueryRows` — every call binds via `FBoundValue`, no SQL string concat |
| Path canonicalization + `..` reject | `BAB::IsPathInsideRoot`, `IsSafeUEPath`, `IsSafePath` in every FS-touching helper |
| Asset-class whitelist for Mark as Asset | `GetSupportedClasses()` set in `ContentBrowserMenuExtension` |
| Catalog cycle detection | `FCatalogManager::WouldCreateCycle` (BFS, depth cap 64) |
| BFS depth caps everywhere | `GetDescendants(MaxDepth=32)`, `MAX_DEPS=10000` in dep copy |
| String/numeric/array length caps | `BAB::MAX_*_LEN` constants in `AssetLibraryTypes`; `Validate()` enforces |
| HTTPS-only URLs | `FAssetEntry::Validate`, Fab scraper, Discord webhook |
| Cross-host redirect block | `FFabUpdateChecker::EnqueueOne` |
| JSON depth + size cap | `BAB::MAX_FILE_BYTES_JSON` before `FJsonSerializer::Deserialize` |
| FBX size cap before Assimp | `BAB::MAX_FILE_BYTES_FBX = 200 MB` |
| Assimp vertex/face cap | `MAX_VERTICES = 2M`, `MAX_FACES = 2M` in `ExternalPreviewRenderer` |
| Process launch via argv (never shell) | `FPlatformProcess::CreateProc` in `BlenderBridgeManager` and `DCCLauncher` |
| DLL allowlist (system folders blocked) | `IsForbiddenRoot` in `ExternalLibraryMount` |
| ONNX model SHA1 hash check | `SigLIPInference::EnsureRuntime` against `kExpectedHash` |
| Tag-embedding alloc cap | `TagVocabulary::LoadEmbeddings` total floats ≤ 10 M |
| SQLite hardening flags | `SQLITE_DQS=0`, `OMIT_LOAD_EXTENSION`, `OMIT_AUTHORIZATION`, `THREADSAFE=2`, `trusted_schema=OFF` |
| Discord webhook URL whitelist | Must start with `https://discord.com/api/webhooks/` or `discordapp.com` |

---

## What still isn't done end-to-end

These work in code but were never clicked through a live editor with real
data. Worth doing in person before calling it shipped:

- **Real Auto-tag round-trip** — load a sample StaticMesh, hit "Auto-tag
  (AI)", confirm at least one tag lands in DB with a reasonable confidence.
- **External FBX preview** — drop a sample .fbx in OS Explorer onto the
  asset browser window and observe the import + thumbnail.
- **Blender bridge** — install the addon in Blender, save a roundtrip, see
  reimport happen in UE.
- **DCC reimport watcher** — open a .spp in Substance via `DCCLauncher`,
  edit & save, confirm UE picks up the change.
- **Catalog drag-drop reparent** — drag one node onto another in the tree,
  confirm DB updates.
- **Snapshots restore** — make a snapshot, modify the file, restore, confirm
  rollback.
- **Discord webhook** — paste a valid webhook into Settings, wait for Fab
  scraper or trigger manually, see message in Discord.

When you do these, expect to find UI polish bugs (focus issues, missing
toasts, etc.) — flag them in TODO.md as they come up.

---

## Backlog (TODO highlights, prioritized)

**Polish that pays off most:**
- Inline editors in inspector (rating slider, notes text box, click-to-edit tags)
- Hover preview (rotating thumbnail for meshes, play button for sounds, looped gif for Niagara)
- Recursive CTE in SQL for catalogs (vs app-level BFS)
- Auto-export `.assetlib` on every DB write (debounced 5 s)

**V2 features:**
- Team library docs are written ([docs/TEAM_LIBRARY.md](docs/TEAM_LIBRARY.md));
  needs real-world test in a 2-person repo
- Per-asset versioning history (snapshot per asset, not per library)
- Global find references across multiple projects
- Substance/Photoshop two-way bridge (launcher works, reimport watcher works,
  needs final glue: register source files when assets are marked)

Full prioritized list in [TODO.md](TODO.md).

---

## User preferences

- Language: Russian for comms, English for code/docs
- Wants thorough upfront analysis before code lands
- Comfortable with autonomous multi-step work; expects "до конца"
- Cares about security ("чтобы хайкеры не подставили")
- Wants honest reporting (will ask explicitly: "all really works?")
- Doesn't tolerate placeholder/fake numbers — found the `PathLen × 100` bug
  immediately

---

## Memory anchors (this is what next-session-me should remember)

- The 10 bugs in section "Bug history" above each have a one-line root cause
  → fix. If you change one of those files, re-check the corresponding fix
  hasn't regressed.
- Filament was removed. Don't accidentally re-add it.
- The model hash is a baked constant. If the model file changes, the plugin
  refuses to load until the constant is updated.
- The Python build_embeddings.py script needs the SigLIP2 text encoder
  (~565 MB) to run, but the encoder is NOT shipped — only the resulting
  `default_tag_embeddings.bin`. Re-running requires re-downloading.
- The user's MSVC has 14.30, 14.32, 14.38, 14.44 installed. CMake defaults
  to 14.38; explicit pin to 14.44 is required for Assimp.
- UE 5.6 ships its own onnxruntime.dll v1.20 inside NNE plugin — that's why
  we ship ORT 1.20.1 headers (matching ABI) and the Windows loader picks the
  already-loaded UE copy.
