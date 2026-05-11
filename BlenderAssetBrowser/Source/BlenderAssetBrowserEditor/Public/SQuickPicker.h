// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Spotlight-style asset picker. Ctrl+Shift+Space opens an overlay; user types
// to fuzzy-filter the library; Enter drops the selected asset into the
// viewport at cursor.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "AssetLibraryTypes.h"

class SEditableTextBox;
class SWindow;

class SQuickPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuickPicker) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Show as a borderless modal overlay centered on the screen. */
	static void ShowOverlay();

private:
	void Refresh();
	void OnSearchTextChanged(const FText& Text);
	FReply OnKeyDownAtRoot(const FGeometry& Geo, const FKeyEvent& Key);
	TSharedRef<class ITableRow> MakeRow(TSharedPtr<FAssetEntry> Item,
		const TSharedRef<class STableViewBase>& Owner);
	void OnSelectionChanged(TSharedPtr<FAssetEntry> Item, ESelectInfo::Type Info);
	void Spawn(TSharedPtr<FAssetEntry> Item);

	TSharedPtr<SEditableTextBox> SearchBox;
	TSharedPtr<class SListView<TSharedPtr<FAssetEntry>>> ResultList;
	TArray<TSharedPtr<FAssetEntry>> Results;
	FString CurrentQuery;
};
