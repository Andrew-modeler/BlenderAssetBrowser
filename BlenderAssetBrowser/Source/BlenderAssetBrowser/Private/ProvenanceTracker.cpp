// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "ProvenanceTracker.h"
#include "BlenderAssetBrowserModule.h"

#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "EditorAssetLibrary.h"
#include "UObject/UObjectGlobals.h"
#include "EditorFramework/AssetImportData.h"

namespace
{
	/** Returns env var or empty string. */
	FString Env(const TCHAR* Name)
	{
		return FPlatformMisc::GetEnvironmentVariable(Name);
	}

	bool IsUnderPath(const FString& Asset, const FString& Folder)
	{
		if (Folder.IsEmpty()) { return false; }
		return Asset.StartsWith(Folder, ESearchCase::IgnoreCase);
	}
}

FString FProvenanceTracker::GetFabVaultCachePath()
{
	// Default location on Windows:
	//   %LOCALAPPDATA%\\EpicGamesLauncher\\VaultCache
	const FString LocalAppData = Env(TEXT("LOCALAPPDATA"));
	if (LocalAppData.IsEmpty()) { return FString(); }

	const FString Candidate = FPaths::ConvertRelativePathToFull(LocalAppData / TEXT("EpicGamesLauncher/VaultCache"));
	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (PFM.DirectoryExists(*Candidate)) { return Candidate; }

	return FString();
}

FProvenanceInfo FProvenanceTracker::Detect(const FAssetData& Data)
{
	FProvenanceInfo Out;
	if (!Data.IsValid()) { return Out; }

	const FString PackagePath = Data.PackagePath.ToString();

	// 1) Megascans heuristic — path under /Game/Megascans/ or /MS_/
	if (IsUnderPath(PackagePath, TEXT("/Game/Megascans")) ||
	    IsUnderPath(PackagePath, TEXT("/Game/MSPresets")) ||
	    PackagePath.Contains(TEXT("/Megascans/"), ESearchCase::IgnoreCase))
	{
		Out.Source = EAssetLibrarySource::Megascans;
		Out.PackName = TEXT("Megascans");
		Out.Author = TEXT("Quixel");
		Out.License = TEXT("Megascans EULA");
		return Out;
	}

	// 2) Fab cache heuristic — check if asset originated from VaultCache by
	//    matching the package short name in any of the cached pack directories.
	const FString FabRoot = GetFabVaultCachePath();
	if (!FabRoot.IsEmpty())
	{
		IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
		const FString ShortName = FPaths::GetBaseFilename(Data.PackageName.ToString());

		// Cheap scan: list pack folder names and look for any subfolder whose
		// "manifest.json" mentions this asset. We DO NOT parse the manifest
		// here in Phase 2.7 — that's later refinement. For now: presence of a
		// folder whose name matches a substring of the asset path is enough
		// to flag as Fab-origin.
		TArray<FString> PackDirs;
		PFM.IterateDirectory(*FabRoot,
			[&PackDirs](const TCHAR* P, bool bDir) -> bool
			{
				if (bDir) { PackDirs.Add(FPaths::GetCleanFilename(P)); }
				return true;
			});

		for (const FString& Pack : PackDirs)
		{
			if (Pack.IsEmpty()) { continue; }
			if (PackagePath.Contains(Pack, ESearchCase::IgnoreCase) ||
			    ShortName.Contains(Pack, ESearchCase::IgnoreCase))
			{
				Out.Source = EAssetLibrarySource::Fab;
				Out.PackName = Pack;
				Out.PackId = Pack;     // we'll refine with manifest parsing later
				Out.License = TEXT("Standard"); // user can override
				return Out;
			}
		}
	}

	// 3) Imported — has AssetImportData with a source file path on disk.
	// Bug #8 fix: FastGetAsset(false) returns null for unloaded assets,
	// which means provenance only worked for assets the user had opened.
	// Use GetAsset() which loads the package if needed. Loading a few KB
	// of metadata is fine; we cap to one asset per Detect() call.
	if (UObject* Obj = Data.GetAsset())
	{
		// Look for an UAssetImportData property the safe way: through reflection.
		// Only attempt if we already have the object loaded (no GetAsset() side effects).
		for (TFieldIterator<FObjectProperty> It(Obj->GetClass()); It; ++It)
		{
			FObjectProperty* Prop = *It;
			if (Prop->PropertyClass &&
				Prop->PropertyClass->IsChildOf(UAssetImportData::StaticClass()))
			{
				const UAssetImportData* Imp = Cast<UAssetImportData>(
					Prop->GetObjectPropertyValue_InContainer(Obj));
				if (Imp)
				{
					const FString First = Imp->GetFirstFilename();
					if (!First.IsEmpty())
					{
						Out.Source = EAssetLibrarySource::Imported;
						Out.LocalSourceFile = First;
						// Cap length defensively.
						Out.LocalSourceFile.LeftInline(BAB::MAX_PATH_LEN);
						return Out;
					}
				}
				break; // first match is enough
			}
		}
	}

	// Nothing matched — leave as Unknown for the user to set.
	return Out;
}
