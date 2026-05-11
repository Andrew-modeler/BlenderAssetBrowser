// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Editor subsystem owning the singleton DB connection and the public API
// surface for catalogs, tags, assets, libraries, etc.
//
// SECURITY: every public method validates input. The subsystem is the
// outermost trust boundary between user actions and the DB layer.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Templates/UniquePtr.h"

#include "AssetLibraryTypes.h"
#include "AssetLibraryDatabase.h"

#include "AssetLibrarySubsystem.generated.h"

UCLASS()
class BLENDERASSETBROWSER_API UAssetLibrarySubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Returns the absolute path to the project-local DB file. */
	FString GetDatabasePath() const;

	/** Returns the trusted root inside which the DB MUST live. */
	FString GetAllowedDatabaseRoot() const;

	/** True if DB is open and migrations have run. */
	bool IsReady() const { return bReady; }

	// --- Catalog API ---

	/** Insert a new catalog. Returns rowid > 0 on success, 0 on failure. */
	int64 AddCatalog(const FCatalogEntry& Entry);

	/** Returns all catalogs (no smart-resolution; use SearchEngine for that). */
	TArray<FCatalogEntry> GetAllCatalogs();

	/** Update a catalog (must already exist). Returns true on success. */
	bool UpdateCatalog(const FCatalogEntry& Entry);

	/** Delete catalog by id. Cascade-deletes asset_catalogs links. */
	bool DeleteCatalog(int64 CatalogId);

	// --- Asset API ---

	int64 AddAsset(const FAssetEntry& Entry);
	TArray<FAssetEntry> GetAllAssets(int64 LibraryId = 0);
	bool UpdateAsset(const FAssetEntry& Entry);
	bool DeleteAsset(int64 AssetId);

	/** Get assets linked to the given catalog. Returns ALL assets when CatalogId == 0. */
	TArray<FAssetEntry> GetAssetsInCatalog(int64 CatalogId, int32 LimitRows = 1000);

	/** Get the asset row by id. Returns FAssetEntry with Id==0 if missing. */
	FAssetEntry GetAssetById(int64 AssetId);

	/** Returns tags assigned to an asset (manual + AI), sorted alphabetically. */
	TArray<FTagEntry> GetAssetTags(int64 AssetId);

	// --- Tag API ---

	int64 AddTag(const FTagEntry& Entry);
	TArray<FTagEntry> GetAllTags();
	bool DeleteTag(int64 TagId);

	bool AssignTag(int64 AssetId, int64 TagId, const FString& Source = TEXT("manual"), float Confidence = 1.0f);
	bool RemoveTag(int64 AssetId, int64 TagId);

	// --- Library API ---

	int64 AddLibrary(const FLibraryEntry& Entry);
	TArray<FLibraryEntry> GetAllLibraries();
	bool DeleteLibrary(int64 LibraryId);

	// --- Favorites / Recents API ---

	/** Pin or unpin an asset. Pinned items appear in the "Pinned" section. */
	bool SetPinned(int64 AssetId, bool bPinned);

	/** Mark asset as used now (updates used_at timestamp). Idempotent insert. */
	bool TouchRecent(int64 AssetId);

	/** Returns up to `Limit` most-recently-used asset ids. */
	TArray<int64> GetRecentAssetIds(int32 Limit = 100);

	/** Returns all currently-pinned asset ids. */
	TArray<int64> GetPinnedAssetIds();

	/** Direct DB access for advanced callers (CatalogManager, SearchEngine). */
	FAssetLibraryDatabase* GetDatabase() const { return Database.Get(); }

private:
	TUniquePtr<FAssetLibraryDatabase> Database;
	bool bReady = false;
};
