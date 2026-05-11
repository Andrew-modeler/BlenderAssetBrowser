// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SSnapshotsPanel.h"
#include "BlenderAssetBrowserEditorModule.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SWindow.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "BABSnapshots"

void SSnapshotsPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel"))).Padding(8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[ SAssignNew(StatusText, STextBlock).Text(LOCTEXT("Listing", "Loading...")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SButton).Text(LOCTEXT("Rescan", "Rescan"))
					.OnClicked_Lambda([this]() { Refresh(); return FReply::Handled(); })
			]
			+ SVerticalBox::Slot().FillHeight(1).Padding(2)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FSnapshotEntry>>)
				.ListItemsSource(&Rows)
				.OnGenerateRow(this, &SSnapshotsPanel::MakeRow)
				.SelectionMode(ESelectionMode::None)
			]
		]
	];
	Refresh();
}

void SSnapshotsPanel::Refresh()
{
	Rows.Reset();
	for (const FSnapshotEntry& E : FSnapshotManager::ListSnapshots())
	{
		Rows.Add(MakeShared<FSnapshotEntry>(E));
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(FString::Printf(
			TEXT("%d snapshot(s)."), Rows.Num())));
	}
	if (ListView.IsValid()) { ListView->RequestListRefresh(); }
}

TSharedRef<ITableRow> SSnapshotsPanel::MakeRow(TSharedPtr<FSnapshotEntry> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	if (!Item.IsValid()) { return SNew(STableRow<TSharedPtr<FSnapshotEntry>>, Owner); }
	const FString Label = FString::Printf(TEXT("%s   (%d files)"),
		*Item->Timestamp, Item->Files.Num());
	return SNew(STableRow<TSharedPtr<FSnapshotEntry>>, Owner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).Padding(4, 2)
			[ SNew(STextBlock).Text(FText::FromString(Label)) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Restore", "Restore"))
				.OnClicked_Lambda([this, Item]() {
					const int32 N = FSnapshotManager::RestoreSnapshot(*Item);
					if (StatusText.IsValid())
					{
						StatusText->SetText(FText::FromString(FString::Printf(
							TEXT("Restored %d file(s) from %s"), N, *Item->Timestamp)));
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Delete", "Delete"))
				.OnClicked_Lambda([this, Item]() {
					FSnapshotManager::DeleteSnapshot(*Item);
					Refresh();
					return FReply::Handled();
				})
			]
		];
}

void SSnapshotsPanel::ShowWindow()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "Library Snapshots"))
		.ClientSize(FVector2D(640, 400))
		.SupportsMaximize(true).SupportsMinimize(true);
	Window->SetContent(SNew(SSnapshotsPanel));
	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
