// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Mounts an external folder so its uassets are visible to UE's Asset Registry
// while the editor is running. The mount is transient (not persisted to .uproject)
// — restoring on next launch is the responsibility of the subsystem.
//
// SECURITY: refuses to mount system folders, paths with traversal, or non-existent
// directories. All paths are canonicalized before any FCoreDelegates call.

#pragma once

#include "CoreMinimal.h"

class BLENDERASSETBROWSER_API FExternalLibraryMount
{
public:
	/**
	 * Mount `AbsolutePath` so it appears under `/AssetLibraries/<MountName>/` in
	 * the Asset Registry. `MountName` is the virtual subfolder; must be a single
	 * safe identifier (alphanumeric + dash/underscore, max 64 chars).
	 *
	 * Returns true on success. On failure, no side effects.
	 */
	static bool Mount(const FString& AbsolutePath, const FString& MountName);

	/** Unmount a previously-mounted library by name. */
	static bool Unmount(const FString& MountName);

	/** True if a mount with this name is active. */
	static bool IsMounted(const FString& MountName);

	/** Virtual content path corresponding to a mount name (e.g. /AssetLibraries/Personal/). */
	static FString GetVirtualRoot(const FString& MountName);

	/** Validate MountName: alnum + - + _, length 1..64. */
	static bool IsValidMountName(const FString& MountName);
};
