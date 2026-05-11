// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "ExternalLibraryMount.h"
#include "AssetLibraryTypes.h"
#include "AssetLibrarySettings.h"
#include "BlenderAssetBrowserModule.h"

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/EngineVersion.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace
{
	/** Heuristic block on system folders. Not exhaustive — combined with
	 *  IsPathInsideRoot at higher layers, this is defense-in-depth. */
	bool IsForbiddenRoot(const FString& Canonical)
	{
		static const TArray<FString> Forbidden = {
			TEXT("C:/Windows"),
			TEXT("C:\\Windows"),
			TEXT("C:/Program Files/Microsoft"),
			TEXT("C:\\Program Files\\Microsoft"),
		};
		for (const FString& F : Forbidden)
		{
			if (Canonical.StartsWith(F, ESearchCase::IgnoreCase)) { return true; }
		}
		return false;
	}

	/** mount name -> disk path we registered. Needed because UnRegisterMountPoint
	 *  requires the same disk path that was passed to RegisterMountPoint. */
	static TMap<FString, FString>& GetMountRegistry()
	{
		static TMap<FString, FString> Registry;
		return Registry;
	}
}

bool FExternalLibraryMount::IsValidMountName(const FString& MountName)
{
	if (MountName.IsEmpty() || MountName.Len() > 64) { return false; }
	for (TCHAR C : MountName)
	{
		const bool bOk = FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_');
		if (!bOk) { return false; }
	}
	return true;
}

FString FExternalLibraryMount::GetVirtualRoot(const FString& MountName)
{
	if (!IsValidMountName(MountName)) { return FString(); }
	return FString::Printf(TEXT("/AssetLibraries/%s/"), *MountName);
}

bool FExternalLibraryMount::IsMounted(const FString& MountName)
{
	const FString Virt = GetVirtualRoot(MountName);
	if (Virt.IsEmpty()) { return false; }

	const FString Stripped = Virt.LeftChop(1); // drop trailing /
	return FPackageName::MountPointExists(Stripped);
}

bool FExternalLibraryMount::Mount(const FString& AbsolutePath, const FString& MountName)
{
	if (!IsValidMountName(MountName))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Mount: invalid name '%s'"), *MountName);
		return false;
	}

	const FString Canonical = FPaths::ConvertRelativePathToFull(AbsolutePath);
	if (Canonical.IsEmpty() || Canonical.Contains(TEXT("..")))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Mount: bad path '%s'"), *AbsolutePath);
		return false;
	}

	if (Canonical.Len() > BAB::MAX_PATH_LEN)
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Mount: path too long"));
		return false;
	}

	if (IsForbiddenRoot(Canonical))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Mount: refusing system folder '%s'"), *Canonical);
		return false;
	}

	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!PFM.DirectoryExists(*Canonical))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Mount: directory does not exist: %s"), *Canonical);
		return false;
	}

	if (IsMounted(MountName))
	{
		UE_LOG(LogBlenderAssetBrowser, Log, TEXT("Mount: '%s' already mounted"), *MountName);
		return true;
	}

	const FString VirtualRoot = GetVirtualRoot(MountName);

	// FPackageName expects path with trailing slash on the disk side.
	FString DiskRoot = Canonical;
	if (!DiskRoot.EndsWith(TEXT("/")) && !DiskRoot.EndsWith(TEXT("\\")))
	{
		DiskRoot.AppendChar(TEXT('/'));
	}

	FPackageName::RegisterMountPoint(VirtualRoot, DiskRoot);
	GetMountRegistry().Add(MountName, DiskRoot);
	UE_LOG(LogBlenderAssetBrowser, Log, TEXT("Mounted '%s' -> '%s'"), *VirtualRoot, *DiskRoot);

	// Engine-version compatibility check. The library may carry a metadata file
	// recording the engine it was authored with. If newer than the current
	// editor, log a warning so the user knows assets may fail to load.
	const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>();
	if (S && S->bWarnOnNewerEngineVersion)
	{
		const FString MetaPath = Canonical / TEXT(".assetlib_meta.json");
		FString Contents;
		if (FFileHelper::LoadFileToString(Contents, *MetaPath) &&
		    Contents.Len() > 0 && Contents.Len() < BAB::MAX_FILE_BYTES_JSON)
		{
			TSharedPtr<FJsonObject> Obj;
			auto Reader = TJsonReaderFactory<>::Create(Contents);
			if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
			{
				FString LibEngineStr;
				if (Obj->TryGetStringField(TEXT("engine_version"), LibEngineStr) && !LibEngineStr.IsEmpty())
				{
					// Bug #2 fix: numeric component-wise compare. Lexicographic
					// "5.10.0" < "5.6.0" by ASCII because '1' < '6'. Parse and
					// compare numerically so "5.10" > "5.6" correctly.
					FEngineVersion LibVer;
					if (FEngineVersion::Parse(LibEngineStr, LibVer))
					{
						const FEngineVersion& MyVer = FEngineVersion::Current();
						const EVersionComparison Cmp = FEngineVersion::GetNewest(MyVer, LibVer, nullptr);
						if (Cmp == EVersionComparison::Second) // Library is newer
						{
							UE_LOG(LogBlenderAssetBrowser, Warning,
								TEXT("Library '%s' was authored on engine %s — newer than this editor (%s). "
								     "Some assets may fail to load."),
								*MountName, *LibEngineStr,
								*MyVer.ToString(EVersionComponent::Patch));
						}
					}
					else
					{
						UE_LOG(LogBlenderAssetBrowser, Log,
							TEXT("Library '%s' has unparseable engine_version '%s' — skipping compat check."),
							*MountName, *LibEngineStr);
					}
				}
			}
		}
	}
	return true;
}

bool FExternalLibraryMount::Unmount(const FString& MountName)
{
	if (!IsValidMountName(MountName)) { return false; }
	if (!IsMounted(MountName)) { return true; }

	const FString* DiskRootPtr = GetMountRegistry().Find(MountName);
	if (!DiskRootPtr)
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Unmount: no record of disk root for '%s'"), *MountName);
		return false;
	}

	const FString VirtualRoot = GetVirtualRoot(MountName);
	FPackageName::UnRegisterMountPoint(VirtualRoot, *DiskRootPtr);
	GetMountRegistry().Remove(MountName);
	UE_LOG(LogBlenderAssetBrowser, Log, TEXT("Unmounted '%s'"), *MountName);
	return true;
}
