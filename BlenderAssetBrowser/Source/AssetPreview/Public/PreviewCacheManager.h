// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Disk cache for thumbnails. Keys are SHA1 of (asset_path + size + content-hash)
// stored inside the cache root directory only — refuses to write anywhere else.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class ASSETPREVIEW_API FPreviewCacheManager
{
public:
	/** Initialize cache root. Returns true if directory is writable. */
	static bool Init();

	/** Absolute path to the cache root. Empty if not initialized. */
	static FString GetCacheRoot();

	/** Absolute path where the thumbnail for (Asset, Size) would live (does NOT check existence). */
	static FString GetCachePath(const FAssetData& Asset, int32 Size);

	/** True if a non-empty cache file already exists for (Asset, Size). */
	static bool HasCached(const FAssetData& Asset, int32 Size);

	/**
	 * Get or render thumbnail. Renders via FAssetPreviewRenderer when missing.
	 * Returns the path of the resulting PNG (existing or freshly rendered),
	 * empty on failure.
	 */
	static FString GetOrRender(const FAssetData& Asset, int32 Size);

	/** Drop cached file for (Asset, Size). No-op if it doesn't exist. */
	static bool Invalidate(const FAssetData& Asset, int32 Size);

	/** Approximate cache size in bytes (sum of *.png files in cache root). */
	static int64 GetTotalSizeBytes();

	/** Delete oldest files until total <= TargetBytes. Returns number of files removed. */
	static int32 EvictOldest(int64 TargetBytes);
};
