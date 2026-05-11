// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "CatalogManager.h"
#include "AssetLibrarySubsystem.h"
#include "AssetLibraryDatabase.h"
#include "BlenderAssetBrowserModule.h"

namespace
{
	BAB::FBoundValue B_Int(int64 X) { return BAB::FBoundValue::MakeInt(X); }
}

FCatalogManager::FCatalogManager(UAssetLibrarySubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
}

bool FCatalogManager::WouldCreateCycle(int64 Ancestor, int64 Descendant)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady()) { return false; }
	if (Ancestor <= 0 || Descendant <= 0) { return false; }
	if (Ancestor == Descendant) { return true; }

	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return false; }

	// Walk upward from `Ancestor` via parent_id links. If we hit `Descendant`,
	// putting `Ancestor` under `Descendant` would close the loop.
	// Bounded iteration: at most 64 steps — far beyond any realistic catalog depth.
	int64 Current = Ancestor;
	for (int32 Step = 0; Step < 64; ++Step)
	{
		int64 Parent = 0;
		bool bFound = false;
		Db->QueryRows(
			TEXT("SELECT parent_id FROM catalogs WHERE id=?"),
			{ B_Int(Current) },
			[&Parent, &bFound](const BAB::FRow& Row) -> bool
			{
				bFound = true;
				Parent = Row.IsNull(0) ? 0 : Row.GetInt64(0);
				return false;
			});
		if (!bFound || Parent == 0) { return false; }
		if (Parent == Descendant) { return true; }
		Current = Parent;
	}
	// We ran out of steps. Treat as suspect — likely an existing cycle in DB.
	UE_LOG(LogBlenderAssetBrowser, Warning,
		TEXT("WouldCreateCycle: traversal exceeded depth limit. DB may already contain a cycle."));
	return true;
}

bool FCatalogManager::Reparent(int64 CatalogId, int64 NewParentId)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady()) { return false; }
	if (CatalogId <= 0) { return false; }
	if (CatalogId == NewParentId) { return false; }

	if (NewParentId > 0 && WouldCreateCycle(NewParentId, CatalogId))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning,
			TEXT("Reparent rejected: would create cycle (cat=%lld parent=%lld)"),
			CatalogId, NewParentId);
		return false;
	}

	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return false; }

	return Db->Execute(
		TEXT("UPDATE catalogs SET parent_id=? WHERE id=?"),
		{
			NewParentId > 0 ? B_Int(NewParentId) : BAB::FBoundValue::MakeNull(),
			B_Int(CatalogId)
		});
}

bool FCatalogManager::AssignAssetToCatalog(int64 AssetId, int64 CatalogId)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady()) { return false; }
	if (AssetId <= 0 || CatalogId <= 0) { return false; }

	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return false; }

	return Db->Execute(
		TEXT("INSERT OR IGNORE INTO asset_catalogs (asset_id, catalog_id) VALUES (?, ?)"),
		{ B_Int(AssetId), B_Int(CatalogId) });
}

bool FCatalogManager::RemoveAssetFromCatalog(int64 AssetId, int64 CatalogId)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady()) { return false; }
	if (AssetId <= 0 || CatalogId <= 0) { return false; }

	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return false; }

	return Db->Execute(
		TEXT("DELETE FROM asset_catalogs WHERE asset_id=? AND catalog_id=?"),
		{ B_Int(AssetId), B_Int(CatalogId) });
}

TArray<int64> FCatalogManager::GetDescendants(int64 RootId, int32 MaxDepth)
{
	TArray<int64> Out;
	if (!Subsystem.IsValid() || !Subsystem->IsReady() || RootId <= 0) { return Out; }
	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return Out; }

	// One recursive CTE replaces the per-level N+1 of the previous BFS. UNION
	// (not UNION ALL) deduplicates and short-circuits if the catalog graph
	// somehow contains a cycle. Depth is capped both inside the CTE and as a
	// hard LIMIT so a corrupted DB cannot make us load the world.
	const int32 ClampedDepth = FMath::Clamp(MaxDepth, 1, 64);
	const int32 HardRowCap   = 100000;

	Db->QueryRows(
		TEXT("WITH RECURSIVE subtree(id, depth) AS ("
		     "  SELECT id, 0 FROM catalogs WHERE id=?"
		     "  UNION"
		     "  SELECT c.id, s.depth + 1"
		     "  FROM catalogs c"
		     "  INNER JOIN subtree s ON c.parent_id = s.id"
		     "  WHERE s.depth < ?"
		     ") SELECT id FROM subtree LIMIT ?"),
		{ B_Int(RootId), B_Int(ClampedDepth), B_Int(HardRowCap) },
		[&Out](const BAB::FRow& Row) -> bool
		{
			Out.Add(Row.GetInt64(0));
			return true;
		});

	return Out;
}
