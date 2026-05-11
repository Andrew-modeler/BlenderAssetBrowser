// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// When the user opens a source file in Substance / Photoshop / Max via
// FDCCLauncher, we register that file with FDCCReimportWatcher. The watcher
// uses DirectoryWatcher on the file's directory and triggers an automatic
// asset reimport when the source file's mtime moves forward.
//
// This generalizes the Blender bridge pattern to all DCC tools — they don't
// have to write to an exchange folder, they just save in place and we react.

#pragma once

#include "CoreMinimal.h"

class FDCCReimportWatcher
{
public:
	void Initialize();
	void Shutdown();

	/** Register a source file we care about. AssetPackageName is the UE
	 *  asset that should be reimported when the file changes. */
	void Register(const FString& AbsoluteSourceFile, const FString& AssetPackageName);

	/** Stop tracking a source file. */
	void Unregister(const FString& AbsoluteSourceFile);

private:
	void OnDirectoryChanged(const TArray<struct FFileChangeData>& Changes);
	void OnWatchedDirectoryChanged(const FString& WatchedDir, const TArray<struct FFileChangeData>& Changes);

	/** {dir} -> handle (DirectoryWatcher). One handle per unique parent dir. */
	TMap<FString, FDelegateHandle> WatchHandles;

	/** absolute source file -> {asset package name, last seen mtime}. */
	struct FEntry { FString AssetPackage; int64 LastMtime = 0; };
	TMap<FString, FEntry> Tracked;
};
