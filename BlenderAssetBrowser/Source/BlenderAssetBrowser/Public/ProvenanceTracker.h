// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Detect where an asset came from: Fab marketplace cache, Megascans pack,
// or a locally-imported source file. Read-only access to filesystem.
//
// SECURITY: Vault Cache is scanned read-only. We never write anywhere outside
// the plugin's own folders. All paths canonicalized before stat/read.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "AssetLibraryTypes.h"

struct BLENDERASSETBROWSER_API FProvenanceInfo
{
	EAssetLibrarySource Source = EAssetLibrarySource::Unknown;
	FString PackName;
	FString PackId;
	FString Author;
	FString License;
	FString Version;
	FString Url;
	FString LocalSourceFile;     // for Imported assets
};

class BLENDERASSETBROWSER_API FProvenanceTracker
{
public:
	/** Inspect `Data` and return a best-effort FProvenanceInfo. Safe to call
	 *  from any thread; reads Asset Registry tags + filesystem. */
	static FProvenanceInfo Detect(const FAssetData& Data);

	/** Path to the Fab/Epic Launcher Vault Cache, or empty if not present. */
	static FString GetFabVaultCachePath();
};
