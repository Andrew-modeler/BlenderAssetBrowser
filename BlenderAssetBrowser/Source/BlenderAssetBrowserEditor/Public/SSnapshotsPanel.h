// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Lists all snapshots and lets the user restore or delete them.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "SnapshotManager.h"

class STextBlock;

class SSnapshotsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSnapshotsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	static void ShowWindow();

private:
	void Refresh();
	TSharedRef<class ITableRow> MakeRow(TSharedPtr<FSnapshotEntry> Item,
		const TSharedRef<class STableViewBase>& Owner);

	TSharedPtr<class SListView<TSharedPtr<FSnapshotEntry>>> ListView;
	TSharedPtr<STextBlock> StatusText;
	TArray<TSharedPtr<FSnapshotEntry>> Rows;
};
