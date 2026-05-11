// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SAssetBrowserWindow.h"
#include "BlenderAssetBrowserEditorModule.h"
#include "SNamingLinterWindow.h"
#include "SSnapshotsPanel.h"
#include "SReferenceBoard.h"

#include "AssetLibrarySubsystem.h"
#include "AssetLibraryTypes.h"
#include "SearchEngine.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "Input/DragAndDrop.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetThumbnail.h"
#include "CatalogDragDrop.h"
#include "CatalogManager.h"

#include "Editor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "BlenderAssetBrowserWindow"

UAssetLibrarySubsystem* SAssetBrowserWindow::GetSubsystem() const
{
	if (!GEditor) { return nullptr; }
	return GEditor->GetEditorSubsystem<UAssetLibrarySubsystem>();
}

void SAssetBrowserWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// --- Toolbar ---
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshButton", "Refresh"))
				.OnClicked_Lambda([this]() { Refresh(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddCatalog", "+ Catalog"))
				.OnClicked(this, &SAssetBrowserWindow::OnAddRootCatalogClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("DeleteCatalog", "- Catalog"))
				.OnClicked(this, &SAssetBrowserWindow::OnDeleteSelectedCatalogClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("NamingLinter", "Naming Linter"))
				.ToolTipText(LOCTEXT("NamingLinterTip", "Scan the library for naming-convention violations and optionally batch-rename."))
				.OnClicked_Lambda([]() { SNamingLinterWindow::ShowWindow(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("ToggleGrid", "List/Grid"))
				.ToolTipText(LOCTEXT("ToggleGridTip", "Toggle between list and thumbnail-grid view."))
				.OnClicked(this, &SAssetBrowserWindow::OnToggleGridClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Snapshots", "Snapshots"))
				.ToolTipText(LOCTEXT("SnapshotsTip", "List library snapshots; restore or delete."))
				.OnClicked_Lambda([]() { SSnapshotsPanel::ShowWindow(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefBoard", "Refs"))
				.ToolTipText(LOCTEXT("RefBoardTip", "Open the reference board (pinned URLs / paths)."))
				.OnClicked_Lambda([]() { SReferenceBoard::ShowWindow(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(8, 2)
			[
				SAssignNew(SearchBox, SEditableTextBox)
				.HintText(LOCTEXT("SearchHint",
					"Search: text  type:StaticMesh  tris:<5000  source:fab"))
				.OnTextChanged(this, &SAssetBrowserWindow::OnSearchTextChanged)
				.OnTextCommitted(this, &SAssetBrowserWindow::OnSearchTextCommitted)
			]
		]

		+ SVerticalBox::Slot().AutoHeight()[ SNew(SSeparator) ]

		// --- Body: tree | grid | inspector ---
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SNew(SSplitter)

			// Catalog tree
			+ SSplitter::Slot().Value(0.2f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
				[
					SAssignNew(CatalogTree, STreeView<TSharedPtr<FCatalogEntry>>)
					.TreeItemsSource(&RootCatalogs)
					.OnGenerateRow(this, &SAssetBrowserWindow::MakeCatalogRow)
					.OnGetChildren(this, &SAssetBrowserWindow::GetCatalogChildren)
					.OnSelectionChanged(this, &SAssetBrowserWindow::OnCatalogSelectionChanged)
					.SelectionMode(ESelectionMode::Single)
				]
			]

			// Asset content — list or grid, rebuilt by RebuildContentArea().
			+ SSplitter::Slot().Value(0.55f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
				[
					SAssignNew(ContentArea, SBox)
				]
			]

			// Inspector
			+ SSplitter::Slot().Value(0.25f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
				.Padding(8)
				[
					SAssignNew(InspectorText, STextBlock)
					.Text(LOCTEXT("InspectorEmpty", "Select an asset to inspect"))
					.AutoWrapText(true)
				]
			]
		]
	];

	RebuildContentArea();
	Refresh();
}

void SAssetBrowserWindow::RebuildContentArea()
{
	if (!ContentArea.IsValid()) { return; }
	if (bGridMode)
	{
		ContentArea->SetContent(
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(AssetGrid, SWrapBox)
				.UseAllottedSize(true)
				.Orientation(EOrientation::Orient_Horizontal)
				.InnerSlotPadding(FVector2D(6, 6))
			]);
	}
	else
	{
		ContentArea->SetContent(
			SAssignNew(AssetList, SListView<TSharedPtr<FAssetEntry>>)
			.ListItemsSource(&AssetItems)
			.OnGenerateRow(this, &SAssetBrowserWindow::MakeAssetRow)
			.OnSelectionChanged(this, &SAssetBrowserWindow::OnAssetSelectionChanged)
			.SelectionMode(ESelectionMode::Single));
	}
}

FReply SAssetBrowserWindow::OnToggleGridClicked()
{
	bGridMode = !bGridMode;
	RebuildContentArea();
	Refresh();
	return FReply::Handled();
}

void SAssetBrowserWindow::Refresh()
{
	RootCatalogs.Reset();
	CatalogChildren.Reset();
	AssetItems.Reset();

	UAssetLibrarySubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->IsReady())
	{
		if (CatalogTree.IsValid()) { CatalogTree->RequestTreeRefresh(); }
		if (AssetList.IsValid())   { AssetList->RequestListRefresh(); }
		return;
	}

	// Catalogs: bucket by parent_id.
	TArray<FCatalogEntry> AllCats = Sub->GetAllCatalogs();
	for (const FCatalogEntry& C : AllCats)
	{
		auto P = MakeShared<FCatalogEntry>(C);
		if (C.ParentId == 0)
		{
			RootCatalogs.Add(P);
		}
		else
		{
			CatalogChildren.FindOrAdd(C.ParentId).Add(P);
		}
	}

	// Assets — apply current search if any; otherwise filter by selected catalog.
	TArray<FAssetEntry> AssetsRaw;
	if (!CurrentSearchText.IsEmpty())
	{
		FSearchEngine Engine(Sub);
		AssetsRaw = Engine.Search(CurrentSearchText, 500);
	}
	else if (SelectedCatalogId > 0)
	{
		AssetsRaw = Sub->GetAssetsInCatalog(SelectedCatalogId, 1000);
	}
	else
	{
		AssetsRaw = Sub->GetAllAssets(0);
	}

	for (const FAssetEntry& A : AssetsRaw)
	{
		AssetItems.Add(MakeShared<FAssetEntry>(A));
	}

	if (CatalogTree.IsValid()) { CatalogTree->RequestTreeRefresh(); }
	if (AssetList.IsValid())   { AssetList->RequestListRefresh(); }

	// Grid mode: cards built around UE's own FAssetThumbnail, which produces
	// the same thumbnail the Content Browser shows (and cleans up correctly).
	// Bug #1 fix: FSlateDynamicImageBrush::CreateWithImageData with empty
	// bytes was silently broken — it ignores the path argument when no
	// bytes are supplied and produces a blank brush.
	if (bGridMode && AssetGrid.IsValid())
	{
		AssetGrid->ClearChildren();

		if (!ThumbnailPool.IsValid())
		{
			// 256 slots × 128 px is plenty for a single library view. Pool
			// recycles GPU resources as cards scroll out of view.
			ThumbnailPool = MakeShared<FAssetThumbnailPool>(/*NumPooledItems*/ 256);
		}

		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Registry = ARM.Get();

		for (const TSharedPtr<FAssetEntry>& Item : AssetItems)
		{
			if (!Item.IsValid()) { continue; }
			const FString Label = FString::Printf(TEXT("%s\n[%s]"),
				*Item->AssetName, *Item->AssetType);

			const FString ObjectPath = Item->AssetPath + TEXT(".") +
				FPaths::GetCleanFilename(Item->AssetPath);
			FAssetData Data = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));

			TSharedRef<SWidget> Inner = SNullWidget::NullWidget;
			if (Data.IsValid())
			{
				TSharedRef<FAssetThumbnail> Thumb =
					MakeShared<FAssetThumbnail>(Data, 128, 128, ThumbnailPool);
				FAssetThumbnailConfig ThumbConfig;
				ThumbConfig.bAllowFadeIn = true;
				ThumbConfig.bAllowHintText = false;

				Inner = SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).WidthOverride(128).HeightOverride(128)
						[
							Thumb->MakeThumbnailWidget(ThumbConfig)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(2)
					[
						SNew(STextBlock).Text(FText::FromString(Item->AssetName))
						.Justification(ETextJustify::Center)
					];
			}
			else
			{
				Inner = SNew(SBox).WidthOverride(128).HeightOverride(140)
					[
						SNew(STextBlock).Text(FText::FromString(Label))
						.Justification(ETextJustify::Center).AutoWrapText(true)
					];
			}

			TSharedRef<SButton> Card = SNew(SButton)
				.OnClicked_Lambda([this, Item]() {
					this->OnAssetSelectionChanged(Item, ESelectInfo::OnMouseClick);
					return FReply::Handled();
				})
				.ContentPadding(2)
				[ Inner ];
			AssetGrid->AddSlot()[ Card ];
		}
	}
}

void SAssetBrowserWindow::OnSearchTextChanged(const FText& Text)
{
	CurrentSearchText = Text.ToString();
	// Cap to avoid pathological query times.
	if (CurrentSearchText.Len() > 1024) { CurrentSearchText.LeftInline(1024); }
}

void SAssetBrowserWindow::OnSearchTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		Refresh();
	}
}

FReply SAssetBrowserWindow::OnAddRootCatalogClicked()
{
	UAssetLibrarySubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->IsReady()) { return FReply::Handled(); }

	FCatalogEntry Cat;
	Cat.Name = FString::Printf(TEXT("New Catalog %d"), FMath::RandRange(100, 9999));
	Cat.ParentId = 0;
	Sub->AddCatalog(Cat);
	Refresh();
	return FReply::Handled();
}

FReply SAssetBrowserWindow::OnDeleteSelectedCatalogClicked()
{
	UAssetLibrarySubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->IsReady() || SelectedCatalogId <= 0) { return FReply::Handled(); }
	Sub->DeleteCatalog(SelectedCatalogId);
	SelectedCatalogId = 0;
	Refresh();
	return FReply::Handled();
}

TSharedRef<ITableRow> SAssetBrowserWindow::MakeCatalogRow(TSharedPtr<FCatalogEntry> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	const FString Label = Item.IsValid()
		? FString::Printf(TEXT("%s%s"), *Item->Name, Item->bIsSmart ? TEXT("  [smart]") : TEXT(""))
		: FString(TEXT("(null)"));
	const int64 CatId = Item.IsValid() ? Item->Id : 0;

	using FRowType = STableRow<TSharedPtr<FCatalogEntry>>;
	return SNew(FRowType, Owner)
		.OnDragDetected_Lambda([CatId](const FGeometry&, const FPointerEvent&) -> FReply
		{
			if (CatId <= 0) { return FReply::Unhandled(); }
			return FReply::Handled().BeginDragDrop(FCatalogDragDropOp::New(CatId));
		})
		.OnAcceptDrop_Lambda([this](const FDragDropEvent& Event, EItemDropZone Zone,
			TSharedPtr<FCatalogEntry> Target) -> FReply
		{
			TSharedPtr<FCatalogDragDropOp> Op = Event.GetOperationAs<FCatalogDragDropOp>();
			if (!Op.IsValid() || !Target.IsValid() || Target->Id == 0) { return FReply::Unhandled(); }
			if (Op->GetCatalogId() == Target->Id) { return FReply::Unhandled(); }
			UAssetLibrarySubsystem* Sub = this->GetSubsystem();
			if (!Sub || !Sub->IsReady()) { return FReply::Unhandled(); }
			FCatalogManager Mgr(Sub);
			if (Mgr.Reparent(Op->GetCatalogId(), Target->Id))
			{
				this->Refresh();
				return FReply::Handled();
			}
			return FReply::Unhandled();
		})
		.OnCanAcceptDrop_Lambda([](const FDragDropEvent& Event, EItemDropZone,
			TSharedPtr<FCatalogEntry>) -> TOptional<EItemDropZone>
		{
			TSharedPtr<FCatalogDragDropOp> Op = Event.GetOperationAs<FCatalogDragDropOp>();
			if (Op.IsValid()) { return EItemDropZone::OntoItem; }
			return TOptional<EItemDropZone>();
		})
		[
			SNew(STextBlock).Text(FText::FromString(Label))
		];
}

void SAssetBrowserWindow::GetCatalogChildren(TSharedPtr<FCatalogEntry> Item,
	TArray<TSharedPtr<FCatalogEntry>>& OutChildren)
{
	if (!Item.IsValid()) { return; }
	if (TArray<TSharedPtr<FCatalogEntry>>* Found = CatalogChildren.Find(Item->Id))
	{
		OutChildren = *Found;
	}
}

void SAssetBrowserWindow::OnCatalogSelectionChanged(TSharedPtr<FCatalogEntry> Item, ESelectInfo::Type Info)
{
	SelectedCatalogId = Item.IsValid() ? Item->Id : 0;
	Refresh();
}

TSharedRef<ITableRow> SAssetBrowserWindow::MakeAssetRow(TSharedPtr<FAssetEntry> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	if (!Item.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FAssetEntry>>, Owner);
	}

	// Single-character provenance badge: F = Fab, M = Megascans, L = Local,
	// I = Imported, C = Custom, ? = Unknown. Cheap and works without icons.
	auto SourceBadge = [](EAssetLibrarySource S) -> FString
	{
		switch (S)
		{
		case EAssetLibrarySource::Fab:       return TEXT("F");
		case EAssetLibrarySource::Megascans: return TEXT("M");
		case EAssetLibrarySource::Local:     return TEXT("L");
		case EAssetLibrarySource::Imported:  return TEXT("I");
		case EAssetLibrarySource::Custom:    return TEXT("C");
		default:                             return TEXT("?");
		}
	};

	const FString Label = FString::Printf(TEXT("[%s] %s   [%s]"),
		*SourceBadge(Item->SourceType), *Item->AssetName, *Item->AssetType);

	return SNew(STableRow<TSharedPtr<FAssetEntry>>, Owner)
		[
			SNew(STextBlock).Text(FText::FromString(Label))
		];
}

FReply SAssetBrowserWindow::OnDragOver(const FGeometry& Geo, const FDragDropEvent& Event)
{
	TSharedPtr<FExternalDragOperation> Ext = Event.GetOperationAs<FExternalDragOperation>();
	if (Ext.IsValid() && Ext->HasFiles())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SAssetBrowserWindow::OnDragLeave(const FDragDropEvent& Event)
{
}

FReply SAssetBrowserWindow::OnDrop(const FGeometry& Geo, const FDragDropEvent& Event)
{
	TSharedPtr<FExternalDragOperation> Ext = Event.GetOperationAs<FExternalDragOperation>();
	if (!Ext.IsValid() || !Ext->HasFiles())
	{
		return FReply::Unhandled();
	}

	const TArray<FString>& Files = Ext->GetFiles();
	// Safety: cap how many files we process per drop.
	if (Files.Num() == 0 || Files.Num() > 256) { return FReply::Unhandled(); }

	// Import everything into /Game/ImportedFromBrowser/ (mirrors the
	// Eleganza pattern of routing OS drops through a dedicated subfolder).
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UAutomatedAssetImportData* Data = NewObject<UAutomatedAssetImportData>();
	Data->bReplaceExisting = false;
	Data->DestinationPath = TEXT("/Game/ImportedFromBrowser");
	Data->Filenames = Files;
	const TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(Data);

	UE_LOG(LogBlenderAssetBrowserEditor, Log,
		TEXT("OS drop: imported %d asset(s) from %d file(s)."),
		Imported.Num(), Files.Num());

	// Refresh the asset list so the user sees them.
	Refresh();
	return FReply::Handled();
}

void SAssetBrowserWindow::OnAssetSelectionChanged(TSharedPtr<FAssetEntry> Item, ESelectInfo::Type Info)
{
	if (!InspectorText.IsValid()) { return; }
	if (!Item.IsValid())
	{
		InspectorText->SetText(LOCTEXT("InspectorEmpty", "Select an asset to inspect"));
		return;
	}

	UAssetLibrarySubsystem* Sub = GetSubsystem();
	FAssetEntry Full = (Sub && Sub->IsReady() && Item->Id > 0)
		? Sub->GetAssetById(Item->Id)
		: *Item;

	// Source-type display.
	auto SourceLabel = [](EAssetLibrarySource S) -> const TCHAR*
	{
		switch (S)
		{
		case EAssetLibrarySource::Local:     return TEXT("Local");
		case EAssetLibrarySource::Fab:       return TEXT("Fab");
		case EAssetLibrarySource::Megascans: return TEXT("Megascans");
		case EAssetLibrarySource::Custom:    return TEXT("Custom");
		case EAssetLibrarySource::Imported:  return TEXT("Imported");
		default:                             return TEXT("Unknown");
		}
	};

	auto UpdateStateLabel = [](EAssetUpdateState S) -> const TCHAR*
	{
		switch (S)
		{
		case EAssetUpdateState::UpToDate:          return TEXT("Up to date");
		case EAssetUpdateState::UpdateAvailable:   return TEXT("Update available");
		case EAssetUpdateState::SourceFileChanged: return TEXT("Source file changed");
		default:                                   return TEXT("Unknown");
		}
	};

	auto FmtBytes = [](int64 B) -> FString
	{
		if (B <= 0) { return TEXT("(unknown)"); }
		if (B < 1024) { return FString::Printf(TEXT("%lld B"), B); }
		if (B < 1024 * 1024) { return FString::Printf(TEXT("%.1f KB"), B / 1024.0); }
		if (B < 1024 * 1024 * 1024) { return FString::Printf(TEXT("%.1f MB"), B / (1024.0 * 1024.0)); }
		return FString::Printf(TEXT("%.2f GB"), B / (1024.0 * 1024.0 * 1024.0));
	};

	// Collect tag names — one line, comma separated.
	FString TagsLine;
	if (Sub && Sub->IsReady() && Full.Id > 0)
	{
		TArray<FString> TagNames;
		for (const FTagEntry& T : Sub->GetAssetTags(Full.Id)) { TagNames.Add(T.Name); }
		TagsLine = FString::Join(TagNames, TEXT(", "));
	}

	FString Text;
	Text.Reserve(2048);
	Text += FString::Printf(TEXT("%s\n"), *Full.AssetName);
	Text += FString::Printf(TEXT("Type: %s\nPath: %s\n"), *Full.AssetType, *Full.AssetPath);
	Text += FString::Printf(TEXT("Rating: %d/5\n"), Full.Rating);
	Text += TEXT("\n-- Technical --\n");
	if (Full.TriCount > 0)      { Text += FString::Printf(TEXT("Triangles: %d\n"), Full.TriCount); }
	if (Full.VertCount > 0)     { Text += FString::Printf(TEXT("Vertices: %d\n"), Full.VertCount); }
	if (Full.LodCount > 0)      { Text += FString::Printf(TEXT("LODs: %d\n"), Full.LodCount); }
	if (Full.TextureResMax > 0) { Text += FString::Printf(TEXT("Max texture res: %d\n"), Full.TextureResMax); }
	if (Full.MaterialCount > 0) { Text += FString::Printf(TEXT("Materials: %d\n"), Full.MaterialCount); }
	if (Full.bHasCollision)     { Text += FString::Printf(TEXT("Collision: %s\n"), *Full.CollisionType); }
	if (Full.DiskSizeBytes > 0) { Text += FString::Printf(TEXT("Disk size: %s\n"), *FmtBytes(Full.DiskSizeBytes)); }
	if (!Full.EngineVersion.IsEmpty()) { Text += FString::Printf(TEXT("Engine: %s\n"), *Full.EngineVersion); }
	Text += TEXT("\n-- Source --\n");
	Text += FString::Printf(TEXT("Origin: %s\n"), SourceLabel(Full.SourceType));
	if (!Full.SourcePackName.IsEmpty()) { Text += FString::Printf(TEXT("Pack: %s\n"), *Full.SourcePackName); }
	if (!Full.SourceAuthor.IsEmpty())   { Text += FString::Printf(TEXT("Author: %s\n"), *Full.SourceAuthor); }
	if (!Full.SourceLicense.IsEmpty())  { Text += FString::Printf(TEXT("License: %s\n"), *Full.SourceLicense); }
	if (!Full.SourceVersion.IsEmpty())  { Text += FString::Printf(TEXT("Version: %s\n"), *Full.SourceVersion); }
	if (!Full.SourceUrl.IsEmpty())      { Text += FString::Printf(TEXT("URL: %s\n"), *Full.SourceUrl); }
	if (Full.UpdateState != EAssetUpdateState::Unknown)
	{
		Text += FString::Printf(TEXT("Update state: %s\n"), UpdateStateLabel(Full.UpdateState));
	}
	if (!TagsLine.IsEmpty())
	{
		Text += FString::Printf(TEXT("\n-- Tags --\n%s\n"), *TagsLine);
	}
	if (!Full.Notes.IsEmpty())
	{
		Text += FString::Printf(TEXT("\n-- Notes --\n%s\n"), *Full.Notes);
	}

	InspectorText->SetText(FText::FromString(Text));

	// Mark as recent — best-effort, ignore failure.
	if (Sub && Sub->IsReady() && Full.Id > 0) { Sub->TouchRecent(Full.Id); }
}

#undef LOCTEXT_NAMESPACE
