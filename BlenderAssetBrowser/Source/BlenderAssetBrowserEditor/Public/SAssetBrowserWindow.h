// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Main asset-browser window. Layout: toolbar | catalog tree | asset grid | inspector.
// Phase-1.8 implementation: real widgets backed by SQLite data, but no thumbnails yet
// (those arrive when SAssetCard wires up FPreviewCacheManager in a UI-polish pass).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "AssetLibraryTypes.h"

class STextBlock;
class SEditableTextBox;
class SCatalogTreeRow;
class UAssetLibrarySubsystem;
class FAssetThumbnailPool;

class SAssetBrowserWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetBrowserWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Slate drag/drop overrides for OS-Explorer file drops.
	virtual FReply OnDragOver(const FGeometry& Geo, const FDragDropEvent& Event) override;
	virtual FReply OnDrop(const FGeometry& Geo, const FDragDropEvent& Event) override;
	virtual void OnDragLeave(const FDragDropEvent& Event) override;

private:
	UAssetLibrarySubsystem* GetSubsystem() const;
	void Refresh();
	void OnSearchTextChanged(const FText& Text);
	void OnSearchTextCommitted(const FText& Text, ETextCommit::Type CommitType);
	FReply OnAddRootCatalogClicked();
	FReply OnDeleteSelectedCatalogClicked();

	// --- Data refreshers ---
	TSharedRef<class ITableRow> MakeCatalogRow(TSharedPtr<FCatalogEntry> Item,
		const TSharedRef<class STableViewBase>& Owner);
	void GetCatalogChildren(TSharedPtr<FCatalogEntry> Item, TArray<TSharedPtr<FCatalogEntry>>& OutChildren);
	void OnCatalogSelectionChanged(TSharedPtr<FCatalogEntry> Item, ESelectInfo::Type Info);

	TSharedRef<class ITableRow> MakeAssetRow(TSharedPtr<FAssetEntry> Item,
		const TSharedRef<class STableViewBase>& Owner);
	void OnAssetSelectionChanged(TSharedPtr<FAssetEntry> Item, ESelectInfo::Type Info);

	// --- State ---
	TSharedPtr<class STreeView<TSharedPtr<FCatalogEntry>>> CatalogTree;
	TSharedPtr<class SListView<TSharedPtr<FAssetEntry>>>   AssetList;
	TSharedPtr<STextBlock>                                 InspectorText;
	TSharedPtr<SEditableTextBox>                           SearchBox;

	TArray<TSharedPtr<FCatalogEntry>>                       RootCatalogs;
	TMap<int64, TArray<TSharedPtr<FCatalogEntry>>>          CatalogChildren;
	TArray<TSharedPtr<FAssetEntry>>                         AssetItems;

	int64    SelectedCatalogId = 0;
	FString  CurrentSearchText;

	// Grid-view state. When bGridMode is on, the asset list is replaced by a
	// wrap-box of thumbnail cards.
	bool                              bGridMode = false;
	TSharedPtr<class SBox>            ContentArea;
	TSharedPtr<class SWrapBox>        AssetGrid;

	/** Shared thumbnail pool for UE's own thumbnail renderer. Lifetime ties
	 *  to the window; the pool internally hands out and reuses GPU resources. */
	TSharedPtr<FAssetThumbnailPool>   ThumbnailPool;

	void RebuildContentArea();
	FReply OnToggleGridClicked();
};
