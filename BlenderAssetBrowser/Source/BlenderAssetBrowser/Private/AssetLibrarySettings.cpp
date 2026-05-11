// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AssetLibrarySettings.h"
#include "AssetLibraryTypes.h"
#include "BlenderAssetBrowserModule.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

UAssetLibrarySettings::UAssetLibrarySettings() = default;

#if WITH_EDITOR
void UAssetLibrarySettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = PropertyChangedEvent.GetPropertyName();

	// SECURITY: validate library paths after every edit. Reject:
	//   - empty path
	//   - non-existent path
	//   - path that contains ".." traversal markers
	//   - path under engine binaries / system folders (heuristic)
	auto IsAllowedRoot = [](const FString& Path) -> bool
	{
		if (Path.IsEmpty()) { return false; }
		const FString Canonical = FPaths::ConvertRelativePathToFull(Path);
		if (Canonical.Contains(TEXT(".."))) { return false; }

		// Heuristic block — never let users point a library at a system folder.
		const TCHAR* Forbidden[] = {
			TEXT("C:/Windows/"), TEXT("C:\\Windows\\"),
			TEXT("C:/Program Files/Microsoft"), TEXT("C:\\Program Files\\Microsoft")
		};
		for (const TCHAR* F : Forbidden)
		{
			if (Canonical.StartsWith(F, ESearchCase::IgnoreCase)) { return false; }
		}

		IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
		return PFM.DirectoryExists(*Canonical);
	};

	if (Name == GET_MEMBER_NAME_CHECKED(FExternalLibraryConfig, Path) ||
	    Name == GET_MEMBER_NAME_CHECKED(UAssetLibrarySettings, Libraries))
	{
		for (FExternalLibraryConfig& Lib : Libraries)
		{
			if (!IsAllowedRoot(Lib.Path.Path))
			{
				UE_LOG(LogBlenderAssetBrowser, Warning,
					TEXT("Library '%s' has invalid path '%s' — clearing."),
					*Lib.Name, *Lib.Path.Path);
				Lib.Path.Path.Empty();
			}
			if (Lib.Name.Len() > BAB::MAX_NAME_LEN)
			{
				Lib.Name = Lib.Name.Left(BAB::MAX_NAME_LEN);
			}
		}
	}

	if (Name == GET_MEMBER_NAME_CHECKED(UAssetLibrarySettings, BlenderExecutable))
	{
		const FString& Exe = BlenderExecutable.FilePath;
		if (!Exe.IsEmpty())
		{
			const FString Canonical = FPaths::ConvertRelativePathToFull(Exe);
			if (Canonical.Contains(TEXT("..")) || !Canonical.EndsWith(TEXT(".exe"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogBlenderAssetBrowser, Warning,
					TEXT("Blender executable path rejected: %s"), *Exe);
				BlenderExecutable.FilePath.Empty();
			}
		}
	}

	if (Name == GET_MEMBER_NAME_CHECKED(UAssetLibrarySettings, ExchangeSubfolder))
	{
		// Restrict to a relative subfolder under ProjectSaved.
		if (ExchangeSubfolder.Contains(TEXT("..")) ||
		    ExchangeSubfolder.Contains(TEXT(":")) ||
		    ExchangeSubfolder.StartsWith(TEXT("/")) ||
		    ExchangeSubfolder.StartsWith(TEXT("\\")))
		{
			UE_LOG(LogBlenderAssetBrowser, Warning,
				TEXT("ExchangeSubfolder must be relative — resetting."));
			ExchangeSubfolder = TEXT("BlenderAssetBrowser/Exchange");
		}
	}
}
#endif
