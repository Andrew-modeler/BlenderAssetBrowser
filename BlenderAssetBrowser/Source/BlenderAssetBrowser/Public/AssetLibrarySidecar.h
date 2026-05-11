// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// JSON sidecar export/import — VCS-friendly serialization of the library.
// Output schema is line-stable so diffs/merges are predictable. The companion
// Python merge driver lives at Resources/MergeDriver/assetlib_merge.py.
//
// SECURITY:
//   - Every imported entry is Validate()-ed before being committed to DB.
//   - File size capped to BAB::MAX_FILE_BYTES_JSON.
//   - No code is executed during import; values are bound parameters only.

#pragma once

#include "CoreMinimal.h"

class UAssetLibrarySubsystem;

class BLENDERASSETBROWSER_API FAssetLibrarySidecar
{
public:
	/** Export the entire library DB to a `.assetlib` JSON file. */
	static bool ExportToFile(UAssetLibrarySubsystem* Subsystem, const FString& OutAbsolutePath);

	/** Import (merge) a `.assetlib` into the live DB. Conflicts resolved by
	 *  last-writer-wins for scalars, union for tags & catalog membership. */
	static bool ImportFromFile(UAssetLibrarySubsystem* Subsystem, const FString& InAbsolutePath);
};
