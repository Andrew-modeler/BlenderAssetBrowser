// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AssetLibrarySubsystem.h"
#include "AssetLibraryDatabase.h"
#include "BlenderAssetBrowserModule.h"

#include "Misc/Paths.h"

namespace
{
	BAB::FBoundValue B_Int(int64 X)             { return BAB::FBoundValue::MakeInt(X); }
	BAB::FBoundValue B_Text(const FString& S)   { return BAB::FBoundValue::MakeText(S); }
	BAB::FBoundValue B_Null()                   { return BAB::FBoundValue::MakeNull(); }
	BAB::FBoundValue B_Double(double X)         { return BAB::FBoundValue::MakeDouble(X); }

	FString SourceTypeToString(EAssetLibrarySource S)
	{
		switch (S)
		{
		case EAssetLibrarySource::Local:     return TEXT("local");
		case EAssetLibrarySource::Fab:       return TEXT("fab");
		case EAssetLibrarySource::Megascans: return TEXT("megascans");
		case EAssetLibrarySource::Custom:    return TEXT("custom");
		case EAssetLibrarySource::Imported:  return TEXT("imported");
		default:                             return TEXT("unknown");
		}
	}

	EAssetLibrarySource SourceTypeFromString(const FString& S)
	{
		if (S.Equals(TEXT("local"),     ESearchCase::IgnoreCase)) { return EAssetLibrarySource::Local; }
		if (S.Equals(TEXT("fab"),       ESearchCase::IgnoreCase)) { return EAssetLibrarySource::Fab; }
		if (S.Equals(TEXT("megascans"), ESearchCase::IgnoreCase)) { return EAssetLibrarySource::Megascans; }
		if (S.Equals(TEXT("custom"),    ESearchCase::IgnoreCase)) { return EAssetLibrarySource::Custom; }
		if (S.Equals(TEXT("imported"),  ESearchCase::IgnoreCase)) { return EAssetLibrarySource::Imported; }
		return EAssetLibrarySource::Unknown;
	}

	EAssetUpdateState UpdateStateFromString(const FString& S)
	{
		if (S.Equals(TEXT("up_to_date"),       ESearchCase::IgnoreCase)) { return EAssetUpdateState::UpToDate; }
		if (S.Equals(TEXT("update_available"), ESearchCase::IgnoreCase)) { return EAssetUpdateState::UpdateAvailable; }
		if (S.Equals(TEXT("source_changed"),   ESearchCase::IgnoreCase)) { return EAssetUpdateState::SourceFileChanged; }
		return EAssetUpdateState::Unknown;
	}

	FDateTime DateTimeFromIsoString(const FString& S)
	{
		FDateTime Out;
		if (S.IsEmpty() || !FDateTime::ParseIso8601(*S, Out))
		{
			Out = FDateTime::MinValue();
		}
		return Out;
	}
}

FString UAssetLibrarySubsystem::GetAllowedDatabaseRoot() const
{
	// The DB lives strictly under {ProjectSaved}/BlenderAssetBrowser/.
	// Refusing any other root is a defense against config-file path poisoning.
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) /
	       TEXT("BlenderAssetBrowser/");
}

FString UAssetLibrarySubsystem::GetDatabasePath() const
{
	return GetAllowedDatabaseRoot() / TEXT("library.db");
}

void UAssetLibrarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Database = MakeUnique<FAssetLibraryDatabase>();

	const FString DbPath = GetDatabasePath();
	const FString AllowedRoot = GetAllowedDatabaseRoot();

	if (!Database->Open(DbPath, AllowedRoot))
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("Failed to open library DB at %s"), *DbPath);
		Database.Reset();
		return;
	}

	if (!Database->RunMigrations())
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("Schema migration failed."));
		Database->Close();
		Database.Reset();
		return;
	}

	bReady = true;

	// Ensure built-in smart catalogs exist. Idempotent — INSERT OR IGNORE on
	// (smart_query) so re-running is safe.
	{
		const FString OutdatedQuery = TEXT("update:available");
		Database->Execute(
			TEXT("INSERT OR IGNORE INTO catalogs (name, parent_id, is_smart, smart_query, color, sort_order) "
			     "VALUES (?, NULL, 1, ?, ?, ?)"),
			{
				B_Text(TEXT("Outdated")),
				B_Text(OutdatedQuery),
				B_Text(TEXT("#cc4422")),
				B_Int(1000)
			});
		const FString SourceChangedQuery = TEXT("update:source_changed");
		Database->Execute(
			TEXT("INSERT OR IGNORE INTO catalogs (name, parent_id, is_smart, smart_query, color, sort_order) "
			     "VALUES (?, NULL, 1, ?, ?, ?)"),
			{
				B_Text(TEXT("Source Changed")),
				B_Text(SourceChangedQuery),
				B_Text(TEXT("#dd9911")),
				B_Int(1001)
			});
	}

	UE_LOG(LogBlenderAssetBrowser, Log, TEXT("AssetLibrarySubsystem ready (DB: %s)"), *DbPath);
}

void UAssetLibrarySubsystem::Deinitialize()
{
	bReady = false;
	if (Database.IsValid())
	{
		Database->Close();
		Database.Reset();
	}
	Super::Deinitialize();
}

// --- Catalog API ---

int64 UAssetLibrarySubsystem::AddCatalog(const FCatalogEntry& Entry)
{
	if (!bReady) { return 0; }

	FString Err;
	if (!Entry.Validate(Err))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("AddCatalog: validation failed: %s"), *Err);
		return 0;
	}

	const TArray<BAB::FBoundValue> Params =
	{
		Entry.ParentId > 0 ? B_Int(Entry.ParentId) : B_Null(),
		B_Text(Entry.Name),
		Entry.Color.IsEmpty() ? B_Null() : B_Text(Entry.Color),
		Entry.Icon.IsEmpty()  ? B_Null() : B_Text(Entry.Icon),
		B_Int(Entry.SortOrder),
		B_Int(Entry.bIsSmart ? 1 : 0),
		Entry.SmartQuery.IsEmpty() ? B_Null() : B_Text(Entry.SmartQuery)
	};

	const bool bOk = Database->Execute(
		TEXT("INSERT INTO catalogs (parent_id, name, color, icon, sort_order, is_smart, smart_query) "
		     "VALUES (?, ?, ?, ?, ?, ?, ?)"),
		Params);

	return bOk ? Database->LastInsertRowId() : 0;
}

TArray<FCatalogEntry> UAssetLibrarySubsystem::GetAllCatalogs()
{
	TArray<FCatalogEntry> Out;
	if (!bReady) { return Out; }

	Database->QueryRows(
		TEXT("SELECT id, parent_id, name, color, icon, sort_order, is_smart, smart_query FROM catalogs ORDER BY sort_order, name"),
		{},
		[&Out](const BAB::FRow& Row) -> bool
		{
			FCatalogEntry E;
			E.Id         = Row.GetInt64(0);
			E.ParentId   = Row.IsNull(1) ? 0 : Row.GetInt64(1);
			E.Name       = Row.GetText(2);
			E.Color      = Row.IsNull(3) ? FString() : Row.GetText(3);
			E.Icon       = Row.IsNull(4) ? FString() : Row.GetText(4);
			E.SortOrder  = static_cast<int32>(Row.GetInt64(5));
			E.bIsSmart   = Row.GetInt64(6) != 0;
			E.SmartQuery = Row.IsNull(7) ? FString() : Row.GetText(7);
			Out.Add(E);
			return true;
		});

	return Out;
}

bool UAssetLibrarySubsystem::UpdateCatalog(const FCatalogEntry& Entry)
{
	if (!bReady || Entry.Id <= 0) { return false; }
	FString Err;
	if (!Entry.Validate(Err)) { return false; }

	return Database->Execute(
		TEXT("UPDATE catalogs SET parent_id=?, name=?, color=?, icon=?, sort_order=?, is_smart=?, smart_query=? WHERE id=?"),
		{
			Entry.ParentId > 0 ? B_Int(Entry.ParentId) : B_Null(),
			B_Text(Entry.Name),
			Entry.Color.IsEmpty() ? B_Null() : B_Text(Entry.Color),
			Entry.Icon.IsEmpty()  ? B_Null() : B_Text(Entry.Icon),
			B_Int(Entry.SortOrder),
			B_Int(Entry.bIsSmart ? 1 : 0),
			Entry.SmartQuery.IsEmpty() ? B_Null() : B_Text(Entry.SmartQuery),
			B_Int(Entry.Id)
		});
}

bool UAssetLibrarySubsystem::DeleteCatalog(int64 CatalogId)
{
	if (!bReady || CatalogId <= 0) { return false; }
	return Database->Execute(TEXT("DELETE FROM catalogs WHERE id=?"), { B_Int(CatalogId) });
}

// --- Asset API ---

int64 UAssetLibrarySubsystem::AddAsset(const FAssetEntry& Entry)
{
	if (!bReady) { return 0; }
	FString Err;
	if (!Entry.Validate(Err))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("AddAsset: %s"), *Err);
		return 0;
	}

	const TArray<BAB::FBoundValue> Params =
	{
		B_Text(Entry.AssetPath),
		B_Text(Entry.AssetName),
		B_Text(Entry.AssetType),
		Entry.LibraryId > 0 ? B_Int(Entry.LibraryId) : B_Null(),
		B_Int(Entry.Rating),
		Entry.Notes.IsEmpty() ? B_Null() : B_Text(Entry.Notes),
		Entry.PreviewPath.IsEmpty() ? B_Null() : B_Text(Entry.PreviewPath),
		Entry.PreviewMesh.IsEmpty() ? B_Null() : B_Text(Entry.PreviewMesh),
		B_Int(Entry.TriCount), B_Int(Entry.VertCount), B_Int(Entry.LodCount),
		B_Int(Entry.TextureResMax), B_Int(Entry.bHasCollision ? 1 : 0),
		Entry.CollisionType.IsEmpty() ? B_Null() : B_Text(Entry.CollisionType),
		B_Int(Entry.MaterialCount), B_Int(Entry.DiskSizeBytes),
		Entry.EngineVersion.IsEmpty() ? B_Null() : B_Text(Entry.EngineVersion),
		B_Text(SourceTypeToString(Entry.SourceType)),
		Entry.SourcePackName.IsEmpty()? B_Null() : B_Text(Entry.SourcePackName),
		Entry.SourcePackId.IsEmpty()  ? B_Null() : B_Text(Entry.SourcePackId),
		Entry.SourceUrl.IsEmpty()     ? B_Null() : B_Text(Entry.SourceUrl),
		Entry.SourceAuthor.IsEmpty()  ? B_Null() : B_Text(Entry.SourceAuthor),
		Entry.SourceLicense.IsEmpty() ? B_Null() : B_Text(Entry.SourceLicense),
		Entry.SourceVersion.IsEmpty() ? B_Null() : B_Text(Entry.SourceVersion),
		Entry.SourceHash.IsEmpty()    ? B_Null() : B_Text(Entry.SourceHash)
	};

	const bool bOk = Database->Execute(
		TEXT("INSERT INTO assets ("
		     "  asset_path, asset_name, asset_type, library_id, rating, notes, preview_path, preview_mesh,"
		     "  tri_count, vert_count, lod_count, texture_res_max, has_collision, collision_type,"
		     "  material_count, disk_size_bytes, engine_version,"
		     "  source_type, source_pack_name, source_pack_id, source_url, source_author,"
		     "  source_license, source_version, source_hash"
		     ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
		Params);

	return bOk ? Database->LastInsertRowId() : 0;
}

TArray<FAssetEntry> UAssetLibrarySubsystem::GetAllAssets(int64 LibraryId)
{
	TArray<FAssetEntry> Out;
	if (!bReady) { return Out; }

	const FString Sql = (LibraryId > 0)
		? TEXT("SELECT id, asset_path, asset_name, asset_type, library_id, rating, notes, source_type "
		       "FROM assets WHERE library_id=? ORDER BY asset_name")
		: TEXT("SELECT id, asset_path, asset_name, asset_type, library_id, rating, notes, source_type "
		       "FROM assets ORDER BY asset_name");

	const TArray<BAB::FBoundValue> Params = (LibraryId > 0)
		? TArray<BAB::FBoundValue>{ B_Int(LibraryId) }
		: TArray<BAB::FBoundValue>{};

	Database->QueryRows(Sql, Params, [&Out](const BAB::FRow& Row) -> bool
	{
		FAssetEntry E;
		E.Id         = Row.GetInt64(0);
		E.AssetPath  = Row.GetText(1);
		E.AssetName  = Row.GetText(2);
		E.AssetType  = Row.GetText(3);
		E.LibraryId  = Row.IsNull(4) ? 0 : Row.GetInt64(4);
		E.Rating     = static_cast<int32>(Row.GetInt64(5));
		E.Notes      = Row.IsNull(6) ? FString() : Row.GetText(6);
		E.SourceType = SourceTypeFromString(Row.GetText(7));
		Out.Add(E);
		return true;
	});

	return Out;
}

bool UAssetLibrarySubsystem::UpdateAsset(const FAssetEntry& Entry)
{
	if (!bReady || Entry.Id <= 0) { return false; }
	FString Err;
	if (!Entry.Validate(Err)) { return false; }

	return Database->Execute(
		TEXT("UPDATE assets SET asset_name=?, rating=?, notes=?, modified_at=CURRENT_TIMESTAMP WHERE id=?"),
		{
			B_Text(Entry.AssetName),
			B_Int(Entry.Rating),
			Entry.Notes.IsEmpty() ? B_Null() : B_Text(Entry.Notes),
			B_Int(Entry.Id)
		});
}

bool UAssetLibrarySubsystem::DeleteAsset(int64 AssetId)
{
	if (!bReady || AssetId <= 0) { return false; }
	return Database->Execute(TEXT("DELETE FROM assets WHERE id=?"), { B_Int(AssetId) });
}

TArray<FAssetEntry> UAssetLibrarySubsystem::GetAssetsInCatalog(int64 CatalogId, int32 LimitRows)
{
	TArray<FAssetEntry> Out;
	if (!bReady) { return Out; }
	if (CatalogId <= 0) { return GetAllAssets(0); }

	const int32 Clamped = FMath::Clamp(LimitRows, 1, 10000);
	Database->QueryRows(
		TEXT("SELECT a.id, a.asset_path, a.asset_name, a.asset_type, a.library_id, a.rating, a.notes, a.source_type "
		     "FROM assets a JOIN asset_catalogs ac ON ac.asset_id = a.id "
		     "WHERE ac.catalog_id=? ORDER BY a.asset_name LIMIT ?"),
		{ B_Int(CatalogId), B_Int(Clamped) },
		[&Out](const BAB::FRow& Row) -> bool
		{
			FAssetEntry E;
			E.Id         = Row.GetInt64(0);
			E.AssetPath  = Row.GetText(1);
			E.AssetName  = Row.GetText(2);
			E.AssetType  = Row.GetText(3);
			E.LibraryId  = Row.IsNull(4) ? 0 : Row.GetInt64(4);
			E.Rating     = static_cast<int32>(Row.GetInt64(5));
			E.Notes      = Row.IsNull(6) ? FString() : Row.GetText(6);
			E.SourceType = SourceTypeFromString(Row.GetText(7));
			Out.Add(E);
			return true;
		});
	return Out;
}

FAssetEntry UAssetLibrarySubsystem::GetAssetById(int64 AssetId)
{
	FAssetEntry Out;
	if (!bReady || AssetId <= 0) { return Out; }

	Database->QueryRows(
		TEXT("SELECT id, asset_path, asset_name, asset_type, library_id, rating, notes, source_type, "
		     "       tri_count, vert_count, lod_count, texture_res_max, has_collision, collision_type, "
		     "       material_count, disk_size_bytes, engine_version, "
		     "       source_pack_name, source_pack_id, source_url, source_author, source_license, "
		     "       source_version, source_hash, imported_at, "
		     "       latest_version, update_state, changelog, preview_path "
		     "FROM assets WHERE id=?"),
		{ B_Int(AssetId) },
		[&Out](const BAB::FRow& Row) -> bool
		{
			Out.Id              = Row.GetInt64(0);
			Out.AssetPath       = Row.GetText(1);
			Out.AssetName       = Row.GetText(2);
			Out.AssetType       = Row.GetText(3);
			Out.LibraryId       = Row.IsNull(4) ? 0 : Row.GetInt64(4);
			Out.Rating          = static_cast<int32>(Row.GetInt64(5));
			Out.Notes           = Row.IsNull(6) ? FString() : Row.GetText(6);
			Out.SourceType      = SourceTypeFromString(Row.GetText(7));
			Out.TriCount        = static_cast<int32>(Row.GetInt64(8));
			Out.VertCount       = static_cast<int32>(Row.GetInt64(9));
			Out.LodCount        = static_cast<int32>(Row.GetInt64(10));
			Out.TextureResMax   = static_cast<int32>(Row.GetInt64(11));
			Out.bHasCollision   = Row.GetInt64(12) != 0;
			Out.CollisionType   = Row.IsNull(13) ? FString() : Row.GetText(13);
			Out.MaterialCount   = static_cast<int32>(Row.GetInt64(14));
			Out.DiskSizeBytes   = Row.GetInt64(15);
			Out.EngineVersion   = Row.IsNull(16) ? FString() : Row.GetText(16);
			Out.SourcePackName  = Row.IsNull(17) ? FString() : Row.GetText(17);
			Out.SourcePackId    = Row.IsNull(18) ? FString() : Row.GetText(18);
			Out.SourceUrl       = Row.IsNull(19) ? FString() : Row.GetText(19);
			Out.SourceAuthor    = Row.IsNull(20) ? FString() : Row.GetText(20);
			Out.SourceLicense   = Row.IsNull(21) ? FString() : Row.GetText(21);
			Out.SourceVersion   = Row.IsNull(22) ? FString() : Row.GetText(22);
			Out.SourceHash      = Row.IsNull(23) ? FString() : Row.GetText(23);
			Out.ImportedAt      = Row.IsNull(24) ? FDateTime::MinValue() : DateTimeFromIsoString(Row.GetText(24));
			Out.LatestVersion   = Row.IsNull(25) ? FString() : Row.GetText(25);
			Out.UpdateState     = Row.IsNull(26) ? EAssetUpdateState::Unknown : UpdateStateFromString(Row.GetText(26));
			Out.Changelog       = Row.IsNull(27) ? FString() : Row.GetText(27);
			Out.PreviewPath     = Row.IsNull(28) ? FString() : Row.GetText(28);
			return false; // single row
		});
	return Out;
}

TArray<FTagEntry> UAssetLibrarySubsystem::GetAssetTags(int64 AssetId)
{
	TArray<FTagEntry> Out;
	if (!bReady || AssetId <= 0) { return Out; }

	Database->QueryRows(
		TEXT("SELECT t.id, t.name, t.color, t.parent "
		     "FROM tags t JOIN asset_tags at ON at.tag_id = t.id "
		     "WHERE at.asset_id=? ORDER BY t.name"),
		{ B_Int(AssetId) },
		[&Out](const BAB::FRow& Row) -> bool
		{
			FTagEntry T;
			T.Id     = Row.GetInt64(0);
			T.Name   = Row.GetText(1);
			T.Color  = Row.IsNull(2) ? FString() : Row.GetText(2);
			T.Parent = Row.IsNull(3) ? FString() : Row.GetText(3);
			Out.Add(T);
			return true;
		});
	return Out;
}

// --- Tag API ---

int64 UAssetLibrarySubsystem::AddTag(const FTagEntry& Entry)
{
	if (!bReady) { return 0; }
	FString Err;
	if (!Entry.Validate(Err)) { return 0; }

	const bool bOk = Database->Execute(
		TEXT("INSERT OR IGNORE INTO tags (name, color, parent) VALUES (?, ?, ?)"),
		{
			B_Text(Entry.Name),
			Entry.Color.IsEmpty()  ? B_Null() : B_Text(Entry.Color),
			Entry.Parent.IsEmpty() ? B_Null() : B_Text(Entry.Parent)
		});

	if (!bOk) { return 0; }

	int64 ResultId = Database->LastInsertRowId();
	if (ResultId == 0)
	{
		// Already existed — find by name.
		Database->QueryRows(TEXT("SELECT id FROM tags WHERE name=?"), { B_Text(Entry.Name) },
			[&ResultId](const BAB::FRow& Row) -> bool
			{
				ResultId = Row.GetInt64(0);
				return false;
			});
	}
	return ResultId;
}

TArray<FTagEntry> UAssetLibrarySubsystem::GetAllTags()
{
	TArray<FTagEntry> Out;
	if (!bReady) { return Out; }

	Database->QueryRows(
		TEXT("SELECT id, name, color, parent, count FROM tags ORDER BY name"),
		{},
		[&Out](const BAB::FRow& Row) -> bool
		{
			FTagEntry E;
			E.Id     = Row.GetInt64(0);
			E.Name   = Row.GetText(1);
			E.Color  = Row.IsNull(2) ? FString() : Row.GetText(2);
			E.Parent = Row.IsNull(3) ? FString() : Row.GetText(3);
			E.Count  = static_cast<int32>(Row.GetInt64(4));
			Out.Add(E);
			return true;
		});
	return Out;
}

bool UAssetLibrarySubsystem::DeleteTag(int64 TagId)
{
	if (!bReady || TagId <= 0) { return false; }
	return Database->Execute(TEXT("DELETE FROM tags WHERE id=?"), { B_Int(TagId) });
}

bool UAssetLibrarySubsystem::AssignTag(int64 AssetId, int64 TagId, const FString& Source, float Confidence)
{
	if (!bReady || AssetId <= 0 || TagId <= 0) { return false; }
	if (Source != TEXT("manual") && Source != TEXT("ai")) { return false; }
	const float ClampedConf = FMath::Clamp(Confidence, 0.0f, 1.0f);

	return Database->Execute(
		TEXT("INSERT OR REPLACE INTO asset_tags (asset_id, tag_id, source, confidence) VALUES (?, ?, ?, ?)"),
		{ B_Int(AssetId), B_Int(TagId), B_Text(Source), B_Double(ClampedConf) });
}

bool UAssetLibrarySubsystem::RemoveTag(int64 AssetId, int64 TagId)
{
	if (!bReady || AssetId <= 0 || TagId <= 0) { return false; }
	return Database->Execute(
		TEXT("DELETE FROM asset_tags WHERE asset_id=? AND tag_id=?"),
		{ B_Int(AssetId), B_Int(TagId) });
}

// --- Library API ---

int64 UAssetLibrarySubsystem::AddLibrary(const FLibraryEntry& Entry)
{
	if (!bReady) { return 0; }
	FString Err;
	if (!Entry.Validate(Err)) { return 0; }

	const bool bOk = Database->Execute(
		TEXT("INSERT INTO libraries (name, path, type, priority, is_visible) VALUES (?, ?, ?, ?, ?)"),
		{
			B_Text(Entry.Name),
			B_Text(Entry.Path),
			B_Text(Entry.Type),
			B_Int(Entry.Priority),
			B_Int(Entry.bIsVisible ? 1 : 0)
		});
	return bOk ? Database->LastInsertRowId() : 0;
}

TArray<FLibraryEntry> UAssetLibrarySubsystem::GetAllLibraries()
{
	TArray<FLibraryEntry> Out;
	if (!bReady) { return Out; }
	Database->QueryRows(
		TEXT("SELECT id, name, path, type, priority, is_visible FROM libraries ORDER BY priority DESC, name"),
		{},
		[&Out](const BAB::FRow& Row) -> bool
		{
			FLibraryEntry E;
			E.Id         = Row.GetInt64(0);
			E.Name       = Row.GetText(1);
			E.Path       = Row.GetText(2);
			E.Type       = Row.GetText(3);
			E.Priority   = static_cast<int32>(Row.GetInt64(4));
			E.bIsVisible = Row.GetInt64(5) != 0;
			Out.Add(E);
			return true;
		});
	return Out;
}

bool UAssetLibrarySubsystem::DeleteLibrary(int64 LibraryId)
{
	if (!bReady || LibraryId <= 0) { return false; }
	return Database->Execute(TEXT("DELETE FROM libraries WHERE id=?"), { B_Int(LibraryId) });
}

// --- Favorites / Recents API ---

bool UAssetLibrarySubsystem::SetPinned(int64 AssetId, bool bPinned)
{
	if (!bReady || AssetId <= 0) { return false; }

	return Database->Execute(
		TEXT("INSERT INTO favorites (asset_id, pinned, used_at) VALUES (?, ?, CURRENT_TIMESTAMP) "
		     "ON CONFLICT(asset_id) DO UPDATE SET pinned=excluded.pinned"),
		{ B_Int(AssetId), B_Int(bPinned ? 1 : 0) });
}

bool UAssetLibrarySubsystem::TouchRecent(int64 AssetId)
{
	if (!bReady || AssetId <= 0) { return false; }
	return Database->Execute(
		TEXT("INSERT INTO favorites (asset_id, pinned, used_at) VALUES (?, 0, CURRENT_TIMESTAMP) "
		     "ON CONFLICT(asset_id) DO UPDATE SET used_at=CURRENT_TIMESTAMP"),
		{ B_Int(AssetId) });
}

TArray<int64> UAssetLibrarySubsystem::GetRecentAssetIds(int32 Limit)
{
	TArray<int64> Out;
	if (!bReady) { return Out; }
	const int32 Clamped = FMath::Clamp(Limit, 1, 5000);

	Database->QueryRows(
		TEXT("SELECT asset_id FROM favorites ORDER BY used_at DESC LIMIT ?"),
		{ B_Int(Clamped) },
		[&Out](const BAB::FRow& Row) -> bool
		{
			Out.Add(Row.GetInt64(0));
			return true;
		});
	return Out;
}

TArray<int64> UAssetLibrarySubsystem::GetPinnedAssetIds()
{
	TArray<int64> Out;
	if (!bReady) { return Out; }
	Database->QueryRows(
		TEXT("SELECT asset_id FROM favorites WHERE pinned=1 ORDER BY used_at DESC"),
		{},
		[&Out](const BAB::FRow& Row) -> bool
		{
			Out.Add(Row.GetInt64(0));
			return true;
		});
	return Out;
}
