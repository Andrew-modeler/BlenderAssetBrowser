// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SAITagReviewWindow.h"
#include "BlenderAssetBrowserEditorModule.h"

#include "AssetLibrarySubsystem.h"
#include "AssetLibraryTypes.h"

#include "Editor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SWindow.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "BABAITagReview"

void SAITagReviewWindow::Construct(const FArguments& InArgs, int64 InAssetId,
	const TArray<TPair<FString, float>>& InSuggestions)
{
	AssetId = InAssetId;
	for (const TPair<FString, float>& S : InSuggestions)
	{
		auto R = MakeShared<FRow>();
		R->Name = S.Key;
		R->Score = S.Value;
		Rows.Add(R);
	}

	ChildSlot
	[
		SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel"))).Padding(8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("AI suggested %d tags. Tick those you want to keep."),
					Rows.Num())))
			]
			+ SVerticalBox::Slot().FillHeight(1).Padding(2)
			[
				SNew(SListView<TSharedPtr<FRow>>)
				.ListItemsSource(&Rows)
				.OnGenerateRow(this, &SAITagReviewWindow::MakeRow)
				.SelectionMode(ESelectionMode::None)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Accept", "Apply checked tags"))
				.OnClicked(this, &SAITagReviewWindow::OnAcceptClicked)
			]
		]
	];
}

TSharedRef<ITableRow> SAITagReviewWindow::MakeRow(TSharedPtr<FRow> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	if (!Item.IsValid()) { return SNew(STableRow<TSharedPtr<FRow>>, Owner); }
	const FString Label = FString::Printf(TEXT("%s   (%.2f)"), *Item->Name, Item->Score);
	return SNew(STableRow<TSharedPtr<FRow>>, Owner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]() {
					return Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Item](ECheckBoxState S) {
					Item->bChecked = (S == ECheckBoxState::Checked);
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(4, 2)
			[ SNew(STextBlock).Text(FText::FromString(Label)) ]
		];
}

FReply SAITagReviewWindow::OnAcceptClicked()
{
	UAssetLibrarySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetLibrarySubsystem>() : nullptr;
	if (!Sub || !Sub->IsReady() || AssetId <= 0) { return FReply::Handled(); }

	int32 Applied = 0;
	for (const TSharedPtr<FRow>& R : Rows)
	{
		if (!R.IsValid() || !R->bChecked) { continue; }
		FTagEntry TE; TE.Name = R->Name;
		const int64 TagId = Sub->AddTag(TE);
		if (TagId > 0 && Sub->AssignTag(AssetId, TagId, TEXT("ai"), R->Score)) { ++Applied; }
	}
	UE_LOG(LogBlenderAssetBrowserEditor, Log,
		TEXT("AITagReview: applied %d tag(s) to asset %lld."), Applied, AssetId);

	if (TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(AsShared()))
	{
		Win->RequestDestroyWindow();
	}
	return FReply::Handled();
}

void SAITagReviewWindow::ShowModal(int64 AssetId, const TArray<TPair<FString, float>>& Suggestions)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "AI Tag Review"))
		.ClientSize(FVector2D(520, 480))
		.SupportsMaximize(false).SupportsMinimize(false);
	Window->SetContent(SNew(SAITagReviewWindow, AssetId, Suggestions));
	GEditor->EditorAddModalWindow(Window);
}

#undef LOCTEXT_NAMESPACE
