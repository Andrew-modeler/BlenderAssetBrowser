// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SnapshotManager.h"
#include "UpdateCheckerModule.h"
#include "AssetLibraryTypes.h"

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	FString GetSnapshotRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())
			/ TEXT("BlenderAssetBrowser/Snapshots");
	}

	bool IsSafePath(const FString& P)
	{
		if (P.IsEmpty() || P.Len() > BAB::MAX_PATH_LEN) { return false; }
		if (P.Contains(TEXT(".."))) { return false; }
		return true;
	}

	bool WriteManifest(const FSnapshotEntry& E)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("timestamp"), E.Timestamp);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& F : E.Files)
		{
			Arr.Add(MakeShared<FJsonValueString>(F));
		}
		Root->SetArrayField(TEXT("files"), Arr);

		FString Out;
		auto Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return FFileHelper::SaveStringToFile(Out, *(E.FolderPath / TEXT("manifest.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

FSnapshotEntry FSnapshotManager::CreateSnapshot(const FString& Label, const TArray<FString>& AbsoluteFiles)
{
	FSnapshotEntry Out;
	if (AbsoluteFiles.Num() == 0 || AbsoluteFiles.Num() > 10000) { return Out; }

	const FString Ts = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	Out.Timestamp = Label.IsEmpty() ? Ts : (Ts + TEXT("_") + Label);
	Out.FolderPath = GetSnapshotRoot() / Out.Timestamp;
	if (!IsSafePath(Out.FolderPath)) { Out.Timestamp.Empty(); return Out; }

	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!PFM.DirectoryExists(*Out.FolderPath))
	{
		PFM.CreateDirectoryTree(*Out.FolderPath);
	}

	int32 OkCount = 0;
	for (const FString& Src : AbsoluteFiles)
	{
		if (!IsSafePath(Src)) { continue; }
		if (!PFM.FileExists(*Src)) { continue; }

		// Mirror the source directory under the snapshot folder using a
		// hash of the source path, so collisions across libraries are safe.
		const uint32 H = GetTypeHash(Src);
		const FString DstName = FString::Printf(TEXT("%08x_%s"), H, *FPaths::GetCleanFilename(Src));
		const FString DstPath = Out.FolderPath / DstName;
		if (PFM.CopyFile(*DstPath, *Src))
		{
			Out.Files.Add(Src);
			++OkCount;
		}
	}

	if (OkCount == 0)
	{
		PFM.DeleteDirectoryRecursively(*Out.FolderPath);
		Out.Timestamp.Empty();
		return Out;
	}

	WriteManifest(Out);
	UE_LOG(LogUpdateChecker, Log,
		TEXT("Snapshot '%s' created with %d file(s)."),
		*Out.Timestamp, OkCount);
	return Out;
}

TArray<FSnapshotEntry> FSnapshotManager::ListSnapshots()
{
	TArray<FSnapshotEntry> Out;
	const FString Root = GetSnapshotRoot();
	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!PFM.DirectoryExists(*Root)) { return Out; }

	struct FVisitor : public IPlatformFile::FDirectoryVisitor
	{
		TArray<FSnapshotEntry>* Sink;
		virtual bool Visit(const TCHAR* Path, bool bIsDir) override
		{
			if (!bIsDir) { return true; }
			FSnapshotEntry E;
			E.Timestamp  = FPaths::GetCleanFilename(Path);
			E.FolderPath = Path;
			// Try to read manifest.json for the file list.
			// SECURITY: cap size before LoadFileToString to defend against an attacker
			// who can write to Saved/.../Snapshots/ replacing a manifest with a giant
			// file. Without the cap we'd attempt to load it whole and DoS the editor.
			const FString ManifestPath = E.FolderPath / TEXT("manifest.json");
			IFileManager& ManifestFM = IFileManager::Get();
			const int64 ManifestSize = ManifestFM.FileSize(*ManifestPath);
			if (ManifestSize <= 0 || ManifestSize > BAB::MAX_FILE_BYTES_JSON)
			{
				Sink->Add(E);
				return true;
			}
			FString Json;
			if (FFileHelper::LoadFileToString(Json, *ManifestPath) && !Json.IsEmpty())
			{
				TSharedPtr<FJsonObject> Obj;
				auto Reader = TJsonReaderFactory<>::Create(Json);
				if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
					if (Obj->TryGetArrayField(TEXT("files"), Arr) && Arr)
					{
						for (const TSharedPtr<FJsonValue>& V : *Arr)
						{
							E.Files.Add(V->AsString());
						}
					}
				}
			}
			Sink->Add(E);
			return true;
		}
	};
	FVisitor V; V.Sink = &Out;
	PFM.IterateDirectory(*Root, V);

	// Sort newest first by folder name (ISO-ish timestamps sort lex correctly).
	Out.Sort([](const FSnapshotEntry& A, const FSnapshotEntry& B)
	{
		return A.Timestamp > B.Timestamp;
	});
	return Out;
}

int32 FSnapshotManager::RestoreSnapshot(const FSnapshotEntry& E)
{
	if (E.Timestamp.IsEmpty() || E.FolderPath.IsEmpty()) { return 0; }
	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!PFM.DirectoryExists(*E.FolderPath)) { return 0; }

	int32 Restored = 0;
	for (const FString& Original : E.Files)
	{
		if (!IsSafePath(Original)) { continue; }
		const uint32 H = GetTypeHash(Original);
		const FString DstName = FString::Printf(TEXT("%08x_%s"), H, *FPaths::GetCleanFilename(Original));
		const FString SrcSnap = E.FolderPath / DstName;
		if (!PFM.FileExists(*SrcSnap)) { continue; }

		// Best-effort: overwrite the original. UE will pick up the change on
		// next reimport / asset registry scan.
		if (PFM.CopyFile(*Original, *SrcSnap))
		{
			++Restored;
		}
	}
	UE_LOG(LogUpdateChecker, Log,
		TEXT("Snapshot '%s' restored: %d/%d files."),
		*E.Timestamp, Restored, E.Files.Num());
	return Restored;
}

bool FSnapshotManager::DeleteSnapshot(const FSnapshotEntry& E)
{
	if (E.FolderPath.IsEmpty() || !IsSafePath(E.FolderPath)) { return false; }
	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	return PFM.DeleteDirectoryRecursively(*E.FolderPath);
}

int64 FSnapshotManager::EvictOldest(int64 TargetBytes)
{
	int64 Total = 0;
	IFileManager& FM = IFileManager::Get();
	TArray<FSnapshotEntry> All = ListSnapshots();
	// Sort oldest-first for eviction.
	All.Sort([](const FSnapshotEntry& A, const FSnapshotEntry& B)
	{
		return A.Timestamp < B.Timestamp;
	});

	struct FAccum
	{
		const FSnapshotEntry* E;
		int64                 Size;
	};
	TArray<FAccum> Sized;
	for (const FSnapshotEntry& E : All)
	{
		int64 Sz = 0;
		TArray<FString> Files;
		FM.FindFiles(Files, *(E.FolderPath / TEXT("*")), true, false);
		for (const FString& F : Files) { Sz += FM.FileSize(*(E.FolderPath / F)); }
		Sized.Add({ &E, Sz });
		Total += Sz;
	}

	if (Total <= TargetBytes) { return Total; }

	for (const FAccum& A : Sized)
	{
		if (Total <= TargetBytes) { break; }
		if (DeleteSnapshot(*A.E))
		{
			Total -= A.Size;
		}
	}
	return Total;
}
