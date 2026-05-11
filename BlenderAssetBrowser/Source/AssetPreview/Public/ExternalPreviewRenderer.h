// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Renders preview PNGs for external mesh files (FBX/OBJ/GLTF/USD) WITHOUT
// importing them into the UE project first.
//
// Pipeline:
//   1. Assimp loads the file into an in-memory aiScene (with safety flags
//      and a file-size cap so a malformed file can't OOM the editor).
//   2. Filament renders one frame using IBL + a key directional light,
//      with camera auto-framed by the scene's AABB.
//   3. The pixel buffer is PNG-encoded via UE's IImageWrapper and written
//      into the preview cache.
//
// SECURITY:
//   - File-size cap before Assimp touches the bytes (BAB::MAX_FILE_BYTES_FBX).
//   - Assimp configured with ValidateDataStructure + sane vertex/face caps.
//   - All Assimp/Filament calls wrapped in try/catch.
//   - Output path verified to live under the cache root.

#pragma once

#include "CoreMinimal.h"

class ASSETPREVIEW_API FExternalPreviewRenderer
{
public:
	/** True if both Assimp and Filament loaded successfully at startup. */
	static bool IsAvailable();

	/**
	 * Render `InAbsoluteFilePath` (an .fbx/.obj/.gltf/.usd...) to a square
	 * PNG of `Size` pixels, written to `OutAbsolutePath`.
	 *
	 * Returns false on any error (file too big, parse failure, render error,
	 * write error). Never throws.
	 */
	static bool RenderToFile(const FString& InAbsoluteFilePath, int32 Size, const FString& OutAbsolutePath);

	/** Render and return the encoded PNG bytes (no file written). */
	static bool RenderToBytes(const FString& InAbsoluteFilePath, int32 Size, TArray<uint8>& OutPng);
};
