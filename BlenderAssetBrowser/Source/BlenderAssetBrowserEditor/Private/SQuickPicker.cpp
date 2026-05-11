// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SQuickPicker.h"
#include "BlenderAssetBrowserEditorModule.h"

#include "AssetLibrarySubsystem.h"
#include "SearchEngine.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "BABQuickPicker"

void SQuickPicker::Construct(const FArguments& InArgs)
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
				SAssignNew(SearchBox, SEditableTextBox)
				.HintText(LOCTEXT("QuickPickerHint", "Find asset…"))
				.OnTextChanged(this, &SQuickPicker::OnSearchTextChanged)
			]
			+ SVerticalBox::Slot().FillHeight(1).Padding(4)
			[
				SNew(SBox).MinDesiredHeight(300).MaxDesiredHeight(500)
				[
					SAssignNew(ResultList, SListView<TSharedPtr<FAssetEntry>>)
					.ListItemsSource(&Results)
					.OnGenerateRow(this, &SQuickPicker::MakeRow)
					.OnSelectionChanged(this, &SQuickPicker::OnSelectionChanged)
					.SelectionMode(ESelectionMode::Single)
				]
			]
		]
	];

	Refresh();
}

void SQuickPicker::Refresh()
{
	Results.Reset();
	UAssetLibrarySubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetLibrarySubsystem>() : nullptr;
	if (!Sub || !Sub->IsReady()) { if (ResultList.IsValid()) { ResultList->RequestListRefresh(); } return; }

	FSearchEngine Engine(Sub);
	TArray<FAssetEntry> Found = CurrentQuery.IsEmpty()
		? Sub->GetAllAssets(0)
		: Engine.Search(CurrentQuery, 50);

	for (const FAssetEntry& A : Found)
	{
		Results.Add(MakeShared<FAssetEntry>(A));
	}
	if (ResultList.IsValid()) { ResultList->RequestListRefresh(); }
}

void SQuickPicker::OnSearchTextChanged(const FText& Text)
{
	CurrentQuery = Text.ToString();
	if (CurrentQuery.Len() > 256) { CurrentQuery.LeftInline(256); }
	Refresh();
}

TSharedRef<ITableRow> SQuickPicker::MakeRow(TSharedPtr<FAssetEntry> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	const FString Label = Item.IsValid()
		? FString::Printf(TEXT("%s   (%s)"), *Item->AssetName, *Item->AssetType)
		: FString(TEXT("(null)"));
	return SNew(STableRow<TSharedPtr<FAssetEntry>>, Owner)
		[
			SNew(STextBlock).Text(FText::FromString(Label))
		];
}

void SQuickPicker::OnSelectionChanged(TSharedPtr<FAssetEntry> Item, ESelectInfo::Type Info)
{
	if (Info == ESelectInfo::OnMouseClick || Info == ESelectInfo::OnKeyPress)
	{
		Spawn(Item);
	}
}

void SQuickPicker::Spawn(TSharedPtr<FAssetEntry> Item)
{
	if (!Item.IsValid() || !GEditor) { return; }
	UE_LOG(LogBlenderAssetBrowserEditor, Log, TEXT("QuickPicker selected: %s"), *Item->AssetPath);

	// Mark as recent — best-effort.
	if (UAssetLibrarySubsystem* Sub = GEditor->GetEditorSubsystem<UAssetLibrarySubsystem>())
	{
		if (Sub->IsReady() && Item->Id > 0) { Sub->TouchRecent(Item->Id); }
	}

	// Spawn — only meaningful for StaticMesh in the current scope. Other
	// asset types (Material, Texture, ...) need a different drop target.
	if (Item->AssetType != TEXT("StaticMesh")) { return; }

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const FString ObjectPath = Item->AssetPath + TEXT(".") + FPaths::GetCleanFilename(Item->AssetPath);
	FAssetData Data = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
	if (!Data.IsValid()) { return; }

	UStaticMesh* Mesh = Cast<UStaticMesh>(Data.GetAsset());
	if (!Mesh) { return; }

	// Find the active level-viewport client. If we have a level editor with
	// a viewport that has focus, use its camera ray; otherwise spawn at origin.
	FVector SpawnLoc = FVector::ZeroVector;
	if (FLevelEditorViewportClient* VC = GCurrentLevelEditingViewportClient)
	{
		// Take camera position projected slightly forward — keeps the actor in view.
		const FVector CamLoc = VC->GetViewLocation();
		const FRotator CamRot = VC->GetViewRotation();
		SpawnLoc = CamLoc + CamRot.Vector() * 500.f;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { return; }

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(SpawnLoc, FRotator::ZeroRotator, Params))
	{
		if (Actor->GetStaticMeshComponent())
		{
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		}
		Actor->SetActorLabel(Mesh->GetName());
	}
}

void SQuickPicker::ShowOverlay()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("QuickPickerTitle", "Asset Picker"))
		.ClientSize(FVector2D(640, 480))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true);

	TSharedRef<SQuickPicker> Picker = SNew(SQuickPicker);
	Window->SetContent(Picker);
	FSlateApplication::Get().AddWindow(Window);
}

FReply SQuickPicker::OnKeyDownAtRoot(const FGeometry& Geo, const FKeyEvent& Key)
{
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
