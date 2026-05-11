// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Sync renderer for in-project uassets (StaticMesh, Material, Texture, BP, ...).
// Uses UE's built-in ThumbnailTools — the registered renderer for each asset
// class handles its own lighting/camera (matches Eleganza-style Studio_Icons
// behaviour for meshes, sphere/cube for materials, raw alpha for textures).
//
// External FBX/USD preview lives in `ExternalPreviewRenderer` (Phase 2.2).

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class ASSETPREVIEW_API FAssetPreviewRenderer
{
public:
	/**
	 * Render `Asset` to a PNG file at `OutAbsolutePath`. Square, `Size` pixels.
	 *
	 * MUST be called from the game thread (UE thumbnail tools require it).
	 *
	 * @param OutAbsolutePath  must be under the plugin's cache root (caller's job).
	 * @return true on success; false on any failure (object load error, render error,
	 *         PNG encode error, or write error).
	 */
	static bool RenderThumbnail(const FAssetData& Asset, int32 Size, const FString& OutAbsolutePath);

	/** Convenience: render and return the encoded PNG bytes (no file written). */
	static bool RenderToBytes(const FAssetData& Asset, int32 Size, TArray<uint8>& OutPng);
};
