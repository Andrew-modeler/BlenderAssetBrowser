# BlenderAssetBrowser — Project Root

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Unreal Engine 5.3+](https://img.shields.io/badge/Unreal%20Engine-5.3%2B-313131?logo=unrealengine&logoColor=white)](https://www.unrealengine.com/)
[![GitHub Stars](https://img.shields.io/github/stars/Andrew-modeler/BlenderAssetBrowser?style=social)](https://github.com/Andrew-modeler/BlenderAssetBrowser/stargazers)
[![GitHub Issues](https://img.shields.io/github/issues/Andrew-modeler/BlenderAssetBrowser)](https://github.com/Andrew-modeler/BlenderAssetBrowser/issues)

Free (Apache 2.0) Unreal Engine 5.3+ plugin that replicates Blender's Asset
Browser for UE: centralized catalogs, cross-project library, AI auto-tagging,
Blender bridge, provenance tracking, fuzzy search.

## Quick map

| Path | What's in it |
|---|---|
| [BlenderAssetBrowser/](BlenderAssetBrowser/) | The plugin itself — drop into your project's `Plugins/` folder. |
| **[docs/USER_GUIDE.md](docs/USER_GUIDE.md)** | **Full user guide — install, every feature, troubleshooting.** |
| [docs/TEAM_LIBRARY.md](docs/TEAM_LIBRARY.md) | Git LFS / Perforce workflow for shared team libraries. |
| [BlenderAssetBrowser/THIRD_PARTY_NOTICES.txt](BlenderAssetBrowser/THIRD_PARTY_NOTICES.txt) | License notices for ONNX Runtime, SigLIP2, Assimp, SQLite. |

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
BlenderAssetBrowser/
├── BlenderAssetBrowser.uplugin
├── LICENSE                         # Apache 2.0
├── THIRD_PARTY_NOTICES.txt
├── Source/
│   ├── BlenderAssetBrowser/        # runtime: SQLite DB, subsystem, search, …
│   ├── BlenderAssetBrowserEditor/  # Slate UI, Content Browser menu, drag-drop
│   ├── AssetPreview/               # thumbnail renderer + LRU disk cache
│   ├── AITagging/                  # ONNX Runtime + SigLIP2 vision encoder
│   ├── BlenderBridge/              # Blender + DCC reimport bridge
│   └── UpdateChecker/              # Fab scraper + local source watcher + snapshots
├── Resources/
│   ├── BlenderAddon/               # blender_asset_browser_bridge.py
│   ├── Models/                     # siglip2_vision_fp16.onnx (≈178 MB via Git LFS)
│   ├── TagVocabulary/              # default_tags.json (189 tags) + embeddings
│   └── MergeDriver/                # assetlib_merge.py (3-way merge for sidecars)
├── ThirdParty/
│   ├── assimp/                     # 6.0.5 — external mesh preview
│   └── onnxruntime/                # 1.20.1 SDK (matches UE 5.6 NNE bundle)
└── Config/
```

## ❤️ Support development

This plugin is **free and Apache 2.0** — no fees, no telemetry, no upsells.
It will stay that way.

If it saved you time and you'd like to say thanks, donations are welcome but
never expected. I'm currently saving for an **RTX 5090** to keep iterating on
the AI side of the plugin (larger tag vocabularies, faster local inference,
fine-tuning on UE-asset thumbnails). Every bit helps me get there sooner.

**Crypto** (preferred — instant, 0% platform fees):
- **USDT (TRC20):** `TJ5uvnYXHgGALH4M9w4pH2kA1UsyzRYCD1` *(~1¢ network fee)*

**Free things that also help a lot:**
- Hit the ⭐ **Star** button at the top of this page — boosts visibility on GitHub
- Open an issue if you hit a bug, or a PR if you fixed something
- Tell another Unreal dev about the plugin

## License

Apache 2.0. Third-party components listed in `BlenderAssetBrowser/THIRD_PARTY_NOTICES.txt`.
