# BlenderAssetBrowser — Project Root

Free (Apache 2.0) Unreal Engine 5.3+ plugin that replicates Blender's Asset
Browser for UE: centralized catalogs, cross-project library, AI auto-tagging,
Blender bridge, provenance tracking, fuzzy search.

## Quick map

| File | What's in it |
|---|---|
| [README.md](README.md) | This file — navigation. |
| [Asset Browser Blender-style.md](Asset%20Browser%20Blender-style.md) | Original research with community quotes. |
| [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) | Full phased plan: architecture, DB schema, module structure, risks. |
| [PHASE_STATUS.md](PHASE_STATUS.md) | What's built and verified. Runtime log. Security posture. |
| [TODO.md](TODO.md) | Prioritized backlog of what's left. |
| [CHECKLIST.md](CHECKLIST.md) | Threat model + per-phase test checklist. |
| [CONTEXT.md](CONTEXT.md) | Session context for continuation. |
| [BlenderAssetBrowser/](BlenderAssetBrowser/) | The plugin itself. |
| [TestProject/](TestProject/) | Minimal UE 5.6 project that hosts the plugin for testing. |

## TL;DR — what works today

Plugin builds and initializes cleanly in UE 5.6. From the live runtime log:

```
LogPluginManager: Mounting Project plugin BlenderAssetBrowser
LogBlenderAssetBrowser:       BlenderAssetBrowser runtime module started.
LogBlenderAssetBrowser:       Database opened: ...library.db
LogBlenderAssetBrowser:       AssetLibrarySubsystem ready
LogBlenderAssetBrowserEditor: BlenderAssetBrowserEditor module started.
LogBlenderAssetBrowserEditor: Content Browser menu extension registered.
LogAssetPreview:              Preview cache root: ...PreviewCache
LogAssetPreview:              AssetPreview module started.
LogAITagging:                 SigLIPInference: model loaded (1 in / 2 out).
LogAITagging:                 AITagging module started (vocabulary: loaded; SigLIP inference: ready).
LogBlenderBridge:             BlenderBridge initialized at ...Exchange
LogBlenderBridge:             BlenderBridge module started.
LogUpdateChecker:             UpdateChecker module started (Phase 1 stub).
```

## How to build

```powershell
& "C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" `
  TestProjectEditor Win64 Development `
  "D:\WORK\UE plugins\Asset Browser Blender-style\TestProject\TestProject.uproject" `
  -waitmutex -NoUBA
```

`-NoUBA` is required on this machine — UnrealBuildAccelerator crashes rc.exe.

## How to verify (headless)

```powershell
& "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "D:\WORK\UE plugins\Asset Browser Blender-style\TestProject\TestProject.uproject" `
  -ExecCmds="quit" -unattended -nullrhi -log -ABSLOG="editor_test.log"
```

Then `grep BlenderAssetBrowser editor_test.log` should show the 13-line boot
sequence above, with no `ERROR` lines.

## Repo layout

```
Asset Browser Blender-style/
├── README.md                       # this file
├── IMPLEMENTATION_PLAN.md          # full plan
├── PHASE_STATUS.md                 # current state
├── TODO.md                         # what's left
├── CHECKLIST.md                    # security + tests per phase
├── CONTEXT.md                      # session context
├── Asset Browser Blender-style.md  # original research
├── BlenderAssetBrowser/            # THE PLUGIN
│   ├── BlenderAssetBrowser.uplugin
│   ├── LICENSE                     # Apache 2.0
│   ├── THIRD_PARTY_NOTICES.txt
│   ├── Source/
│   │   ├── BlenderAssetBrowser/    # runtime: DB, types, subsystem, search, ...
│   │   ├── BlenderAssetBrowserEditor/  # UI, Content Browser menu, drag-drop
│   │   ├── AssetPreview/           # FAssetPreviewRenderer + cache
│   │   ├── AITagging/              # FSigLIPInference + tag vocabulary
│   │   ├── BlenderBridge/          # FBlenderBridgeManager
│   │   └── UpdateChecker/          # FLocalSourceWatcher
│   ├── Resources/
│   │   ├── BlenderAddon/           # blender_asset_browser_bridge.py
│   │   ├── Models/                 # siglip2_vision_fp16.onnx (178 MB)
│   │   ├── TagVocabulary/          # default_tags.json (200+ tags)
│   │   └── MergeDriver/            # assetlib_merge.py (Git merge driver)
│   ├── ThirdParty/
│   │   ├── assimp/                 # README only — see Phase 2.2 blocker
│   │   ├── filament/               # 430 MB static libs (/MD release)
│   │   └── onnxruntime/            # 1.20.1 SDK (matches UE 5.6 bundle)
│   └── Config/
└── TestProject/                    # UE 5.6 test project, Plugin junctioned in
    ├── TestProject.uproject
    └── Source/TestProject/
```

## License

Apache 2.0. Third-party components listed in `BlenderAssetBrowser/THIRD_PARTY_NOTICES.txt`.
