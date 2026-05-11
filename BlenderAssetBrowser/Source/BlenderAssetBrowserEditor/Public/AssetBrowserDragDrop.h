// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Drag & drop payload + handlers for the asset browser.
//
// Material-on-mesh viewport drop: hit-test viewport pixel → resolve actor + mesh
// section → replace single material slot. NO global mesh material swap.
//
// SECURITY: source asset paths sanity-checked (length, type) before they touch
// any UObject ops. Refuses to operate when target is Editor-only or in a sublevel
// the user doesn't have write access to.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "AssetRegistry/AssetData.h"

class FAssetBrowserDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FAssetBrowserDragDropOp, FDragDropOperation)

	static TSharedRef<FAssetBrowserDragDropOp> New(const TArray<FAssetData>& InAssets);

	const TArray<FAssetData>& GetAssets() const { return Assets; }

	/** Drop on a viewport at the given world hit. Routes to slot-replacement for
	 *  materials and actor-spawn for meshes/blueprints. Returns true if any
	 *  action was taken. */
	bool ExecuteViewportDrop(class UWorld* World, const FVector& HitLocation,
	                         class AActor* HitActor, int32 HitSectionIndex) const;

private:
	TArray<FAssetData> Assets;
};
