// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Modal-ish window that shows asset-name violations and lets the user
// batch-fix them via FAssetTools::RenameAssets.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "NamingLinter.h"

class STextBlock;

class SNamingLinterWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNamingLinterWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Convenience: open the window as a non-blocking floating window. */
	static void ShowWindow();

private:
	struct FRow
	{
		FNamingViolation V;
		bool bChecked = true;
	};

	void Refresh();
	TSharedRef<class ITableRow> MakeRow(TSharedPtr<FRow> Item,
		const TSharedRef<class STableViewBase>& Owner);
	FReply OnRescanClicked();
	FReply OnApplyClicked();

	TSharedPtr<class SListView<TSharedPtr<FRow>>> ListView;
	TSharedPtr<STextBlock> StatusText;
	TArray<TSharedPtr<FRow>> Rows;
};
