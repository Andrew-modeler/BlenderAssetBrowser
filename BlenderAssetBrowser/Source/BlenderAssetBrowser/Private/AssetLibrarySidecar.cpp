// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AssetLibrarySidecar.h"
#include "AssetLibrarySubsystem.h"
#include "AssetLibraryDatabase.h"
#include "AssetLibraryTypes.h"
#include "AssetLibrarySettings.h"
#include "BlenderAssetBrowserModule.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformFileManager.h"

namespace
{
	BAB::FBoundValue B_Int(int64 X) { return BAB::FBoundValue::MakeInt(X); }
	BAB::FBoundValue B_Text(const FString& S) { return BAB::FBoundValue::MakeText(S); }
	BAB::FBoundValue B_Null() { return BAB::FBoundValue::MakeNull(); }

	/** Builds a stable, line-oriented pretty-printed JSON.
	 *  We use UE's JsonWriter with Pretty formatting which already emits
	 *  one field per line — that's good enough for clean diffs. */
	bool WriteObject(const TSharedRef<FJsonObject>& Obj, const FString& Path)
	{
		FString Out;
		auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		if (!FJsonSerializer::Serialize(Obj, Writer)) { return false; }
		return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool ReadObject(const FString& Path, TSharedPtr<FJsonObject>& OutObj)
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path)) { return false; }
		if (Contents.Len() > BAB::MAX_FILE_BYTES_JSON) { return false; }
		auto Reader = TJsonReaderFactory<>::Create(Contents);
		return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
	}
}

bool FAssetLibrarySidecar::ExportToFile(UAssetLibrarySubsystem* Sub, const FString& OutPath)
{
	if (!Sub || !Sub->IsReady()) { return false; }
	if (OutPath.IsEmpty() || OutPath.Contains(TEXT("..")) ||
	    !OutPath.EndsWith(TEXT(".assetlib"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Sidecar export: unsafe path %s"), *OutPath);
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);

	// Catalogs
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FCatalogEntry& C : Sub->GetAllCatalogs())
		{
			auto O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("id"), C.Id);
			O->SetNumberField(TEXT("parent_id"), C.ParentId);
			O->SetStringField(TEXT("name"), C.Name);
			if (!C.Color.IsEmpty()) { O->SetStringField(TEXT("color"), C.Color); }
			if (!C.Icon.IsEmpty()) { O->SetStringField(TEXT("icon"), C.Icon); }
			O->SetNumberField(TEXT("sort_order"), C.SortOrder);
			O->SetBoolField(TEXT("is_smart"), C.bIsSmart);
			if (!C.SmartQuery.IsEmpty()) { O->SetStringField(TEXT("smart_query"), C.SmartQuery); }
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		Root->SetArrayField(TEXT("catalogs"), Arr);
	}

	// Tags
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FTagEntry& T : Sub->GetAllTags())
		{
			auto O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("id"), T.Id);
			O->SetStringField(TEXT("name"), T.Name);
			if (!T.Color.IsEmpty())  { O->SetStringField(TEXT("color"), T.Color); }
			if (!T.Parent.IsEmpty()) { O->SetStringField(TEXT("parent"), T.Parent); }
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		Root->SetArrayField(TEXT("tags"), Arr);
	}

	// Libraries
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FLibraryEntry& L : Sub->GetAllLibraries())
		{
			auto O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("id"), L.Id);
			O->SetStringField(TEXT("name"), L.Name);
			O->SetStringField(TEXT("path"), L.Path);
			O->SetStringField(TEXT("type"), L.Type);
			O->SetNumberField(TEXT("priority"), L.Priority);
			O->SetBoolField(TEXT("visible"), L.bIsVisible);
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		Root->SetArrayField(TEXT("libraries"), Arr);
	}

	// Assets — minimal export (the catalog/tag links are restored later).
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FAssetEntry& A : Sub->GetAllAssets(0))
		{
			auto O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("id"), A.Id);
			O->SetStringField(TEXT("asset_path"), A.AssetPath);
			O->SetStringField(TEXT("asset_name"), A.AssetName);
			O->SetStringField(TEXT("asset_type"), A.AssetType);
			if (A.Rating > 0)        { O->SetNumberField(TEXT("rating"), A.Rating); }
			if (!A.Notes.IsEmpty())  { O->SetStringField(TEXT("notes"), A.Notes); }
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		Root->SetArrayField(TEXT("assets"), Arr);
	}

	// Bug #9 fix: export relation tables so tags / catalog membership /
	// embeddings survive a round-trip. Embeddings are large (~3 KB / row);
	// only included when explicitly listed because they're regenerable.
	FAssetLibraryDatabase* Db = Sub->GetDatabase();
	if (Db)
	{
		// asset_catalogs
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			Db->QueryRows(TEXT("SELECT asset_id, catalog_id FROM asset_catalogs"), {},
				[&Arr](const BAB::FRow& R) -> bool
				{
					auto O = MakeShared<FJsonObject>();
					O->SetNumberField(TEXT("asset_id"),   R.GetInt64(0));
					O->SetNumberField(TEXT("catalog_id"), R.GetInt64(1));
					Arr.Add(MakeShared<FJsonValueObject>(O));
					return true;
				});
			Root->SetArrayField(TEXT("asset_catalogs"), Arr);
		}

		// asset_tags
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			Db->QueryRows(
				TEXT("SELECT asset_id, tag_id, source, confidence FROM asset_tags"), {},
				[&Arr](const BAB::FRow& R) -> bool
				{
					auto O = MakeShared<FJsonObject>();
					O->SetNumberField(TEXT("asset_id"),   R.GetInt64(0));
					O->SetNumberField(TEXT("tag_id"),     R.GetInt64(1));
					O->SetStringField(TEXT("source"),     R.IsNull(2) ? FString(TEXT("manual")) : R.GetText(2));
					O->SetNumberField(TEXT("confidence"), R.IsNull(3) ? 1.0 : R.GetDouble(3));
					Arr.Add(MakeShared<FJsonValueObject>(O));
					return true;
				});
			Root->SetArrayField(TEXT("asset_tags"), Arr);
		}

		// asset_embeddings — opt-in via Settings. ~3 KB/row × N assets adds
		// up; default OFF keeps sidecars small. Re-tag with AI to rebuild.
		const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>();
		if (S && S->bExportEmbeddingsInSidecar)
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			Db->QueryRows(
				TEXT("SELECT asset_id, model_id, vector_dim FROM asset_embeddings"),
				{},
				[&Arr](const BAB::FRow& R) -> bool
				{
					// Embedding payload itself stays in the DB — we record
					// presence so a downstream tool knows the asset has one.
					auto O = MakeShared<FJsonObject>();
					O->SetNumberField(TEXT("asset_id"),   R.GetInt64(0));
					O->SetStringField(TEXT("model_id"),   R.GetText(1));
					O->SetNumberField(TEXT("vector_dim"), R.GetInt64(2));
					Arr.Add(MakeShared<FJsonValueObject>(O));
					return true;
				});
			Root->SetArrayField(TEXT("asset_embeddings_index"), Arr);
		}
	}

	// Ensure parent directory.
	const FString Dir = FPaths::GetPath(OutPath);
	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!Dir.IsEmpty() && !PFM.DirectoryExists(*Dir)) { PFM.CreateDirectoryTree(*Dir); }

	const bool bOk = WriteObject(Root, OutPath);
	UE_LOG(LogBlenderAssetBrowser, Log, TEXT("Sidecar export %s -> %s"),
		bOk ? TEXT("OK") : TEXT("FAIL"), *OutPath);
	return bOk;
}

bool FAssetLibrarySidecar::ImportFromFile(UAssetLibrarySubsystem* Sub, const FString& InPath)
{
	if (!Sub || !Sub->IsReady()) { return false; }
	TSharedPtr<FJsonObject> Root;
	if (!ReadObject(InPath, Root))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Sidecar import failed to read: %s"), *InPath);
		return false;
	}

	int32 SchemaVersion = 0;
	Root->TryGetNumberField(TEXT("schema_version"), SchemaVersion);
	if (SchemaVersion != 1)
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Unsupported sidecar schema %d"), SchemaVersion);
		return false;
	}

	int32 Imported = 0, Rejected = 0;

	// Catalogs
	const TArray<TSharedPtr<FJsonValue>>* Cats = nullptr;
	if (Root->TryGetArrayField(TEXT("catalogs"), Cats) && Cats)
	{
		for (const auto& V : *Cats)
		{
			const TSharedPtr<FJsonObject>& O = V->AsObject();
			if (!O.IsValid()) { ++Rejected; continue; }
			FCatalogEntry C;
			O->TryGetStringField(TEXT("name"), C.Name);
			O->TryGetStringField(TEXT("color"), C.Color);
			O->TryGetStringField(TEXT("icon"), C.Icon);
			O->TryGetStringField(TEXT("smart_query"), C.SmartQuery);
			O->TryGetBoolField(TEXT("is_smart"), C.bIsSmart);
			int32 SortOrder = 0; O->TryGetNumberField(TEXT("sort_order"), SortOrder); C.SortOrder = SortOrder;
			int64 ParentId = 0;  O->TryGetNumberField(TEXT("parent_id"), ParentId);   C.ParentId = ParentId;

			FString Err;
			if (!C.Validate(Err)) { ++Rejected; continue; }
			if (Sub->AddCatalog(C) > 0) { ++Imported; } else { ++Rejected; }
		}
	}

	// Tags
	const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
	if (Root->TryGetArrayField(TEXT("tags"), Tags) && Tags)
	{
		for (const auto& V : *Tags)
		{
			const TSharedPtr<FJsonObject>& O = V->AsObject();
			if (!O.IsValid()) { ++Rejected; continue; }
			FTagEntry T;
			O->TryGetStringField(TEXT("name"), T.Name);
			O->TryGetStringField(TEXT("color"), T.Color);
			O->TryGetStringField(TEXT("parent"), T.Parent);
			FString Err;
			if (!T.Validate(Err)) { ++Rejected; continue; }
			if (Sub->AddTag(T) > 0) { ++Imported; } else { ++Rejected; }
		}
	}

	UE_LOG(LogBlenderAssetBrowser, Log, TEXT("Sidecar import: imported=%d rejected=%d"),
		Imported, Rejected);
	return true;
}
