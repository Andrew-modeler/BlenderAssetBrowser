// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "DCCReimportWatcher.h"
#include "BlenderBridgeModule.h"

#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "EditorReimportHandler.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"

namespace
{
	FString NormDir(const FString& AbsFile)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::GetPath(AbsFile));
	}
}

void FDCCReimportWatcher::Initialize() {}
void FDCCReimportWatcher::Shutdown()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher"))) { return; }
	FDirectoryWatcherModule& DW = FModuleManager::GetModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* W = DW.Get();
	if (!W) { return; }
	for (auto& Pair : WatchHandles)
	{
		W->UnregisterDirectoryChangedCallback_Handle(Pair.Key, Pair.Value);
	}
	WatchHandles.Reset();
	Tracked.Reset();
}

void FDCCReimportWatcher::Register(const FString& AbsoluteSourceFile, const FString& AssetPackageName)
{
	if (AbsoluteSourceFile.IsEmpty() || AssetPackageName.IsEmpty()) { return; }
	if (AbsoluteSourceFile.Contains(TEXT(".."))) { return; }

	IFileManager& FM = IFileManager::Get();
	const FDateTime Mtime = FM.GetTimeStamp(*AbsoluteSourceFile);
	if (Mtime == FDateTime::MinValue()) { return; }

	FEntry E;
	E.AssetPackage = AssetPackageName;
	E.LastMtime    = Mtime.ToUnixTimestamp();
	Tracked.Add(FPaths::ConvertRelativePathToFull(AbsoluteSourceFile), MoveTemp(E));

	const FString Dir = NormDir(AbsoluteSourceFile);
	if (!WatchHandles.Contains(Dir))
	{
		FDirectoryWatcherModule& DW = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		IDirectoryWatcher* W = DW.Get();
		if (W)
		{
			FDelegateHandle H;
			W->RegisterDirectoryChangedCallback_Handle(
				Dir,
				IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FDCCReimportWatcher::OnDirectoryChanged),
				H);
			WatchHandles.Add(Dir, H);
		}
	}
	UE_LOG(LogBlenderBridge, Log, TEXT("DCC reimport watch: %s → %s"),
		*AbsoluteSourceFile, *AssetPackageName);
}

void FDCCReimportWatcher::Unregister(const FString& AbsoluteSourceFile)
{
	Tracked.Remove(FPaths::ConvertRelativePathToFull(AbsoluteSourceFile));
}

void FDCCReimportWatcher::OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
	if (!GEditor) { return; }
	IFileManager& FM = IFileManager::Get();
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	for (const FFileChangeData& Ch : Changes)
	{
		const FString AbsFile = FPaths::ConvertRelativePathToFull(Ch.Filename);
		FEntry* E = Tracked.Find(AbsFile);
		if (!E) { continue; }

		const FDateTime Now = FM.GetTimeStamp(*AbsFile);
		if (Now == FDateTime::MinValue()) { continue; }
		const int64 NowTs = Now.ToUnixTimestamp();
		if (NowTs <= E->LastMtime) { continue; }
		E->LastMtime = NowTs;

		// Look up the target asset and queue reimport.
		FAssetData Data = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(E->AssetPackage));
		UObject* Asset = Data.IsValid() ? Data.GetAsset() : nullptr;
		if (Asset)
		{
			if (auto* RM = FReimportManager::Instance())
			{
				RM->ReimportAsync(Asset, /*bAskForNewFileIfMissing*/ false,
					/*bShowNotification*/ true, AbsFile);
				UE_LOG(LogBlenderBridge, Log,
					TEXT("DCC reimport: %s ← %s"), *E->AssetPackage, *AbsFile);
			}
		}
	}
}
