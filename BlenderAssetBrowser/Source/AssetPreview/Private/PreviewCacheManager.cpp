// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "PreviewCacheManager.h"
#include "AssetPreviewRenderer.h"
#include "AssetPreviewModule.h"

#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"

namespace
{
	static FString GCacheRoot;
	static FCriticalSection GCacheMutex;

	/** SHA1-based deterministic name. We only use the hash for filename uniqueness;
	 *  no security claim — it's not a password, just a deduplicating key.
	 *
	 *  Bug #10 fix: the key now includes the .uasset file mtime, so reimporting
	 *  an asset invalidates the cache for it automatically. mtime is a much
	 *  more reliable signal than PackageGuid here because newer UE doesn't
	 *  reliably expose PackageGuid on FAssetData. */
	FString HashKey(const FAssetData& Asset, int32 Size)
	{
		FString Composite = Asset.GetObjectPathString();
		Composite.Append(TEXT("|"));
		Composite.AppendInt(Size);

		// Append the .uasset file mtime as the invalidation token. If we can't
		// resolve the file we fall back to path-only — equivalent to old
		// behaviour, so we never produce a worse key.
		FString DiskPath;
		if (FPackageName::TryConvertLongPackageNameToFilename(
				Asset.PackageName.ToString(), DiskPath,
				FPackageName::GetAssetPackageExtension()))
		{
			const FDateTime Mtime = IFileManager::Get().GetTimeStamp(*DiskPath);
			if (Mtime != FDateTime::MinValue())
			{
				Composite.Append(TEXT("|"));
				Composite.AppendInt(Mtime.ToUnixTimestamp());
			}
		}

		FSHA1 Sha;
		const FTCHARToUTF8 Utf8(*Composite);
		Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		Sha.Final();

		uint8 Hash[20];
		Sha.GetHash(Hash);

		FString Hex;
		Hex.Reserve(40);
		for (int32 i = 0; i < 20; ++i)
		{
			Hex.Appendf(TEXT("%02x"), Hash[i]);
		}
		return Hex;
	}

	bool IsUnderCacheRoot(const FString& Path)
	{
		if (GCacheRoot.IsEmpty()) { return false; }
		const FString Canonical = FPaths::ConvertRelativePathToFull(Path);
		const FString Root = FPaths::ConvertRelativePathToFull(GCacheRoot);
		return Canonical.StartsWith(Root, ESearchCase::IgnoreCase);
	}
}

bool FPreviewCacheManager::Init()
{
	FScopeLock Lock(&GCacheMutex);
	if (!GCacheRoot.IsEmpty()) { return true; }

	GCacheRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())
		/ TEXT("BlenderAssetBrowser/PreviewCache");

	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!PFM.DirectoryExists(*GCacheRoot))
	{
		PFM.CreateDirectoryTree(*GCacheRoot);
	}

	const bool bOk = PFM.DirectoryExists(*GCacheRoot);
	if (!bOk)
	{
		UE_LOG(LogAssetPreview, Error, TEXT("Failed to create cache root: %s"), *GCacheRoot);
		GCacheRoot.Empty();
	}
	else
	{
		UE_LOG(LogAssetPreview, Log, TEXT("Preview cache root: %s"), *GCacheRoot);
	}
	return bOk;
}

FString FPreviewCacheManager::GetCacheRoot()
{
	FScopeLock Lock(&GCacheMutex);
	return GCacheRoot;
}

FString FPreviewCacheManager::GetCachePath(const FAssetData& Asset, int32 Size)
{
	FScopeLock Lock(&GCacheMutex);
	if (GCacheRoot.IsEmpty() || !Asset.IsValid()) { return FString(); }
	return GCacheRoot / (HashKey(Asset, Size) + TEXT(".png"));
}

bool FPreviewCacheManager::HasCached(const FAssetData& Asset, int32 Size)
{
	const FString Path = GetCachePath(Asset, Size);
	if (Path.IsEmpty()) { return false; }
	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	return PFM.FileExists(*Path) && PFM.FileSize(*Path) > 0;
}

FString FPreviewCacheManager::GetOrRender(const FAssetData& Asset, int32 Size)
{
	if (!Asset.IsValid()) { return FString(); }
	const FString Path = GetCachePath(Asset, Size);
	if (Path.IsEmpty()) { return FString(); }
	if (!IsUnderCacheRoot(Path))
	{
		UE_LOG(LogAssetPreview, Error, TEXT("Computed cache path escapes root: %s"), *Path);
		return FString();
	}

	if (HasCached(Asset, Size))
	{
		return Path;
	}

	if (FAssetPreviewRenderer::RenderThumbnail(Asset, Size, Path))
	{
		return Path;
	}
	return FString();
}

bool FPreviewCacheManager::Invalidate(const FAssetData& Asset, int32 Size)
{
	const FString Path = GetCachePath(Asset, Size);
	if (Path.IsEmpty() || !IsUnderCacheRoot(Path)) { return false; }

	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (PFM.FileExists(*Path))
	{
		return PFM.DeleteFile(*Path);
	}
	return true; // nothing to invalidate
}

int64 FPreviewCacheManager::GetTotalSizeBytes()
{
	FScopeLock Lock(&GCacheMutex);
	if (GCacheRoot.IsEmpty()) { return 0; }

	int64 Total = 0;
	IFileManager& FM = IFileManager::Get();
	TArray<FString> Files;
	FM.FindFiles(Files, *(GCacheRoot / TEXT("*.png")), true, false);
	for (const FString& F : Files)
	{
		Total += FM.FileSize(*(GCacheRoot / F));
	}
	return Total;
}

int32 FPreviewCacheManager::EvictOldest(int64 TargetBytes)
{
	FScopeLock Lock(&GCacheMutex);
	if (GCacheRoot.IsEmpty()) { return 0; }

	IFileManager& FM = IFileManager::Get();
	TArray<FString> Files;
	FM.FindFiles(Files, *(GCacheRoot / TEXT("*.png")), true, false);

	struct FEntry { FString Path; FDateTime Mtime; int64 Size; };
	TArray<FEntry> Entries;
	Entries.Reserve(Files.Num());

	int64 Total = 0;
	for (const FString& F : Files)
	{
		FEntry E;
		E.Path  = GCacheRoot / F;
		E.Mtime = FM.GetTimeStamp(*E.Path);
		E.Size  = FM.FileSize(*E.Path);
		Total += FMath::Max<int64>(0, E.Size);
		Entries.Add(MoveTemp(E));
	}

	if (Total <= TargetBytes) { return 0; }

	Entries.Sort([](const FEntry& A, const FEntry& B) { return A.Mtime < B.Mtime; });

	int32 Removed = 0;
	for (const FEntry& E : Entries)
	{
		if (Total <= TargetBytes) { break; }
		if (FM.Delete(*E.Path))
		{
			Total -= E.Size;
			++Removed;
		}
	}

	UE_LOG(LogAssetPreview, Log, TEXT("Cache eviction: removed %d files, new total ~%lld bytes"),
		Removed, Total);
	return Removed;
}
