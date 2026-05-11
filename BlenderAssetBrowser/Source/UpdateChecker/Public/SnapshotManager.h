// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Local snapshots of .uasset files before applying a library update. Each
// snapshot is a flat folder of copied files under
// {ProjectSaved}/BlenderAssetBrowser/Snapshots/{ISO-timestamp}/.
//
// Rollback restores the files in-place. We never delete or rename source
// files — only copy and overwrite.

#pragma once

#include "CoreMinimal.h"

struct UPDATECHECKER_API FSnapshotEntry
{
	FString Timestamp;          // ISO-8601 folder name
	FString FolderPath;         // absolute path on disk
	TArray<FString> Files;      // original absolute paths that were snapshotted
};

class UPDATECHECKER_API FSnapshotManager
{
public:
	/** Create a snapshot copying the listed absolute file paths.
	 *  Returns the newly-created FSnapshotEntry (Timestamp empty on failure). */
	static FSnapshotEntry CreateSnapshot(const FString& Label, const TArray<FString>& AbsoluteFiles);

	/** List all snapshots in the project's snapshot root. */
	static TArray<FSnapshotEntry> ListSnapshots();

	/** Restore a snapshot: copy each stored file back to its original location.
	 *  Returns the number of files restored. */
	static int32 RestoreSnapshot(const FSnapshotEntry& Snapshot);

	/** Drop a snapshot folder. Use sparingly. */
	static bool DeleteSnapshot(const FSnapshotEntry& Snapshot);

	/** Cap on total disk space — evict oldest snapshots above this. */
	static int64 EvictOldest(int64 TargetBytes);
};
