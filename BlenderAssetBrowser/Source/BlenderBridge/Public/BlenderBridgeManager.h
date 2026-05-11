// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// File-based exchange with Blender:
//   {ProjectSaved}/BlenderAssetBrowser/Exchange/
//     manifest.json    project info
//     outgoing/        UE -> Blender  (FBX + .meta.json with target UE path)
//     incoming/        Blender -> UE  (FBX + .meta.json with target UE path)
//
// We watch `incoming/` with DirectoryWatcher and auto-reimport when a .meta.json
// lands. The Blender addon writes the .meta.json LAST so we never trigger on a
// half-written FBX.
//
// SECURITY:
//   - target paths in .meta.json must start with `/Game/` and contain no `..`
//   - FBX files must be smaller than MAX_FILE_BYTES_FBX
//   - manifest schema strictly validated; unknown keys ignored, type-checked
//   - Blender launched via CreateProc with argv array (no shell expansion)

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Containers/Ticker.h"

class FBlenderBridgeManager
{
public:
	FBlenderBridgeManager();
	~FBlenderBridgeManager();

	void Initialize();
	void Shutdown();

	/** Absolute path to the exchange root. */
	FString GetExchangeRoot() const;

	/**
	 * Export an asset to FBX in outgoing/ and launch Blender to open it.
	 * `UEAssetPath` is the long package name (/Game/...) so we can re-import
	 * back into it on round-trip.
	 */
	bool EditInBlender(const FString& UEAssetPath);

private:
	void EnsureFolders();
	void WriteProjectManifest();

	void OnIncomingDirChanged(const TArray<struct FFileChangeData>& Changes);
	bool TickQueue(float DeltaTime);

	void ProcessIncomingMeta(const FString& MetaPath);

	FString ExchangeRoot;
	FString IncomingDir;
	FString OutgoingDir;

	FDelegateHandle DirectoryWatcherHandle;

	struct FPending { FString FbxPath; FString TargetUEPath; bool bIsNew = false; };
	TArray<FPending> ReimportQueue;
	mutable FCriticalSection QueueLock;

	FTSTicker::FDelegateHandle TickerHandle;
};
