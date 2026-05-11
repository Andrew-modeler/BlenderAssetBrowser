# BlenderAssetBrowser — Implementation Plan

## Technical Decisions (locked)

| Decision | Choice | Rationale |
|---|---|---|
| Plugin name | BlenderAssetBrowser | — |
| Target UE | 5.3+ | API stability, avoid excessive #if version guards |
| Language | C++ (Slate UI) | Performance, full API access |
| Storage | SQLite (runtime) + JSON sidecar (.assetlib) | SQLite for speed, JSON for VCS merge |
| Preview (uassets) | FPreviewScene + FSceneViewport | Like Static Mesh Editor, no PIE needed |
| Preview (external FBX) | Assimp + Google Filament | Autonomous, no UE dependency for external files |
| AI tagging | SigLIP2 ViT-B/16 FP16 via ONNX Runtime C++ | Apache 2.0, ~170 MB, sigmoid scoring |
| D&D material on mesh | Hit-test single slot replacement | User preference |
| Collection drop | Blueprint container + ChildActorComponent | User preference |
| VCS | JSON sidecar + custom merge driver | Team-friendly |
| Default shortcut | Ctrl+Shift+Space (configurable) | Avoids UE conflicts |
| Blender bridge | File-based exchange + DirectoryWatcher | Based on Eleganza ExternalMeshEditor |
| Fab updates | Daily HTTP scrape of Fab pages | No official API exists |
| Distribution | Standalone, Engine/Plugins | Not commercial |
| License | Apache 2.0 | Patent protection for ML code, compatible with all deps |

## Plugin Module Structure

```
BlenderAssetBrowser/
├── BlenderAssetBrowser.uplugin
├── LICENSE                          # Apache 2.0
├── THIRD_PARTY_NOTICES.txt          # Filament, ONNX, SigLIP2, Assimp, SQLite
│
├── Source/
│   ├── BlenderAssetBrowser/         # Runtime module (data, DB, types)
│   │   ├── BlenderAssetBrowser.Build.cs
│   │   ├── Public/
│   │   │   ├── BlenderAssetBrowserModule.h
│   │   │   ├── AssetLibraryDatabase.h       # SQLite wrapper
│   │   │   ├── AssetLibraryTypes.h          # FCatalog, FAssetEntry, FTagEntry...
│   │   │   ├── AssetLibrarySubsystem.h      # UEditorSubsystem — lifecycle
│   │   │   ├── CatalogManager.h             # Catalog CRUD, tree ops
│   │   │   ├── TagManager.h                 # Tag CRUD, batch ops
│   │   │   ├── ProvenanceTracker.h          # Source detection, Fab/Megascans/Local
│   │   │   ├── SearchEngine.h               # Fuzzy + metadata search
│   │   │   ├── ExternalLibraryMount.h       # Cross-project library mounting
│   │   │   └── AssetLibrarySettings.h       # UDeveloperSettings
│   │   └── Private/
│   │       ├── BlenderAssetBrowserModule.cpp
│   │       ├── AssetLibraryDatabase.cpp
│   │       ├── AssetLibrarySubsystem.cpp
│   │       ├── CatalogManager.cpp
│   │       ├── TagManager.cpp
│   │       ├── ProvenanceTracker.cpp
│   │       ├── SearchEngine.cpp
│   │       ├── ExternalLibraryMount.cpp
│   │       └── AssetLibrarySettings.cpp
│   │
│   ├── BlenderAssetBrowserEditor/   # Editor module (UI, Slate, interactions)
│   │   ├── BlenderAssetBrowserEditor.Build.cs
│   │   ├── Public/
│   │   │   ├── BlenderAssetBrowserEditorModule.h
│   │   │   ├── SAssetBrowserWindow.h        # Main dockable window
│   │   │   ├── SAssetGrid.h                 # Thumbnail grid widget
│   │   │   ├── SAssetCard.h                 # Single thumbnail card
│   │   │   ├── SCatalogTree.h               # Left panel catalog tree
│   │   │   ├── SAssetInspector.h            # Right panel details
│   │   │   ├── STagEditor.h                 # Tag management widget
│   │   │   ├── SQuickPicker.h               # Ctrl+Shift+Space overlay
│   │   │   ├── SSearchBar.h                 # Search with syntax highlighting
│   │   │   ├── SSmartFilterPanel.h          # Smart collections panel
│   │   │   ├── SProvenanceBadge.h           # Source badge overlay
│   │   │   └── AssetBrowserDragDrop.h       # D&D handlers
│   │   └── Private/
│   │       ├── BlenderAssetBrowserEditorModule.cpp
│   │       ├── SAssetBrowserWindow.cpp
│   │       ├── SAssetGrid.cpp
│   │       ├── SAssetCard.cpp
│   │       ├── SCatalogTree.cpp
│   │       ├── SAssetInspector.cpp
│   │       ├── STagEditor.cpp
│   │       ├── SQuickPicker.cpp
│   │       ├── SSearchBar.cpp
│   │       ├── SSmartFilterPanel.cpp
│   │       ├── SProvenanceBadge.cpp
│   │       ├── AssetBrowserDragDrop.cpp
│   │       ├── AssetBrowserCommands.cpp     # UI commands & keybindings
│   │       ├── AssetBrowserStyle.cpp        # FSlateStyleSet
│   │       └── ContentBrowserMenuExtension.cpp  # "Mark as Asset" in CB context menu
│   │
│   ├── AssetPreview/                # Preview rendering module
│   │   ├── AssetPreview.Build.cs
│   │   ├── Public/
│   │   │   ├── AssetPreviewRenderer.h       # FPreviewScene-based for uassets
│   │   │   ├── ExternalPreviewRenderer.h    # Assimp+Filament for FBX/USD
│   │   │   ├── PreviewCacheManager.h        # Disk cache for thumbnails
│   │   │   ├── MaterialPreviewRenderer.h    # Sphere/Cube/Plane preview for mats
│   │   │   └── AnimationPreviewRenderer.h   # Scrubber/gif for animations
│   │   └── Private/
│   │       ├── AssetPreviewRenderer.cpp
│   │       ├── ExternalPreviewRenderer.cpp
│   │       ├── PreviewCacheManager.cpp
│   │       ├── MaterialPreviewRenderer.cpp
│   │       └── AnimationPreviewRenderer.cpp
│   │
│   ├── AITagging/                   # AI auto-tagging module
│   │   ├── AITagging.Build.cs
│   │   ├── Public/
│   │   │   ├── AITaggingSubsystem.h         # Background batch tagger
│   │   │   ├── SigLIPInference.h            # ONNX Runtime C++ wrapper
│   │   │   └── TagVocabulary.h              # Built-in + user tag embeddings
│   │   └── Private/
│   │       ├── AITaggingSubsystem.cpp
│   │       ├── SigLIPInference.cpp
│   │       └── TagVocabulary.cpp
│   │
│   ├── BlenderBridge/               # Blender integration module
│   │   ├── BlenderBridge.Build.cs
│   │   ├── Public/
│   │   │   ├── BlenderBridgeManager.h       # Exchange folder, watcher, import
│   │   │   └── BlenderBridgeSettings.h
│   │   └── Private/
│   │       ├── BlenderBridgeManager.cpp
│   │       └── BlenderBridgeSettings.cpp
│   │
│   └── UpdateChecker/               # Fab/source update awareness
│       ├── UpdateChecker.Build.cs
│       ├── Public/
│       │   ├── FabUpdateChecker.h           # Daily HTTP scraper
│       │   ├── LocalSourceWatcher.h         # mtime/hash watcher for local files
│       │   └── UpdateNotificationManager.h
│       └── Private/
│           ├── FabUpdateChecker.cpp
│           ├── LocalSourceWatcher.cpp
│           └── UpdateNotificationManager.cpp
│
├── Content/
│   └── Icons/                       # Plugin icons, badges, UI assets
│
├── Resources/
│   ├── BlenderAddon/
│   │   └── blender_asset_browser_bridge.py  # Blender addon with "Send to UE"
│   ├── Models/
│   │   └── siglip2_vision_fp16.onnx        # SigLIP2 vision encoder
│   ├── TagVocabulary/
│   │   ├── default_tags.json                # 300+ built-in tag definitions
│   │   └── default_tag_embeddings.bin       # Pre-computed text embeddings
│   └── MergeDriver/
│       └── assetlib_merge.py                # Custom VCS merge driver
│
├── ThirdParty/
│   ├── sqlite/                      # SQLite amalgamation (public domain)
│   ├── filament/                    # Google Filament libs (Apache 2.0)
│   ├── assimp/                      # Assimp libs (BSD 3-Clause)
│   └── onnxruntime/                 # ONNX Runtime libs (MIT)
│
└── Config/
    └── DefaultBlenderAssetBrowser.ini
```

## SQLite Database Schema

```sql
-- Core tables
CREATE TABLE catalogs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id   INTEGER REFERENCES catalogs(id) ON DELETE CASCADE,
    name        TEXT NOT NULL,
    color       TEXT,           -- hex color
    icon        TEXT,           -- icon name
    sort_order  INTEGER DEFAULT 0,
    is_smart    BOOLEAN DEFAULT FALSE,
    smart_query TEXT,           -- filter expression for smart catalogs
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE assets (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_path      TEXT NOT NULL,       -- /Game/Meshes/SM_Rock or external path
    asset_name      TEXT NOT NULL,
    asset_type      TEXT NOT NULL,       -- StaticMesh, Material, Blueprint...
    library_id      INTEGER REFERENCES libraries(id),
    rating          INTEGER DEFAULT 0,   -- 0-5
    notes           TEXT,
    preview_path    TEXT,                -- path to cached thumbnail
    preview_mesh    TEXT,                -- for materials: Sphere/Cube/Plane/Custom

    -- Technical metadata (auto-populated)
    tri_count       INTEGER,
    vert_count      INTEGER,
    lod_count       INTEGER,
    texture_res_max INTEGER,
    has_collision    BOOLEAN,
    collision_type   TEXT,
    material_count  INTEGER,
    disk_size_bytes INTEGER,
    engine_version  TEXT,

    -- Provenance
    source_type     TEXT,               -- Fab/Megascans/Local/Custom/Imported
    source_pack_name TEXT,
    source_pack_id  TEXT,
    source_url      TEXT,
    source_author   TEXT,
    source_license  TEXT,               -- Standard/Pro/CC0/Custom
    source_version  TEXT,
    source_hash     TEXT,               -- for local source tracking
    imported_at     DATETIME,

    -- Update tracking
    latest_version  TEXT,
    update_available BOOLEAN DEFAULT FALSE,
    changelog       TEXT,

    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    modified_at     DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(asset_path, library_id)
);

CREATE TABLE asset_catalogs (
    asset_id    INTEGER REFERENCES assets(id) ON DELETE CASCADE,
    catalog_id  INTEGER REFERENCES catalogs(id) ON DELETE CASCADE,
    PRIMARY KEY (asset_id, catalog_id)
);

CREATE TABLE tags (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    name    TEXT NOT NULL UNIQUE,
    color   TEXT,           -- hex color for visual badge
    parent  TEXT,           -- hierarchical: "environment/foliage"
    count   INTEGER DEFAULT 0
);

CREATE TABLE asset_tags (
    asset_id INTEGER REFERENCES assets(id) ON DELETE CASCADE,
    tag_id   INTEGER REFERENCES tags(id) ON DELETE CASCADE,
    source   TEXT DEFAULT 'manual',  -- manual/ai
    confidence REAL,                 -- AI confidence 0.0-1.0
    PRIMARY KEY (asset_id, tag_id)
);

CREATE TABLE libraries (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    path        TEXT NOT NULL,       -- disk path to library root
    type        TEXT DEFAULT 'local', -- local/network/mounted
    priority    INTEGER DEFAULT 0,
    is_visible  BOOLEAN DEFAULT TRUE,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE favorites (
    asset_id    INTEGER REFERENCES assets(id) ON DELETE CASCADE,
    pinned      BOOLEAN DEFAULT FALSE,
    used_at     DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (asset_id)
);

CREATE TABLE collections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    description TEXT,
    preview_path TEXT,
    spawn_mode  TEXT DEFAULT 'blueprint',  -- blueprint/level_instance/loose
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE collection_items (
    collection_id INTEGER REFERENCES collections(id) ON DELETE CASCADE,
    asset_id      INTEGER REFERENCES assets(id) ON DELETE CASCADE,
    transform     TEXT,             -- JSON: {location, rotation, scale}
    material_overrides TEXT,        -- JSON: slot->material mapping
    sort_order    INTEGER DEFAULT 0,
    PRIMARY KEY (collection_id, asset_id, sort_order)
);

CREATE TABLE search_history (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    query   TEXT NOT NULL,
    used_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Indexes for fast lookups
CREATE INDEX idx_assets_type ON assets(asset_type);
CREATE INDEX idx_assets_source ON assets(source_type);
CREATE INDEX idx_assets_library ON assets(library_id);
CREATE INDEX idx_assets_name ON assets(asset_name);
CREATE INDEX idx_tags_name ON tags(name);
CREATE INDEX idx_favorites_used ON favorites(used_at DESC);

-- FTS5 for fuzzy search
CREATE VIRTUAL TABLE assets_fts USING fts5(
    asset_name, notes, source_pack_name, source_author,
    content='assets', content_rowid='id'
);
```

## Blender Bridge Design

### Architecture: File-based exchange + DirectoryWatcher

Based on Eleganza ExternalMeshEditor but enhanced for two-way workflow.

```
Exchange Folder: {ProjectSaved}/BlenderAssetBrowser/Exchange/
  ├── manifest.json          # Current exchange state
  ├── outgoing/              # UE → Blender
  │   ├── SM_Rock.fbx
  │   └── SM_Rock.meta.json  # {ue_asset_path, ue_project, engine_version}
  └── incoming/              # Blender → UE
      ├── SM_Rock.fbx
      └── SM_Rock.meta.json  # {target_path, is_new, overwrite}
```

### Flow 1: UE → Blender → UE (roundtrip edit)

1. User right-clicks asset in BlenderAssetBrowser → "Edit in Blender"
2. Plugin exports FBX to `outgoing/` + writes `.meta.json` with UE path
3. Plugin launches: `blender.exe --python blender_bridge.py -- "outgoing/SM_Rock.fbx" "/Game/Meshes/SM_Rock"`
4. Blender opens, imports FBX, shows "Unreal Bridge" panel in sidebar
5. User edits mesh in Blender
6. User clicks "Send to Unreal" → Blender exports FBX to same path
7. UE DirectoryWatcher detects file change → auto-reimport to original asset path
8. Toast notification: "SM_Rock updated from Blender"

### Flow 2: New file from Blender → UE

1. User opens Blender independently, has a mesh they want in UE
2. Blender addon installed (from plugin's Resources/BlenderAddon/)
3. Addon shows "Send to Unreal" panel. For new files:
   - Button "Send to Unreal (New Asset)"
   - Dropdown: select target UE project (addon reads `manifest.json` for known projects)
   - Text field: target folder path (with autocomplete from UE project structure)
   - Addon exports FBX to `incoming/` + writes `.meta.json` with target path + `is_new: true`
4. UE plugin watches `incoming/`, sees new file, imports to specified path
5. Automatically runs "Mark as Asset" on the imported mesh
6. Toast notification: "New asset imported from Blender: SM_NewMesh"

### Flow 3: Non-UE file → UE

1. User has a standalone .blend file, opens it in Blender
2. Addon panel shows "Send to Unreal" with folder picker
3. Same as Flow 2 from step 3

### Blender Addon Features

- Panel in 3D Viewport sidebar (N-panel), tab "Unreal"
- "Send to Unreal" button (large, prominent)
- "Send as New" / "Update Existing" toggle (auto-detected from meta)
- Target project selector (reads from manifest)
- Target folder path with autocomplete
- Display: asset name, source UE path, collision count
- FBX export settings preset (UE-compatible: Z-up, cm, FBX 2020)

### UE Plugin Side

- `FBlenderBridgeManager`:
  - Creates exchange folder structure on startup
  - Writes `manifest.json` with project info (name, path, engine version)
  - `IDirectoryWatcher` on `incoming/` and `outgoing/`
  - On file change: validate FBX, read `.meta.json`, import/reimport
  - Queue reimport on game thread via `FTSTicker`
  - Show toast notification on success/failure
- Context menu integration: "Edit in Blender" for any mesh asset
- Settings: Blender executable path (auto-detect or manual), exchange folder path

---

## Phase 1 — Foundation (4-6 weeks)

Goal: Plugin builds, loads, has working UI shell and data layer.

### 1.1 Plugin skeleton
- .uplugin descriptor for UE 5.3+
- 6 modules: Runtime, Editor, AssetPreview, AITagging, BlenderBridge, UpdateChecker
- Build.cs files with all dependencies
- Module startup/shutdown lifecycle

### 1.2 SQLite integration
- Bundle sqlite3 amalgamation in ThirdParty/
- `FAssetLibraryDatabase` wrapper: open/close, execute, prepared statements
- Schema creation on first run
- Migration system for future schema changes

### 1.3 Core data types
- `FAssetEntry`, `FCatalog`, `FTagEntry`, `FLibraryEntry`, `FCollection`
- Serialization to/from SQLite rows
- JSON sidecar export/import for VCS

### 1.4 AssetLibrarySubsystem
- `UAssetLibrarySubsystem` (UEditorSubsystem)
- Lifecycle: init DB on editor start, close on shutdown
- API surface for all CRUD operations on catalogs, assets, tags
- Async indexing worker on background thread

### 1.5 Catalogs
- `FCatalogManager`: create, rename, delete, reparent, reorder
- Tree structure with parent_id
- Drag-and-drop reorder/reparent
- Smart catalogs: stored filter expression, auto-refresh results
- One asset in multiple catalogs (many-to-many via asset_catalogs)

### 1.6 Mark as Asset
- Content Browser context menu extension via `FContentBrowserModule::GetAllAssetViewContextMenuExtenders()`
- "Mark as Asset" for: StaticMesh, SkeletalMesh, Material, MaterialInstance, MaterialFunction, Blueprint, NiagaraSystem, SoundBase, AnimSequence, AnimMontage, Texture2D, DataAsset, LevelInstance
- Batch marking: multi-select → mark all
- On mark: populate auto-fields (tri_count, vert_count, LOD, etc.) by loading asset metadata
- "Unmark" to remove from library

### 1.7 Tags (basic)
- `FTagManager`: create, rename, delete, merge tags
- Assign/remove tags to assets (single + batch)
- Tag counter (auto-maintained)
- Hierarchical tags via "/" separator: "environment/foliage/grass"
- Color tags (predefined set: red/orange/yellow/green/blue/purple)

### 1.8 UI Shell
- Register tab spawner: `FGlobalTabmanager`
- `SAssetBrowserWindow`: main layout (toolbar | catalog tree | grid | inspector)
- Dockable as tab or panel next to Content Browser
- Toolbar: library source switcher, search bar, filter chips, Mark as Asset button
- `SCatalogTree`: left panel with tree view (placeholder data initially)
- `SAssetGrid`: center panel with scrollable tile grid (placeholder cards)
- `SAssetInspector`: right panel detail view (placeholder)
- Grid size slider (32px → 512px)
- Dark/light/compact UI modes via FSlateStyleSet

### 1.9 Settings
- `UAssetLibrarySettings` (UDeveloperSettings)
- Library paths (TArray<FDirectoryPath>)
- Blender executable path
- Default shortcut
- Preview resolution (256/512)
- AI tagging on/off
- Theme selection
- Registered in Project Settings UI

---

## Phase 2 — Core Features (6-8 weeks)

Goal: Functional asset browser with preview, search, drag & drop, and cross-project library.

### 2.1 Preview system (uassets)
- `FAssetPreviewRenderer`:
  - `FPreviewScene` with custom DirectionalLight + SkyLight + EnvironmentCubemap
  - Auto-framing camera by asset BoundingBox
  - For StaticMesh: turntable angle, configurable
  - For Material: preview mesh selection (Sphere/Cube/Plane/Cylinder/Custom)
  - For Texture: flat display with alpha
  - For Blueprint: icon + class name + public API list
  - For Sound: waveform image + play button
  - Render to `UTextureRenderTarget2D` → extract to PNG → cache
- `FPreviewCacheManager`:
  - Disk cache in library folder: `{library}/.previews/{asset_hash}.png`
  - LRU eviction when cache exceeds configured size
  - Async generation on background thread
  - Invalidation on asset modification (timestamp check)

### 2.2 Preview system (external FBX/USD)
- `FExternalPreviewRenderer`:
  - Assimp: load FBX/OBJ/GLTF/USD geometry + materials
  - Filament: setup PBR scene (IBL + directional light)
  - Auto-framing by AABB
  - Render to offscreen buffer → PNG → cache
  - Build system: link Assimp + Filament from ThirdParty/
  - Fallback: file type icon if render fails

### 2.3 Search engine
- `FSearchEngine`:
  - SQLite FTS5 for fuzzy text search
  - Structured query parser: `tag:foliage tris:<5000 type:StaticMesh source:Megascans`
  - Supported filters: type, tag, source, tris, verts, texture_res, has_lod, has_collision, rating, engine_version, date_range
  - AND/OR/NOT boolean logic
  - Search history (last 50 queries)
  - Results ranked by relevance (FTS5 rank + recency + rating)

### 2.4 Drag & Drop
- `FAssetBrowserDragDrop`:
  - Library → Viewport: spawn actor at surface-snap position
  - Material → Viewport mesh: hit-test to detect material slot index, replace single slot
  - Library → Content Browser: copy asset to target folder
  - OS Explorer → Library: auto-import FBX/PNG/WAV, mark as asset
  - Between catalogs: update catalog assignment (no file move)
  - Folder drag from OS: preserve hierarchy as catalog structure
- Cross-project copy:
  - Resolve all dependencies (textures, materials, sub-assets)
  - Copy atomically without redirectors
  - `FAssetToolsModule::Get().MigratePackages()` + fixup references
  - Headless redirector fixup via `FAssetRegistryModule`
  - Option toggle: "Copy with dependencies" / "Link only" / "Asset only"

### 2.5 Global cross-project library
- `FExternalLibraryMount`:
  - Mount external folder as virtual content path
  - Method 1: Content-Only Plugin mount (UE native, clean)
  - Method 2: Symlink creation (fallback for non-plugin-aware paths)
  - Multiple libraries with priority and visibility toggle
  - Library scan on editor startup (async, non-blocking)
  - Engine version compatibility check on assets
  - Library-level .assetlib sidecar file with all metadata (portable)
- Library browser in UI: source switcher dropdown (Project / Personal / Studio / Custom)

### 2.6 Favorites & Recents
- Pin/unpin assets via star icon on card
- Recent assets list (last 100, sorted by last-used timestamp)
- Bottom "Drop Zone" bar with pinned + recent
- Sync across projects via shared library .assetlib file
- Dedicated catalog node "Favorites" and "Recent" in tree

### 2.7 Provenance tracking
- `FProvenanceTracker`:
  - Auto-detect source on "Mark as Asset":
    - Fab: scan `%LOCALAPPDATA%/EpicGamesLauncher/VaultCache/`, match by package name
    - Megascans: check for Quixel metadata in asset
    - Imported: read `UAssetImportData::GetSourceData()` for original file path
    - Custom: user-set
  - Store source_type, source_pack_name, source_url, author, license
  - Badge overlay on thumbnail card (small icon: Fab/Megascans/Local/Custom)
  - "Open Source Page" context menu action
  - Filter/group by source in catalog tree
  - License badge with tooltip

---

## Phase 3 — Intelligence & Integration (6-8 weeks)

Goal: AI tagging, update awareness, Blender bridge, command palette.

### 3.1 AI auto-tagging
- `FSigLIPInference`:
  - Bundle ONNX Runtime C++ (DirectML for GPU on Windows)
  - Ship siglip2_vision_fp16.onnx (~170 MB) in Resources/Models/
  - Ship pre-computed tag embeddings (~300 tags) in Resources/TagVocabulary/
  - Image preprocessing: resize to 224x224, normalize (SigLIP mean/std)
  - Batch inference: process N thumbnails per call
  - Cosine similarity vs tag embeddings → tags above threshold
- `FAITaggingSubsystem`:
  - Background worker: queue assets without tags → batch process
  - Configurable confidence threshold (default 0.3)
  - Results stored with source='ai' and confidence score
  - UI: "Auto-tag selected" button, "Accept/reject AI tags" review panel
  - User can add custom tags to vocabulary → recompute embeddings on save
  - Tag vocabulary editor: add/remove/rename, preview suggested tags

### 3.2 Update awareness
- `FFabUpdateChecker`:
  - Daily check (on editor startup, if >24h since last)
  - For each Fab-sourced asset: HTTP GET to fab.com page, parse version/changelog
  - Compare with stored source_version
  - Mark update_available = true, store changelog
  - Badge "Update Available" on thumbnail
  - Smart filter "Outdated" shows all assets with updates
- `FLocalSourceWatcher`:
  - For locally-imported assets: check mtime/hash of source file
  - If changed: mark "Source Changed" badge, offer reimport
  - Panel: "N source files changed since last check"
- `FUpdateNotificationManager`:
  - In-editor toast notifications
  - Optional: Discord webhook for update alerts
  - Changelog panel in SAssetInspector

### 3.3 Blender bridge
- Implementation as described in "Blender Bridge Design" section above
- `FBlenderBridgeManager` in BlenderBridge module
- Blender addon in Resources/BlenderAddon/
- Context menu: "Edit in Blender" for StaticMesh, SkeletalMesh
- Settings: Blender path, auto-detect, exchange folder location
- DirectoryWatcher for incoming files
- Auto-reimport with notification

### 3.4 VCS sync
- JSON sidecar export: `{library}/.assetlib` file
  - Human-readable, line-per-entry format for clean diffs
  - Sections: catalogs, assets, tags, collections, settings
  - Auto-export on every change (debounced 5s)
  - Auto-import on file change (DirectoryWatcher on .assetlib)
- Custom merge driver:
  - Python script in Resources/MergeDriver/
  - Three-way merge: base/ours/theirs
  - Conflict resolution: per-asset, per-catalog, per-tag
  - Merge strategy: last-writer-wins for scalar fields, union for tags/catalogs
  - Git config: `.gitattributes` entry + merge driver registration
  - P4: custom resolve trigger

### 3.5 Quick Picker (command palette)
- `SQuickPicker`:
  - Ctrl+Shift+Space (configurable) → overlay window center-screen
  - Fuzzy search input with instant results
  - Grid of matching thumbnails (max 20)
  - Enter → spawn selected in viewport at cursor
  - Esc → close
  - Arrow keys / mouse navigation
  - Category chips for quick filter (Meshes / Materials / Blueprints)

---

## Phase 4 — V1 Differentiation (6-8 weeks)

Goal: Features that set plugin apart from Dash and built-in UE tools.

### 4.1 Actor collections as assets
- Select actors on level → "Mark as Collection"
- Stores: transform offsets relative to group center, asset references, material overrides
- Drop collection in viewport: spawn as Blueprint with ChildActorComponents (user choice)
- Alternative: spawn as loose actors
- Collection editor: add/remove items, adjust transforms

### 4.2 Material presets
- "Mark as Preset" on MaterialInstance
- Stores only parameter delta over master material (<5KB)
- On spawn in new project: auto-create MI with parameters, resolve master material
- Catalog: "Materials / Wood / Oak Dark" with preview on configurable mesh

### 4.3 Pose Library
- Save pose from Sequencer / Animation Editor: "Save Pose to Library"
- Stores: bone transforms relative to ref pose
- Preview: render character in pose
- Drag pose onto SkeletalMesh actor → apply
- Blend between poses (optional slider)
- Port of Blender Pose Library v2 concept

### 4.4 Dependency awareness
- On drag from library: show dependency tree popup
- Options: "Copy with all dependencies" / "Copy asset only" / "Link (don't copy)"
- Dependency visualization in SAssetInspector
- "Used in N projects" counter (if multiple projects indexed)

### 4.5 Bulk operations
- Multi-select in grid → context menu:
  - Batch rename (regex, prefix/suffix, numbering)
  - Batch add/remove tags
  - Batch set thumbnail camera angle
  - Batch reimport from source
  - Batch set collision type
  - Batch generate LOD
  - Batch Property Matrix (opens UE built-in)

### 4.6 DCC integration (beyond Blender)
- "Edit Source" for .spp (Substance Painter), .psd (Photoshop)
- Launch DCC with source file path
- Watch source file for changes → offer reimport
- Configurable DCC paths in settings

---

## Phase 5 — V2 Ambitious (8-12 weeks)

### 5.1 Team / shared library
- Library folder in P4/Git LFS
- Lock via P4 checkout
- .assetlib as text, mergeable
- User roles: admin / editor / viewer (via .assetlib metadata)

### 5.2 Versioning within library
- "Snapshot" creates versioned copy in `.versions/` subfolder
- Version list in SAssetInspector
- Rollback to any version
- Diff preview between versions (thumbnail comparison)

### 5.3 Global find references
- "Used in N projects" for library assets
- Per-project index of asset references
- "Find all references" across connected projects

### 5.4 Naming convention linter
- Configurable rules in JSON (SM_, M_, T_, BP_ prefixes)
- Scan library for violations
- Batch auto-fix with rename
- Import-time lint: rename on import per studio standard

### 5.5 Selective update with diff + rollback
- Before Fab update: snapshot all affected files
- Diff panel: show what changed (files added/modified/deleted)
- Checklist: select what to update, skip customized files
- One-click rollback from snapshot

### 5.6 Image/reference board
- Side panel with drag-from-web/desktop reference images
- Pin references next to asset groups
- Lightweight, not a core feature

---

## Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| Assimp + Filament build complexity | High | Compile as separate DLL, lazy-load |
| ONNX Runtime DLL size (~100 MB) | Medium | Ship as optional download or lazy-load |
| Fab page scraping breaks | Medium | Graceful degradation, user notification |
| UE API changes between 5.3-5.6 | Medium | Abstraction layer, CI on multiple UE versions |
| SQLite thread safety | High | WAL mode, single writer, connection pool |
| Large library perf (>100K assets) | High | Pagination, virtual scrolling, async queries |
| DirectoryWatcher reliability on network shares | Medium | Periodic fallback scan alongside watcher |
