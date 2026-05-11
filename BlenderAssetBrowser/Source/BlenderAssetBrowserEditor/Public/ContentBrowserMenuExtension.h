// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Adds "Mark as Asset" / "Unmark" entries to the Content Browser context menu.
//
// SECURITY: every selected asset is reduced to its (path, type) tuple before
// it ever touches the DB. We never store the live UObject, so a malicious
// in-memory state can't flow through here.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class FContentBrowserMenuExtension
{
public:
	void Register();
	void Unregister();

private:
	void ExtendContentBrowserMenu();
	bool IsAssetSupported(const FAssetData& Asset) const;

	void ExecuteMarkAsAsset(TArray<FAssetData> SelectedAssets);
	void ExecuteUnmark(TArray<FAssetData> SelectedAssets);
	void ExecuteAutoTag(TArray<FAssetData> SelectedAssets);
	void ExecuteBulkSetRating(TArray<FAssetData> SelectedAssets, int32 Rating);
	void ExecuteBulkAddTag(TArray<FAssetData> SelectedAssets);

	FDelegateHandle MenuExtensionDelegateHandle;
};
