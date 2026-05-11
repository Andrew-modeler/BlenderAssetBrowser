"""
blender_asset_browser_bridge.py
================================
Blender addon — companion to the UE plugin "BlenderAssetBrowser".

Install (in Blender):
  Edit > Preferences > Add-ons > Install...  pick this file > enable.

Usage:
  1) From UE: right-click an asset > "Edit in Blender". UE exports an FBX into
     the project's exchange folder and (when configured) launches Blender.
  2) In Blender: 3D Viewport > N-panel > "Unreal" tab.
        - "Send to Unreal (Update Existing)"  — writes back to the original asset.
        - "Send to Unreal (New Asset)"        — pick a project + target folder.
  3) UE watches the exchange `incoming/` folder and reimports automatically.

Security:
  - Target paths must start with /Game/  (the addon refuses any other input).
  - No code is downloaded or executed from UE; the bridge is file-based only.
"""

bl_info = {
    "name": "Blender Asset Browser Bridge",
    "author": "BlenderAssetBrowser Contributors",
    "version": (0, 1, 0),
    "blender": (3, 6, 0),
    "location": "View3D > Sidebar > Unreal",
    "description": "Two-way FBX bridge to the UE plugin BlenderAssetBrowser.",
    "category": "Import-Export",
}

import bpy
import os
import json
import re
import tempfile

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------

UE_PATH_RE = re.compile(r"^/Game/[A-Za-z0-9_./-]{1,1024}$")
NAME_RE    = re.compile(r"^[A-Za-z0-9_-]{1,256}$")


def _is_safe_ue_path(path: str) -> bool:
    return bool(UE_PATH_RE.match(path)) and (".." not in path)


def _is_safe_name(name: str) -> bool:
    return bool(NAME_RE.match(name))


def _exchange_root_from_manifest(scene) -> str:
    """Persist the exchange folder per-scene so users don't re-pick it every time."""
    return scene.get("ue_exchange_root", "")


def _read_manifest(exchange_root: str) -> dict:
    path = os.path.join(exchange_root, "manifest.json")
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def _write_incoming(exchange_root: str, name: str, target_ue_path: str, is_new: bool, overwrite: bool) -> str:
    """Export the active scene's mesh to incoming/<name>.fbx and write the meta JSON LAST."""
    incoming = os.path.join(exchange_root, "incoming")
    os.makedirs(incoming, exist_ok=True)

    fbx_path  = os.path.join(incoming, name + ".fbx")
    meta_path = os.path.join(incoming, name + ".meta.json")

    # Select all mesh objects for export.
    bpy.ops.object.select_all(action="DESELECT")
    n_meshes = 0
    for obj in bpy.context.view_layer.objects:
        if obj.type in {"MESH", "EMPTY"}:
            obj.select_set(True)
            n_meshes += 1
    if n_meshes == 0:
        raise RuntimeError("Nothing to export — scene has no mesh objects.")

    bpy.ops.export_scene.fbx(
        filepath=fbx_path,
        use_selection=True,
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        axis_forward="-Z",
        axis_up="Y",
        object_types={"MESH", "EMPTY"},
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        use_tspace=True,
        bake_anim=False,
    )

    # Atomic-ish: write meta JSON LAST so UE's directory watcher doesn't fire
    # while the FBX is still being written.
    meta = {
        "target_ue_path": target_ue_path,
        "is_new":         bool(is_new),
        "overwrite":      bool(overwrite),
    }
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(meta, f)
    return fbx_path


# -----------------------------------------------------------------------------
# Operators
# -----------------------------------------------------------------------------

class UNREAL_OT_set_exchange(bpy.types.Operator):
    bl_idname = "unreal.set_exchange"
    bl_label = "Set Exchange Folder"
    bl_description = "Point this scene at the UE plugin's exchange folder"

    directory: bpy.props.StringProperty(subtype="DIR_PATH")

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        d = bpy.path.abspath(self.directory)
        if not os.path.isdir(d):
            self.report({"ERROR"}, "Not a directory.")
            return {"CANCELLED"}
        # SECURITY: must contain a manifest.json from our UE plugin.
        if not os.path.isfile(os.path.join(d, "manifest.json")):
            self.report({"ERROR"}, "No manifest.json — pick the plugin's exchange folder.")
            return {"CANCELLED"}
        context.scene["ue_exchange_root"] = d
        self.report({"INFO"}, f"Exchange folder set: {d}")
        return {"FINISHED"}


class UNREAL_OT_send_existing(bpy.types.Operator):
    bl_idname = "unreal.send_existing"
    bl_label = "Send to Unreal (Update Existing)"
    bl_description = "Export FBX back to the original UE asset"

    def execute(self, context):
        scene = context.scene
        exchange = _exchange_root_from_manifest(scene)
        target = scene.get("ue_target_path", "")
        if not exchange or not _is_safe_ue_path(target):
            self.report({"ERROR"}, "No round-trip context. Use 'Send as New' instead.")
            return {"CANCELLED"}

        name = target.rsplit("/", 1)[-1]
        if not _is_safe_name(name):
            self.report({"ERROR"}, "Asset name has unsafe characters.")
            return {"CANCELLED"}

        try:
            _write_incoming(exchange, name, target, is_new=False, overwrite=True)
        except Exception as e:
            self.report({"ERROR"}, f"Export failed: {e}")
            return {"CANCELLED"}
        self.report({"INFO"}, f"Sent to {target}")
        return {"FINISHED"}


class UNREAL_OT_send_new(bpy.types.Operator):
    bl_idname = "unreal.send_new"
    bl_label = "Send to Unreal (New Asset)"
    bl_description = "Pick a project + target folder, then export as a new asset"

    asset_name:   bpy.props.StringProperty(name="Asset Name", default="NewAsset")
    target_folder: bpy.props.StringProperty(name="Target Folder", default="/Game/Library")

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        scene = context.scene
        exchange = _exchange_root_from_manifest(scene)
        if not exchange:
            self.report({"ERROR"}, "Pick exchange folder first.")
            return {"CANCELLED"}

        if not _is_safe_name(self.asset_name):
            self.report({"ERROR"}, "Asset name must be alnum/-/_, 1..256 chars.")
            return {"CANCELLED"}

        folder = self.target_folder.rstrip("/")
        if not folder.startswith("/Game"):
            self.report({"ERROR"}, "Target folder must start with /Game/")
            return {"CANCELLED"}

        target = folder + "/" + self.asset_name
        if not _is_safe_ue_path(target):
            self.report({"ERROR"}, "Resulting UE path is not safe.")
            return {"CANCELLED"}

        try:
            _write_incoming(exchange, self.asset_name, target, is_new=True, overwrite=False)
        except Exception as e:
            self.report({"ERROR"}, f"Export failed: {e}")
            return {"CANCELLED"}

        self.report({"INFO"}, f"New asset queued: {target}")
        return {"FINISHED"}


# -----------------------------------------------------------------------------
# Panel
# -----------------------------------------------------------------------------

class UNREAL_PT_panel(bpy.types.Panel):
    bl_label = "Unreal Bridge"
    bl_idname = "UNREAL_PT_panel"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Unreal"

    def draw(self, context):
        layout = self.layout
        scene = context.scene

        exchange = _exchange_root_from_manifest(scene)
        if exchange:
            manifest = _read_manifest(exchange)
            proj = manifest.get("project_name", "(unknown)")
            layout.label(text=f"Project: {proj}", icon="WORKSPACE")
            layout.label(text=os.path.basename(exchange.rstrip("/\\")))
        else:
            layout.label(text="No exchange folder", icon="ERROR")

        layout.operator("unreal.set_exchange", icon="FILE_FOLDER")
        layout.separator()

        target = scene.get("ue_target_path", "")
        if target:
            layout.label(text="Round-trip:")
            layout.label(text=target)
            row = layout.row()
            row.scale_y = 1.6
            row.operator("unreal.send_existing", icon="EXPORT")
        layout.separator()
        row = layout.row()
        row.scale_y = 1.4
        row.operator("unreal.send_new", icon="PLUS")


# -----------------------------------------------------------------------------
# Register
# -----------------------------------------------------------------------------

_classes = [
    UNREAL_OT_set_exchange,
    UNREAL_OT_send_existing,
    UNREAL_OT_send_new,
    UNREAL_PT_panel,
]


def register():
    for c in _classes:
        bpy.utils.register_class(c)


def unregister():
    for c in reversed(_classes):
        bpy.utils.unregister_class(c)


if __name__ == "__main__":
    register()
