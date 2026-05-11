// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Shows the user the top-N SigLIP suggestions for an asset and lets them
// pick which to accept. Each accepted tag is written to the DB with
// source='ai' + the confidence score from the model.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SAITagReviewWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAITagReviewWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, int64 InAssetId, const TArray<TPair<FString, float>>& InSuggestions);

	static void ShowModal(int64 AssetId, const TArray<TPair<FString, float>>& Suggestions);

private:
	struct FRow { FString Name; float Score = 0.f; bool bChecked = true; };
	int64 AssetId = 0;
	TArray<TSharedPtr<FRow>> Rows;

	TSharedRef<class ITableRow> MakeRow(TSharedPtr<FRow> Item,
		const TSharedRef<class STableViewBase>& Owner);

	FReply OnAcceptClicked();
};
