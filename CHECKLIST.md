# BlenderAssetBrowser — Master Checklist

This is the authoritative checklist for the entire plugin. Every step has:
- **Pre-checks** — what to verify before starting
- **Implementation** — what to build
- **Tests** — how to verify it works
- **Security review** — vulnerabilities to defend against

## Threat Model (apply to ALL phases)

### Attack Surfaces

| Surface | Threat | Defense |
|---|---|---|
| `.assetlib` JSON files (shared via VCS) | Path traversal, SQL injection, JSON bomb | Strict schema validation, prepared statements, size limits, reject `../` |
| External FBX/USD/GLTF files | Assimp CVEs (buffer overflow, malformed file crashes) | Set Assimp safety flags, file size cap, run validation in try/catch |
| SQLite queries | SQL injection | ALWAYS prepared statements, never string-concat user input |
| Blender bridge `manifest.json` | Crafted paths to overwrite system files | Whitelist target paths to project root, reject symlinks, validate JSON schema |
| Fab HTTP scraping | MITM, malicious response, redirect chains | HTTPS only, cert validation, max redirects=2, timeout=10s, no script execution |
| Custom merge driver (Python) | Code execution if attacker swaps script | Bundle in plugin, hash check on load, document install steps clearly |
| ONNX model file | Tampered model causing OOB reads | Hash check at load, bundled hash in source code |
| Library symlinks | Symlink loops, escape from library root | Resolve canonical path, reject if outside library root |
| DLL loading (Filament, Assimp, ONNX RT) | DLL hijacking | Use `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` + bundled paths |
| Blender executable launch | Command injection via crafted asset name | Use `FPlatformProcess::CreateProc` with separate args array, NEVER shell-string |
| File system operations | Race conditions (TOCTOU), arbitrary write | Atomic operations, validate paths, restrict to allowed roots |
| Memory safety (C++) | Buffer overflows, UAF | TArray/TUniquePtr only, no raw new/delete, bounds checks on all input |
| JSON parsing | DoS via deeply nested JSON | Max depth limit (32), max size cap (10MB per file) |
| AI model inference inputs | Adversarial images causing crashes | Resize+normalize before inference, catch ONNX exceptions |

### Universal Defenses (every module follows these)

1. **Prepared statements only** for all SQLite queries
2. **Path canonicalization** before any file operation: `FPaths::ConvertRelativePathToFull` + verify it's within allowed root
3. **JSON schema validation** before deserialization: required fields, type checks, size limits
4. **Safe HTTP**: HTTPS, cert verify, timeout, max-size cap, no auto-redirect to other domains
5. **No eval / exec** of any external content (Python, JS, etc.) loaded at runtime
6. **No shell strings** for process launch — always argv array
7. **Hash verification** for bundled binary assets (ONNX model, DLLs if shipped)
8. **Bounds checks** on every array access from external data
9. **Try/catch** around all third-party library calls (Assimp, Filament, ONNX)
10. **No reflection on user input** — never deserialize into arbitrary types

---

## Phase 1 — Foundation

### 1.1 Plugin skeleton

**Pre-checks:**
- [ ] UE 5.3+ installation located on disk
- [ ] Test project available or need to create one
- [ ] All folder paths absolute, no spaces issues

**Implementation:**
- [ ] Create folder `BlenderAssetBrowser/`
- [ ] Create `BlenderAssetBrowser.uplugin` (validated JSON)
- [ ] Create `LICENSE` (Apache 2.0 full text)
- [ ] Create `THIRD_PARTY_NOTICES.txt`
- [ ] Create `Source/` with 6 module folders
- [ ] Each module: `Public/`, `Private/`, `ModuleName.Build.cs`, module class

**Tests:**
- [ ] `.uplugin` parses as valid JSON
- [ ] All `Build.cs` files reference existing modules only
- [ ] Plugin loads in editor without errors
- [ ] Modules appear in `Plugins → Window → Plugins` UI
- [ ] No "module not found" errors in Output Log

**Security review:**
- [ ] No `RunsInPlatforms` includes Server (editor-only plugin)
- [ ] `EnabledByDefault: false` (user opts in)
- [ ] No suspicious external dependencies in Build.cs at this stage

### 1.2 SQLite integration

**Pre-checks:**
- [ ] sqlite3.c amalgamation downloaded from official sqlite.org
- [ ] SHA256 hash matches official release
- [ ] Compiled with safe flags: `SQLITE_DEFAULT_FOREIGN_KEYS=1`, `SQLITE_DQS=0`

**Implementation:**
- [ ] Bundle `sqlite3.c`, `sqlite3.h` in `ThirdParty/sqlite/`
- [ ] `FAssetLibraryDatabase` class:
  - [ ] `Open(path)` — opens DB, enables WAL mode, foreign keys
  - [ ] `Close()` — closes connection cleanly
  - [ ] `Execute(sql, params)` — prepared statement only, never plain
  - [ ] `Query(sql, params, callback)` — prepared statement, row callback
  - [ ] `Transaction(lambda)` — BEGIN/COMMIT/ROLLBACK with exception safety
  - [ ] Error logging via `UE_LOG`
- [ ] Schema migration system:
  - [ ] Version table: `schema_version (version INTEGER PRIMARY KEY)`
  - [ ] Migration scripts with version numbers
  - [ ] On open: run pending migrations in transaction

**Tests:**
- [ ] Open DB at writable path
- [ ] Create schema (run migrations)
- [ ] Insert test row via prepared statement
- [ ] Query test row
- [ ] Try SQL injection on test input — must escape
- [ ] Close DB cleanly
- [ ] Reopen and verify data persists

**Security review:**
- [ ] **NO** `sqlite3_exec` with user input
- [ ] **ALL** queries use `sqlite3_prepare_v2` + `sqlite3_bind_*`
- [ ] DB opened with `SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_PRIVATECACHE` (we serialize)
- [ ] Path validated before opening (no traversal)
- [ ] DB file permissions: 0600 on Unix, ACL-locked on Windows
- [ ] WAL files cleaned on close (no leftover sensitive data)

### 1.3 Core data types

**Implementation:**
- [ ] `FAssetEntry` struct — all fields from schema
- [ ] `FCatalog` struct
- [ ] `FTagEntry` struct
- [ ] `FLibraryEntry` struct
- [ ] `FCollection` + `FCollectionItem` structs
- [ ] All structs: `Validate()` method that checks invariants
- [ ] Serialize/deserialize to/from SQLite row
- [ ] Serialize/deserialize to/from JSON (for sidecar)

**Tests:**
- [ ] Roundtrip: struct → SQL row → struct gives identical data
- [ ] Roundtrip: struct → JSON → struct gives identical data
- [ ] `Validate()` rejects: empty asset_path, invalid type, negative IDs

**Security review:**
- [ ] String fields have max length (asset_path: 1024, name: 256, notes: 4096)
- [ ] Numeric fields have ranges (rating: 0-5, IDs: positive)
- [ ] No buffer reads beyond declared sizes
- [ ] JSON depth limit on deserialization

### 1.4 AssetLibrarySubsystem

**Implementation:**
- [ ] `UAssetLibrarySubsystem : UEditorSubsystem`
- [ ] `Initialize()` — creates DB at `{ProjectSaved}/BlenderAssetBrowser/library.db`
- [ ] `Deinitialize()` — closes DB
- [ ] Public API: `AddAsset`, `RemoveAsset`, `UpdateAsset`, `GetAssets`, etc.
- [ ] All API methods validate input before DB call
- [ ] Background thread for indexing (FRunnable)

**Tests:**
- [ ] Subsystem starts/stops with editor
- [ ] DB file created at expected location
- [ ] Add/remove/update operations work
- [ ] Concurrent access safe (multiple UI requests)

**Security review:**
- [ ] Subsystem only accessible in editor (not runtime)
- [ ] No reflection-based asset operations on untrusted data
- [ ] Async operations don't leak to game thread without bounds check

### 1.5 Catalogs

**Implementation:**
- [ ] `FCatalogManager` static class
- [ ] CRUD operations
- [ ] Reparent operation (cycle detection!)
- [ ] Reorder via sort_order
- [ ] Smart catalogs: store filter expression, eval on query
- [ ] Asset-catalog assignment (many-to-many)

**Tests:**
- [ ] Create root catalog
- [ ] Create child catalog
- [ ] Move catalog to new parent
- [ ] Detect cycle: A→B→A must fail
- [ ] Delete catalog: orphan child catalogs reparent to root
- [ ] Smart catalog query returns expected assets

**Security review:**
- [ ] Cycle detection prevents infinite recursion
- [ ] Catalog name length limit (256 chars)
- [ ] Smart filter expression: parse only, never eval as code
- [ ] SQL injection via filter expression: parameterized only

### 1.6 Mark as Asset

**Implementation:**
- [ ] Content Browser context menu extension
- [ ] Detect supported asset types
- [ ] Batch marking (multi-select)
- [ ] Auto-populate technical metadata via AssetRegistry
- [ ] "Unmark" command

**Tests:**
- [ ] Right-click on StaticMesh → menu shows "Mark as Asset"
- [ ] Click → asset appears in library
- [ ] Tri count, vert count populated correctly
- [ ] Multi-select 10 assets → mark all in single operation
- [ ] Unmark removes from library (file untouched)

**Security review:**
- [ ] Asset path validated before insertion
- [ ] Binary data from asset registry not stored verbatim (only metadata)
- [ ] No code execution from blueprint scanning

### 1.7 Tags

**Implementation:**
- [ ] `FTagManager` static class
- [ ] CRUD + merge operations
- [ ] Hierarchical via "/" delimiter
- [ ] Color tags
- [ ] Batch assign/remove
- [ ] Tag count auto-maintained via triggers or app-level

**Tests:**
- [ ] Create tag, assign to asset, retrieve
- [ ] Hierarchy: "env/foliage/grass" parses correctly
- [ ] Merge two tags into one (assets transferred)
- [ ] Delete tag removes from all assets

**Security review:**
- [ ] Tag name length limit (128 chars)
- [ ] Tag hierarchy depth limit (8 levels)
- [ ] No special chars allowed in tag names except `/`, `-`, `_`, alnum

### 1.8 UI Shell

**Implementation:**
- [ ] Tab spawner registration
- [ ] `SAssetBrowserWindow` main layout
- [ ] Toolbar widget
- [ ] Catalog tree (placeholder)
- [ ] Asset grid (placeholder)
- [ ] Inspector panel (placeholder)
- [ ] Grid size slider
- [ ] Theme support

**Tests:**
- [ ] Window opens via menu
- [ ] Window dockable
- [ ] Layout responsive to resize
- [ ] No Slate errors in log

**Security review:**
- [ ] User input fields have length limits
- [ ] No HTML rendering of user-provided strings
- [ ] No URL navigation from user content without confirmation

### 1.9 Settings

**Implementation:**
- [ ] `UAssetLibrarySettings : UDeveloperSettings`
- [ ] All config options
- [ ] Validation in `PostEditChangeProperty`

**Tests:**
- [ ] Settings appear in Project Settings
- [ ] Changes persist
- [ ] Invalid input rejected

**Security review:**
- [ ] Path settings validated (must exist, must be writable)
- [ ] URL settings validated (must be HTTPS)
- [ ] Executable paths validated (must exist, not in system folders we shouldn't run)

---

## Phase 2 — Core Features

### 2.1 Preview system (uassets)

**Pre-checks:**
- [ ] FPreviewScene available in current UE version

**Implementation:**
- [ ] `FAssetPreviewRenderer` class
- [ ] Scene setup (DirectionalLight + SkyLight + EnvironmentCubemap)
- [ ] Auto-framing camera by AABB
- [ ] Type-specific renderers (Mesh, Material, Texture, Blueprint, Sound)
- [ ] Render to UTextureRenderTarget2D
- [ ] Export to PNG with cache

**Tests:**
- [ ] Render StaticMesh thumbnail (256px)
- [ ] Render Material on Sphere/Cube/Plane
- [ ] Render Blueprint icon
- [ ] Cache hit returns same data without re-render
- [ ] Cache miss generates new thumbnail

**Security review:**
- [ ] Preview path validated (within library cache folder)
- [ ] PNG file size cap (5 MB)
- [ ] Scene cleanup on every render (no leak)
- [ ] No code execution from preview generation

### 2.2 Preview system (external FBX)

**Pre-checks:**
- [ ] Assimp DLLs/libs in ThirdParty/
- [ ] Filament DLLs/libs in ThirdParty/
- [ ] Hashes verified

**Implementation:**
- [ ] `FExternalPreviewRenderer`
- [ ] Assimp load with safety flags:
  - [ ] `aiProcess_ValidateDataStructure`
  - [ ] `aiProcess_FindInvalidData`
  - [ ] File size cap (200 MB)
  - [ ] Try/catch around all calls
- [ ] Filament render setup (IBL, dirlight)
- [ ] Auto-framing
- [ ] PNG output

**Tests:**
- [ ] Load valid FBX → thumbnail
- [ ] Load malformed FBX → graceful error
- [ ] Load 1GB file → rejected at size check
- [ ] Memory leak check after 100 renders

**Security review:**
- [ ] Assimp memory limits set (`aiSetImportPropertyInteger(...AI_CONFIG_PP_LBW_MAX_WEIGHTS...)`)
- [ ] File size validated BEFORE handing to Assimp
- [ ] No path traversal: file must be within allowed library
- [ ] Filament shader compilation isolated
- [ ] All render targets/buffers freed after use

### 2.3 Search engine

**Implementation:**
- [ ] `FSearchEngine` with FTS5
- [ ] Query parser: `tag:foo tris:<5000`
- [ ] Boolean logic
- [ ] Results ranking
- [ ] Search history

**Tests:**
- [ ] Simple text search
- [ ] Filter `type:StaticMesh tris:<5000`
- [ ] Boolean: `wood AND NOT wet`
- [ ] Empty query returns all
- [ ] Special chars in query don't break it

**Security review:**
- [ ] Query parser is custom, no eval
- [ ] All SQL via prepared statements
- [ ] Query length cap (1024 chars)
- [ ] Rate limit search (max 1/100ms)

### 2.4 Drag & Drop

**Implementation:**
- [ ] `FAssetDragDropOp` Slate drag-drop
- [ ] Library → viewport
- [ ] Material → viewport mesh (slot hit-test)
- [ ] OS Explorer → library
- [ ] Cross-project copy with dependencies

**Tests:**
- [ ] Drag mesh, drop in viewport, actor spawns at cursor
- [ ] Drop material on mesh, only target slot replaced
- [ ] Drag OS folder into library, hierarchy preserved
- [ ] Cross-project copy: no redirectors created

**Security review:**
- [ ] OS file paths validated (not system folders)
- [ ] Cross-project copy: no execution of imported content
- [ ] Manifest files from external sources validated

### 2.5 Global cross-project library

**Implementation:**
- [ ] `FExternalLibraryMount`
- [ ] Content-Only Plugin mount approach
- [ ] Symlink fallback
- [ ] Multi-library priority
- [ ] Engine version compat check

**Tests:**
- [ ] Add library folder, scan, see assets
- [ ] Switch library source, view changes
- [ ] Unmount cleanly

**Security review:**
- [ ] Library path validated (no system folders)
- [ ] Symlinks: resolve canonical, check stays in library root
- [ ] Engine version check prevents incompatible loads

### 2.6 Favorites & Recents

**Tests:**
- [ ] Pin asset, persists across sessions
- [ ] Recent list updates on use
- [ ] Sync between projects via shared library

### 2.7 Provenance tracking

**Implementation:**
- [ ] `FProvenanceTracker`
- [ ] Auto-detect Fab/Megascans/Local
- [ ] Vault Cache scanning (read-only)
- [ ] Source URL storage
- [ ] Badge UI

**Security review:**
- [ ] Vault Cache: read-only access
- [ ] URLs validated (HTTPS, well-formed)
- [ ] No automatic page navigation on click without confirm

---

## Phase 3 — Intelligence & Integration

### 3.1 AI auto-tagging

**Pre-checks:**
- [ ] ONNX Runtime DLL hash verified
- [ ] SigLIP2 model file hash verified
- [ ] Tag embeddings file hash verified

**Implementation:**
- [ ] `FSigLIPInference`
- [ ] Image preprocessing (resize, normalize)
- [ ] Batch inference
- [ ] Cosine similarity vs embeddings
- [ ] Background tagger

**Tests:**
- [ ] Tag a known wood texture → "wood" appears in suggestions
- [ ] Tag rocky terrain → "rock", "stone" appear
- [ ] Confidence threshold respected
- [ ] Batch of 100 thumbnails completes

**Security review:**
- [ ] Model file hash verified at load
- [ ] No execution of arbitrary ONNX from user paths
- [ ] Image preprocessing has bounds checks
- [ ] ONNX exceptions caught
- [ ] Tag vocabulary user additions validated

### 3.2 Update awareness

**Pre-checks:**
- [ ] HTTPS lib available (UE built-in)

**Implementation:**
- [ ] `FFabUpdateChecker` daily scrape
- [ ] HTTP request via UE `IHttpRequest`
- [ ] HTML parsing (look for version)
- [ ] Local source watcher

**Security review:**
- [ ] HTTPS only, cert validation
- [ ] Max redirects=2
- [ ] Timeout=10s, max body=2MB
- [ ] No script execution from response
- [ ] Sanitize HTML before storing changelog
- [ ] User-Agent identifies plugin (transparency)
- [ ] Rate limit: max 1 req/2s to fab.com

### 3.3 Blender bridge

**Pre-checks:**
- [ ] Blender exe path detected or settable
- [ ] Exchange folder writable

**Implementation:**
- [ ] `FBlenderBridgeManager`
- [ ] Exchange folder creation
- [ ] manifest.json with schema validation
- [ ] DirectoryWatcher on incoming/
- [ ] FBX export/reimport
- [ ] Blender addon py file

**Security review:**
- [ ] **CRITICAL:** target paths in manifest restricted to project /Content
- [ ] Blender launch via argv array, never shell string
- [ ] manifest.json schema strict validation
- [ ] Reject manifests with `..` in paths
- [ ] Validate FBX size before reimport
- [ ] Don't auto-import unsigned/untrusted FBX without confirm option

### 3.4 VCS sync

**Implementation:**
- [ ] JSON sidecar export
- [ ] DirectoryWatcher on .assetlib
- [ ] Custom merge driver (Python)

**Security review:**
- [ ] .assetlib JSON schema strict validation
- [ ] Size cap on .assetlib (50 MB)
- [ ] Merge driver is bundled, hash-verified
- [ ] Imported entries validated before insertion

### 3.5 Quick Picker

**Tests:**
- [ ] Hotkey opens overlay
- [ ] Search filters in real-time
- [ ] Enter spawns selected asset

---

## Phase 4 — V1 Differentiation

(Same checklist pattern: pre-check, implement, test, security review for each)

### 4.1 Actor collections — security: BP container code execution risk
### 4.2 Material presets — security: parameter injection
### 4.3 Pose Library — security: bone transform sanity
### 4.4 Dependency awareness — security: cycle detection in dep graph
### 4.5 Bulk operations — security: regex DoS, path validation in batch rename
### 4.6 DCC integration — security: same as Blender bridge for Substance/Photoshop

---

## Phase 5 — V2 Ambitious

### 5.1 Team library — security: P4/Git auth not stored in plaintext
### 5.2 Versioning — security: snapshot path validation
### 5.3 Global find refs — security: cross-project index isolation
### 5.4 Naming linter — security: regex DoS prevention
### 5.5 Selective update — security: snapshot integrity
### 5.6 Image board — security: HTML/URL validation, no JS

---

## Final Pre-Release Security Audit

Before public release, run full audit:

- [ ] Static analysis (PVS-Studio or Coverity)
- [ ] Fuzzing of: JSON parser, FBX loader (Assimp), search query parser
- [ ] Penetration test: try injecting malicious .assetlib, manifest.json, FBX
- [ ] Code review focused on: SQL queries, path operations, process launches
- [ ] Dependency audit: all deps still maintained, no known CVEs
- [ ] Hash verify all bundled binaries
- [ ] Plugin signing (if distributing binaries)
- [ ] Documentation: clear "trust your library sources" warning
- [ ] Threat model documented in repo

## Universal Anti-Pattern Checklist (catch in code review)

- [ ] No `sqlite3_exec` with concatenated input
- [ ] No `system()`, `popen()`, `ShellExecute()` with user data
- [ ] No `FString::Printf` for SQL queries
- [ ] No `FParse::Token` for security-critical input
- [ ] No raw pointers for buffers from external data
- [ ] No `memcpy` without size check
- [ ] No `strcpy/sprintf` (use FString or Printf with bounds)
- [ ] No HTML rendering of user content
- [ ] No URL fetched without HTTPS
- [ ] No path operation without canonicalization
- [ ] No DLL load from user-controlled path
- [ ] No exec of user-provided scripts
