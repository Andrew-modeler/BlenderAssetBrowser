// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Actor collections — pack a selection of level actors into a single "Asset
// Browser collection" that can be dropped into another scene as a group.
// Storage uses the existing `collections` + `collection_items` tables.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "AssetLibraryTypes.h"

class AActor;
class UAssetLibrarySubsystem;
class UWorld;

class BLENDERASSETBROWSER_API FCollectionManager
{
public:
	explicit FCollectionManager(UAssetLibrarySubsystem* InSubsystem);

	/**
	 * Create a collection from the given live actors. Stores per-actor relative
	 * transforms (vs. group pivot) and asset references. Returns the new
	 * collection id, 0 on failure.
	 */
	int64 CreateFromActors(const FString& CollectionName, const TArray<AActor*>& Actors);

	/**
	 * Spawn all items of a collection into `World` at `BaseLocation`.
	 * `bAsBlueprintContainer` — true = single AActor with ChildActorComponents.
	 * Returns the number of actors spawned.
	 */
	int32 SpawnIntoWorld(int64 CollectionId, UWorld* World, const FVector& BaseLocation,
	                     bool bAsBlueprintContainer);

	/** Delete a collection (cascades collection_items). */
	bool DeleteCollection(int64 CollectionId);

private:
	TWeakObjectPtr<UAssetLibrarySubsystem> Subsystem;
};
