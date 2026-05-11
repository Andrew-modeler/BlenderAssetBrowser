// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Higher-level operations on the catalog tree: reparenting with cycle detection,
// asset assignment, and smart-catalog management.
//
// SECURITY: cycle detection is mandatory — without it, a malicious .assetlib
// could ship a self-cycling catalog (A->B->A) and crash the editor with
// infinite recursion when traversing the tree.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "AssetLibraryTypes.h"

class UAssetLibrarySubsystem;

class BLENDERASSETBROWSER_API FCatalogManager
{
public:
	explicit FCatalogManager(UAssetLibrarySubsystem* InSubsystem);

	/** Reparent CatalogId under NewParentId. Returns false if it would create a cycle.
	 *  NewParentId may be 0 (root). */
	bool Reparent(int64 CatalogId, int64 NewParentId);

	/** Assign asset to catalog. No-op if already linked. */
	bool AssignAssetToCatalog(int64 AssetId, int64 CatalogId);

	/** Remove asset from catalog. */
	bool RemoveAssetFromCatalog(int64 AssetId, int64 CatalogId);

	/** All catalog ids reachable downward from RootId (including RootId).
	 *  Bounded by depth limit to prevent stack abuse. */
	TArray<int64> GetDescendants(int64 RootId, int32 MaxDepth = 32);

	/** Returns true if a child relationship from `Ancestor` to `Descendant` would form
	 *  a cycle, i.e. `Descendant` is already an ancestor of `Ancestor`. */
	bool WouldCreateCycle(int64 Ancestor, int64 Descendant);

private:
	// SECURITY: TWeakObjectPtr makes a GC-collected subsystem visible as IsValid()==false,
	// instead of a dangling raw pointer. The subsystem lives for the editor session, but
	// during editor shutdown / hot-reload it can vanish from under us.
	TWeakObjectPtr<UAssetLibrarySubsystem> Subsystem;
};
