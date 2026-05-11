// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SNamingLinterWindow.h"
#include "BlenderAssetBrowserEditorModule.h"

#include "AssetLibrarySubsystem.h"
#include "AssetLibraryTypes.h"
#include "AssetLibraryDatabase.h"
#include "NamingLinter.h"

#include "Editor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"

#define LOCTEXT_NAMESPACE "BABNamingLinter"

void SNamingLinterWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
		.Padding(8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(LOCTEXT("Scanning", "Scanning..."))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SButton)
					.Text(LOCTEXT("Rescan", "Rescan"))
					.OnClicked(this, &SNamingLinterWindow::OnRescanClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyFixes", "Apply checked renames"))
					.OnClicked(this, &SNamingLinterWindow::OnApplyClicked)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()[ SNew(SSeparator) ]
			+ SVerticalBox::Slot().FillHeight(1).Padding(4)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FRow>>)
				.ListItemsSource(&Rows)
				.OnGenerateRow(this, &SNamingLinterWindow::MakeRow)
				.SelectionMode(ESelectionMode::None)
			]
		]
	];

	Refresh();
}

void SNamingLinterWindow::Refresh()
{
	Rows.Reset();
	UAssetLibrarySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetLibrarySubsystem>() : nullptr;
	if (!Sub || !Sub->IsReady())
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("NoSubsystem", "AssetLibrarySubsystem not ready."));
		}
		if (ListView.IsValid()) { ListView->RequestListRefresh(); }
		return;
	}

	const TArray<FNamingViolation> Violations = FNamingLinter::Scan(Sub);
	for (const FNamingViolation& V : Violations)
	{
		auto R = MakeShared<FRow>();
		R->V = V;
		R->bChecked = true;
		Rows.Add(R);
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(FString::Printf(
			TEXT("%d naming violation(s) found."), Rows.Num())));
	}
	if (ListView.IsValid()) { ListView->RequestListRefresh(); }
}

TSharedRef<ITableRow> SNamingLinterWindow::MakeRow(TSharedPtr<FRow> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	if (!Item.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FRow>>, Owner);
	}
	const FString Label = FString::Printf(TEXT("%s  →  %s   [%s]"),
		*Item->V.AssetName, *Item->V.SuggestedName, *Item->V.AssetType);

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
			[
				SNew(STextBlock).Text(FText::FromString(Label))
			]
		];
}

FReply SNamingLinterWindow::OnRescanClicked()
{
	Refresh();
	return FReply::Handled();
}

FReply SNamingLinterWindow::OnApplyClicked()
{
	UAssetLibrarySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetLibrarySubsystem>() : nullptr;
	if (!Sub || !Sub->IsReady()) { return FReply::Handled(); }

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetRenameData> Renames;
	for (const TSharedPtr<FRow>& R : Rows)
	{
		if (!R.IsValid() || !R->bChecked) { continue; }
		// Lookup the live UObject from the asset registry by the recorded path.
		FAssetEntry E = Sub->GetAssetById(R->V.AssetId);
		if (E.Id <= 0) { continue; }

		const FString ObjectPath = E.AssetPath + TEXT(".") + E.AssetName;
		FAssetData Data = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (!Data.IsValid()) { continue; }
		UObject* Asset = Data.GetAsset();
		if (!Asset) { continue; }

		// New name = suggested; keep folder + package suffix the same.
		const FString OldPackagePath = FPackageName::GetLongPackagePath(E.AssetPath);
		Renames.Add(FAssetRenameData(Asset, OldPackagePath, R->V.SuggestedName));
	}

	if (Renames.Num() == 0)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("NoneSelected", "Nothing checked — nothing renamed."));
		}
		return FReply::Handled();
	}

	const bool bOk = AssetTools.RenameAssets(Renames);
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(FString::Printf(
			TEXT("Renamed %d asset(s). %s"),
			Renames.Num(),
			bOk ? TEXT("OK") : TEXT("Some failed — see Output Log."))));
	}
	UE_LOG(LogBlenderAssetBrowserEditor, Log,
		TEXT("NamingLinter: applied %d rename(s), result=%s"),
		Renames.Num(), bOk ? TEXT("OK") : TEXT("partial"));
	Refresh();
	return FReply::Handled();
}

void SNamingLinterWindow::ShowWindow()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "Naming Linter"))
		.ClientSize(FVector2D(900, 600))
		.SupportsMaximize(true)
		.SupportsMinimize(true);

	Window->SetContent(SNew(SNamingLinterWindow));
	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
