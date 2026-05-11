# Blender Addon — Unreal Bridge

`blender_asset_browser_bridge.py` is a single-file Blender addon that talks
to the UE plugin via a file-based exchange folder.

## Install

In Blender:
1. **Edit > Preferences > Add-ons > Install...**
2. Pick `blender_asset_browser_bridge.py`.
3. Tick the checkbox next to "Blender Asset Browser Bridge".

The panel appears in the 3D Viewport sidebar (`N` key) under the "Unreal" tab.

## First-time setup

You need to point the addon at the UE plugin's exchange folder:
1. Open any UE project that has the BlenderAssetBrowser plugin enabled.
   The plugin creates the exchange folder at
   `{Project}/Saved/BlenderAssetBrowser/Exchange/` on first editor start.
2. In Blender's "Unreal" panel, click **Set Exchange Folder** and pick that
   folder. The addon will only accept folders that contain a `manifest.json`
   written by the UE plugin.

## Flows

### Round-trip edit (UE → Blender → UE)
UE: Right-click an asset → "Edit in Blender" (NOT yet wired in MVP — uses
generic Blender launch + file dialog for now).
Blender opens with the mesh imported. After editing, click **Send to Unreal
(Update Existing)**. UE auto-reimports because the addon writes the FBX +
`*.meta.json` to `incoming/`, and UE's `DirectoryWatcher` picks it up.

### New asset (Blender → UE)
Open Blender independently. Click **Send to Unreal (New Asset)**, set
asset name + target `/Game/...` folder, click OK.

## Security

The addon enforces:
- Asset names are alnum + `-`/`_` only.
- Target paths must start with `/Game/`.
- No paths containing `..`.
- Exchange folder must contain the UE-side `manifest.json` (proves you
  pointed at a real plugin install, not an attacker's folder).

UE-side validation is stricter still — see `BlenderBridgeManager.cpp`.
