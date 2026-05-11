// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "ContentBrowserMenuExtension.h"
#include "BlenderAssetBrowserEditorModule.h"

#include "AssetLibrarySubsystem.h"
#include "AssetLibraryTypes.h"
#include "AssetLibraryDatabase.h"

#include "AssetPreviewRenderer.h"
#include "SigLIPInference.h"
#include "TagVocabulary.h"
#include "AssetLibrarySettings.h"
#include "SAITagReviewWindow.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "Styling/AppStyle.h"
#include "Editor.h"

#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Async/Async.h"
#include "Async/Future.h"

#define LOCTEXT_NAMESPACE "BlenderAssetBrowserMenu"

namespace
{
	// Whitelist of asset class names we accept. Anything else is rejected
	// by IsAssetSupported. We never trust the asset's class metadata blindly.
	const TSet<FName>& GetSupportedClasses()
	{
		static const TSet<FName> Classes = {
			FName(TEXT("StaticMesh")),
			FName(TEXT("SkeletalMesh")),
			FName(TEXT("Material")),
			FName(TEXT("MaterialInstance")),
			FName(TEXT("MaterialInstanceConstant")),
			FName(TEXT("MaterialFunction")),
			FName(TEXT("Texture2D")),
			FName(TEXT("TextureCube")),
			FName(TEXT("TextureRenderTarget2D")),
			FName(TEXT("Blueprint")),
			FName(TEXT("AnimSequence")),
			FName(TEXT("AnimMontage")),
			FName(TEXT("PoseAsset")),
			FName(TEXT("NiagaraSystem")),
			FName(TEXT("NiagaraEmitter")),
			FName(TEXT("SoundCue")),
			FName(TEXT("SoundWave")),
			FName(TEXT("MetaSoundSource")),
			FName(TEXT("DataAsset")),
			FName(TEXT("LevelInstance")),
			FName(TEXT("PCGGraph")),
		};
		return Classes;
	}

	UAssetLibrarySubsystem* GetSubsystem()
	{
		if (!GEditor) { return nullptr; }
		return GEditor->GetEditorSubsystem<UAssetLibrarySubsystem>();
	}

	int32 SafePathToInt(const FString& In, int32 MaxLen)
	{
		return FMath::Min(In.Len(), MaxLen);
	}

	/** Returns sanitized FAssetEntry built from FAssetData. Never reads UObject. */
	FAssetEntry BuildEntryFromAssetData(const FAssetData& Data)
	{
		FAssetEntry E;
		// Asset path is /Game/...; bound via prepared statement, capped by Validate().
		E.AssetPath = Data.PackageName.ToString();
		E.AssetName = Data.AssetName.ToString();
		E.AssetType = Data.AssetClassPath.GetAssetName().ToString();
		E.SourceType = EAssetLibrarySource::Imported;
		// LibraryId 0 = "this project" (no external library)
		E.LibraryId = 0;

		// Cap lengths defensively even though Validate() will also check.
		E.AssetPath.LeftInline(BAB::MAX_PATH_LEN);
		E.AssetName.LeftInline(BAB::MAX_NAME_LEN);
		E.AssetType.LeftInline(BAB::MAX_NAME_LEN);
		return E;
	}
}

void FContentBrowserMenuExtension::Register()
{
	FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	auto& AssetMenuExtenders = CB.GetAllAssetViewContextMenuExtenders();

	AssetMenuExtenders.Add(
		FContentBrowserMenuExtender_SelectedAssets::CreateLambda(
			[this](const TArray<FAssetData>& SelectedAssets) -> TSharedRef<FExtender>
			{
				TSharedRef<FExtender> Extender = MakeShared<FExtender>();

				// Filter — only show menu when at least one supported asset is selected.
				bool bHasSupported = false;
				for (const FAssetData& A : SelectedAssets)
				{
					if (IsAssetSupported(A)) { bHasSupported = true; break; }
				}
				if (!bHasSupported) { return Extender; }

				TArray<FAssetData> SelectedCopy = SelectedAssets;
				Extender->AddMenuExtension(
					TEXT("CommonAssetActions"),
					EExtensionHook::After,
					nullptr,
					FMenuExtensionDelegate::CreateLambda(
						[this, SelectedCopy](FMenuBuilder& MenuBuilder)
						{
							MenuBuilder.BeginSection(TEXT("BlenderAssetBrowser"),
								LOCTEXT("BABSection", "Blender Asset Browser"));

							MenuBuilder.AddMenuEntry(
								LOCTEXT("MarkAsAsset",  "Mark as Asset"),
								LOCTEXT("MarkAsAssetTip",
									"Add the selected asset(s) to the Blender Asset Browser library."),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda(
									[this, SelectedCopy]()
									{
										this->ExecuteMarkAsAsset(SelectedCopy);
									})));

							MenuBuilder.AddMenuEntry(
								LOCTEXT("Unmark", "Unmark from Library"),
								LOCTEXT("UnmarkTip",
									"Remove the selected asset(s) from the Blender Asset Browser library "
									"(does not delete the asset itself)."),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda(
									[this, SelectedCopy]()
									{
										this->ExecuteUnmark(SelectedCopy);
									})));

							MenuBuilder.AddMenuEntry(
								LOCTEXT("AutoTag", "Auto-tag (AI)"),
								LOCTEXT("AutoTagTip",
									"Run the SigLIP2 vision encoder on the thumbnail and store the "
									"resulting embedding so the asset can be found by visual similarity."),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda(
									[this, SelectedCopy]()
									{
										this->ExecuteAutoTag(SelectedCopy);
									})));

							// --- Bulk: rating submenu ---
							if (SelectedCopy.Num() > 1)
							{
								MenuBuilder.AddSubMenu(
									LOCTEXT("BulkRating", "Bulk: Set Rating"),
									LOCTEXT("BulkRatingTip",
										"Set the same rating on all selected assets."),
									FNewMenuDelegate::CreateLambda(
										[this, SelectedCopy](FMenuBuilder& Sub)
										{
											for (int32 R = 0; R <= 5; ++R)
											{
												Sub.AddMenuEntry(
													FText::FromString(FString::Printf(TEXT("%d stars"), R)),
													FText::GetEmpty(),
													FSlateIcon(),
													FUIAction(FExecuteAction::CreateLambda(
														[this, SelectedCopy, R]()
														{
															this->ExecuteBulkSetRating(SelectedCopy, R);
														})));
											}
										}));

								MenuBuilder.AddMenuEntry(
									LOCTEXT("BulkAddTag", "Bulk: Add Tag..."),
									LOCTEXT("BulkAddTagTip",
										"Prompt for a tag name and assign it to all selected assets."),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda(
										[this, SelectedCopy]()
										{
											this->ExecuteBulkAddTag(SelectedCopy);
										})));
							}

							MenuBuilder.EndSection();
						}));

				return Extender;
			}));

	MenuExtensionDelegateHandle = AssetMenuExtenders.Last().GetHandle();

	UE_LOG(LogBlenderAssetBrowserEditor, Log, TEXT("Content Browser menu extension registered."));
}

void FContentBrowserMenuExtension::Unregister()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser"))) { return; }

	FContentBrowserModule& CB = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	auto& Extenders = CB.GetAllAssetViewContextMenuExtenders();
	Extenders.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& E)
	{
		return E.GetHandle() == MenuExtensionDelegateHandle;
	});
}

bool FContentBrowserMenuExtension::IsAssetSupported(const FAssetData& Asset) const
{
	if (!Asset.IsValid()) { return false; }
	const FName ClassName = Asset.AssetClassPath.GetAssetName();
	return GetSupportedClasses().Contains(ClassName);
}

void FContentBrowserMenuExtension::ExecuteMarkAsAsset(TArray<FAssetData> SelectedAssets)
{
	UAssetLibrarySubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->IsReady())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("SubsystemNotReady",
				"Asset library is not ready. Check the Output Log for initialization errors."));
		return;
	}

	int32 NumAdded = 0;
	int32 NumSkipped = 0;
	int32 NumFailed = 0;

	for (const FAssetData& A : SelectedAssets)
	{
		if (!IsAssetSupported(A)) { ++NumSkipped; continue; }
		FAssetEntry Entry = BuildEntryFromAssetData(A);

		FString Err;
		if (!Entry.Validate(Err))
		{
			UE_LOG(LogBlenderAssetBrowserEditor, Warning,
				TEXT("Mark as Asset rejected for '%s': %s"), *Entry.AssetPath, *Err);
			++NumFailed;
			continue;
		}

		const int64 RowId = Sub->AddAsset(Entry);
		if (RowId > 0) { ++NumAdded; }
		else { ++NumFailed; }
	}

	UE_LOG(LogBlenderAssetBrowserEditor, Log,
		TEXT("Mark as Asset: added=%d skipped=%d failed=%d"),
		NumAdded, NumSkipped, NumFailed);

	const FText Msg = FText::FromString(FString::Printf(
		TEXT("Marked: %d\nSkipped (unsupported type): %d\nFailed: %d"),
		NumAdded, NumSkipped, NumFailed));
	FMessageDialog::Open(EAppMsgType::Ok, Msg);
}

void FContentBrowserMenuExtension::ExecuteUnmark(TArray<FAssetData> SelectedAssets)
{
	UAssetLibrarySubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->IsReady()) { return; }
	FAssetLibraryDatabase* Db = Sub->GetDatabase();
	if (!Db) { return; }

	int32 NumRemoved = 0;
	for (const FAssetData& A : SelectedAssets)
	{
		const FString Path = A.PackageName.ToString().Left(BAB::MAX_PATH_LEN);
		if (Path.IsEmpty()) { continue; }

		const bool bOk = Db->Execute(
			TEXT("DELETE FROM assets WHERE asset_path=? AND library_id IS NULL"),
			{ BAB::FBoundValue::MakeText(Path) });
		if (bOk) { ++NumRemoved; }
	}

	UE_LOG(LogBlenderAssetBrowserEditor, Log,
		TEXT("Unmark: removed=%d (or no match)"), NumRemoved);
}

void FContentBrowserMenuExtension::ExecuteAutoTag(TArray<FAssetData> SelectedAssets)
{
	UAssetLibrarySubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->IsReady())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("SubNotReady", "Asset library is not ready."));
		return;
	}
	if (!FSigLIPInference::IsReady() && !FSigLIPInference::EnsureRuntime())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("AINotReady",
				"SigLIP inference is not available. Check the Output Log for ONNX Runtime errors."));
		return;
	}

	FAssetLibraryDatabase* Db = Sub->GetDatabase();
	if (!Db) { return; }

	// Phase 1 (Game Thread): validate each selection, look up its DB id, render
	// its thumbnail. Thumbnail rendering requires RHI, so it MUST stay on the
	// Game Thread. Wrapped in FScopedSlowTask so the user sees progress and
	// can cancel.
	struct FPending
	{
		FAssetData     Data;
		int64          AssetId = 0;
		FString        Path;
		TArray<uint8>  Png;
	};

	TArray<FPending> Pending;
	int32 NumSkipped = 0;
	int32 NumFailed  = 0;

	{
		FScopedSlowTask Phase1(static_cast<float>(SelectedAssets.Num()),
			LOCTEXT("AutoTagPhase1", "AI auto-tag: preparing thumbnails..."));
		Phase1.MakeDialog(true /*bShowCancelButton*/);

		for (const FAssetData& A : SelectedAssets)
		{
			if (Phase1.ShouldCancel())
			{
				UE_LOG(LogBlenderAssetBrowserEditor, Log, TEXT("Auto-tag cancelled during phase 1."));
				break;
			}
			Phase1.EnterProgressFrame(1.0f, FText::Format(
				LOCTEXT("AutoTagRendering", "Rendering {0}"), FText::FromName(A.AssetName)));

			if (!IsAssetSupported(A)) { ++NumSkipped; continue; }

			const FString Path = A.PackageName.ToString().Left(BAB::MAX_PATH_LEN);
			if (Path.IsEmpty()) { ++NumSkipped; continue; }

			int64 AssetId = 0;
			Db->QueryRows(
				TEXT("SELECT id FROM assets WHERE asset_path=? AND library_id IS NULL"),
				{ BAB::FBoundValue::MakeText(Path) },
				[&AssetId](const BAB::FRow& Row) -> bool
				{
					AssetId = Row.GetInt64(0);
					return false;
				});
			if (AssetId <= 0)
			{
				UE_LOG(LogBlenderAssetBrowserEditor, Warning,
					TEXT("Auto-tag: asset '%s' is not marked — run 'Mark as Asset' first."), *Path);
				++NumSkipped;
				continue;
			}

			TArray<uint8> Png;
			if (!FAssetPreviewRenderer::RenderToBytes(A, 224, Png))
			{
				++NumFailed;
				continue;
			}

			FPending P;
			P.Data    = A;
			P.AssetId = AssetId;
			P.Path    = Path;
			P.Png     = MoveTemp(Png);
			Pending.Add(MoveTemp(P));
		}
	}

	if (Pending.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
			TEXT("AI auto-tag: nothing to do.\n  Skipped (unsupported/unmarked): %d\n  Failed: %d"),
			NumSkipped, NumFailed)));
		return;
	}

	// Phase 2: per-asset inference on the thread pool (frees the Game Thread
	// from running ORT). The Game Thread still waits on each future via Get(),
	// but the SlowTask pumps Slate between iterations so the editor stays
	// responsive — progress updates and cancel work between assets.
	int32 NumProcessed = 0;
	TArray<TPair<int64, TArray<TPair<FString, float>>>> ReviewQueue;

	const UAssetLibrarySettings* Settings = GetDefault<UAssetLibrarySettings>();
	const float Threshold    = Settings ? Settings->AITagConfidenceThreshold : 0.30f;
	const bool  bReviewFirst = Settings ? Settings->bAITagReviewBeforeApply  : false;
	constexpr int32 MaxTagsPerAsset = 10;

	{
		FScopedSlowTask Phase2(static_cast<float>(Pending.Num()),
			LOCTEXT("AutoTagPhase2", "AI auto-tag: running inference..."));
		Phase2.MakeDialog(true /*bShowCancelButton*/);

		for (FPending& P : Pending)
		{
			if (Phase2.ShouldCancel())
			{
				UE_LOG(LogBlenderAssetBrowserEditor, Log, TEXT("Auto-tag cancelled during phase 2."));
				break;
			}
			Phase2.EnterProgressFrame(1.0f, FText::Format(
				LOCTEXT("AutoTagInferring", "Inferring {0}"), FText::FromName(P.Data.AssetName)));

			// Move the PNG bytes onto the worker — they're not needed on GT
			// after this point.
			TArray<uint8> PngForWorker = MoveTemp(P.Png);
			TFuture<TArray<float>> Future = Async(EAsyncExecution::ThreadPool,
				[Png = MoveTemp(PngForWorker)]() -> TArray<float>
				{
					return FSigLIPInference::ComputeEmbedding(Png);
				});

			// Blocking wait. The Game Thread parks while ORT runs on the
			// worker. The SlowTask will pump Slate on the next EnterProgressFrame.
			const TArray<float> Embedding = Future.Get();
			if (Embedding.Num() != FSigLIPInference::EmbeddingDim)
			{
				UE_LOG(LogBlenderAssetBrowserEditor, Warning,
					TEXT("Auto-tag: embedding for '%s' had wrong dim %d."),
					*P.Path, Embedding.Num());
				++NumFailed;
				continue;
			}

			TArray<uint8> Blob;
			Blob.SetNumUninitialized(Embedding.Num() * sizeof(float));
			FMemory::Memcpy(Blob.GetData(), Embedding.GetData(), Blob.Num());

			const bool bOk = Db->Execute(
				TEXT("INSERT INTO asset_embeddings (asset_id, model_id, vector_dim, vector_blob) "
				     "VALUES (?, ?, ?, ?) "
				     "ON CONFLICT(asset_id) DO UPDATE SET "
				     "  model_id=excluded.model_id, vector_dim=excluded.vector_dim, "
				     "  vector_blob=excluded.vector_blob, computed_at=CURRENT_TIMESTAMP"),
				{
					BAB::FBoundValue::MakeInt(P.AssetId),
					BAB::FBoundValue::MakeText(TEXT("siglip2-base-patch16-224")),
					BAB::FBoundValue::MakeInt(FSigLIPInference::EmbeddingDim),
					BAB::FBoundValue::MakeBlob(Blob)
				});
			if (!bOk) { ++NumFailed; continue; }
			++NumProcessed;

			// --- Tag suggestion via cosine similarity against pre-computed vocab. ---
			if (!FTagVocabulary::IsEmbeddingsReady()) { continue; }
			const TArray<FString>& Vocab    = FTagVocabulary::GetBuiltinTags();
			const TArray<float>&   VocabEmb = FTagVocabulary::GetEmbeddingsFlat();
			const int32            Dim      = FTagVocabulary::GetEmbeddingDim();
			if (Vocab.Num() <= 0 || VocabEmb.Num() != Vocab.Num() * Dim) { continue; }

			TArray<FSigLIPInference::FScored> Scored =
				FSigLIPInference::ScoreAgainst(Embedding, VocabEmb, Vocab.Num());

			if (bReviewFirst)
			{
				TArray<TPair<FString, float>> Suggestions;
				int32 Taken = 0;
				for (const FSigLIPInference::FScored& S : Scored)
				{
					if (Taken >= MaxTagsPerAsset) { break; }
					if (S.Score < Threshold) { break; }
					if (!Vocab.IsValidIndex(S.TagIndex)) { continue; }
					Suggestions.Add(TPair<FString, float>(Vocab[S.TagIndex], S.Score));
					++Taken;
				}
				if (Suggestions.Num() > 0)
				{
					// Defer the modal until after the slow task closes — opening
					// a modal while another modal (the slow-task dialog) is up
					// is fragile.
					ReviewQueue.Add(TPair<int64, TArray<TPair<FString, float>>>(P.AssetId, MoveTemp(Suggestions)));
				}
				UE_LOG(LogBlenderAssetBrowserEditor, Log,
					TEXT("AI: %s -> queued for review"), *P.Path);
				continue;
			}

			int32 Assigned = 0;
			for (const FSigLIPInference::FScored& S : Scored)
			{
				if (Assigned >= MaxTagsPerAsset) { break; }
				if (S.Score < Threshold) { break; }
				if (!Vocab.IsValidIndex(S.TagIndex)) { continue; }
				FTagEntry TE; TE.Name = Vocab[S.TagIndex];
				const int64 TagId = Sub->AddTag(TE);
				if (TagId > 0 && Sub->AssignTag(P.AssetId, TagId, TEXT("ai"), S.Score))
				{
					++Assigned;
				}
			}
			UE_LOG(LogBlenderAssetBrowserEditor, Log,
				TEXT("AI: %s -> %d tag(s) above %.2f"), *P.Path, Assigned, Threshold);
		}
	}

	// Open any deferred review modals AFTER the slow-task progress dialog has
	// closed — stacking modals on top of FScopedSlowTask leads to focus chaos.
	for (auto& Item : ReviewQueue)
	{
		SAITagReviewWindow::ShowModal(Item.Key, Item.Value);
	}

	UE_LOG(LogBlenderAssetBrowserEditor, Log,
		TEXT("Auto-tag: processed=%d skipped=%d failed=%d review=%d"),
		NumProcessed, NumSkipped, NumFailed, ReviewQueue.Num());

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
		TEXT("AI auto-tag complete:\n  Embeddings stored: %d\n  Skipped (unsupported/unmarked): %d\n  Failed: %d\n  Sent to review: %d"),
		NumProcessed, NumSkipped, NumFailed, ReviewQueue.Num())));
}

void FContentBrowserMenuExtension::ExecuteBulkAddTag(TArray<FAssetData> SelectedAssets)
{
	UAssetLibrarySubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->IsReady()) { return; }
	FAssetLibraryDatabase* Db = Sub->GetDatabase();
	if (!Db) { return; }

	// Bug #7 fix: keep a Slate handle on the entry box so Enter, Apply, and
	// even normal window-close (X / Esc-via-FSlateApplication) all read the
	// committed text. Previously only the Apply button captured input.
	TSharedPtr<SEditableTextBox> Entry;
	TSharedRef<SWindow> Dlg = SNew(SWindow)
		.Title(LOCTEXT("BulkAddTagTitle", "Add Tag to Selected"))
		.ClientSize(FVector2D(380, 110))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	bool bConfirmed = false;
	FString TagText;

	auto CommitAndClose = [&bConfirmed, &TagText, &Entry, DlgWeak = TWeakPtr<SWindow>(Dlg)]()
	{
		if (Entry.IsValid())
		{
			TagText = Entry->GetText().ToString();
			TagText.TrimStartAndEndInline();
		}
		bConfirmed = !TagText.IsEmpty();
		if (TSharedPtr<SWindow> D = DlgWeak.Pin()) { D->RequestDestroyWindow(); }
	};

	Dlg->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
		.Padding(8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EnterTag", "Tag name (alnum, -, _, /, space; <= 128 chars). Enter to apply."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SAssignNew(Entry, SEditableTextBox)
				.OnTextCommitted_Lambda([CommitAndClose](const FText&, ETextCommit::Type T) {
					// Enter applies, focus-lose / Esc only closes (no apply).
					if (T == ETextCommit::OnEnter) { CommitAndClose(); }
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SButton)
					.Text(LOCTEXT("Apply", "Apply"))
					.OnClicked_Lambda([CommitAndClose]() {
						CommitAndClose();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked_Lambda([DlgWeak = TWeakPtr<SWindow>(Dlg)]() {
						if (TSharedPtr<SWindow> D = DlgWeak.Pin()) { D->RequestDestroyWindow(); }
						return FReply::Handled();
					})
				]
			]
		]);

	GEditor->EditorAddModalWindow(Dlg);
	if (!bConfirmed) { return; }

	// Sanitize tag name (SanitizeName helper handles length + char whitelist).
	const FString Clean = BAB::SanitizeName(TagText, BAB::MAX_TAG_LEN);
	if (Clean.IsEmpty()) { return; }

	// Ensure the tag exists; capture its id.
	FTagEntry Tag;
	Tag.Name = Clean;
	const int64 TagId = Sub->AddTag(Tag);
	if (TagId <= 0) { return; }

	int32 NumAssigned = 0;
	for (const FAssetData& A : SelectedAssets)
	{
		const FString Path = A.PackageName.ToString().Left(BAB::MAX_PATH_LEN);
		if (Path.IsEmpty()) { continue; }
		// Lookup asset id.
		int64 AssetId = 0;
		Db->QueryRows(
			TEXT("SELECT id FROM assets WHERE asset_path=? AND library_id IS NULL"),
			{ BAB::FBoundValue::MakeText(Path) },
			[&AssetId](const BAB::FRow& R) -> bool {
				AssetId = R.GetInt64(0);
				return false;
			});
		if (AssetId <= 0) { continue; }
		if (Sub->AssignTag(AssetId, TagId)) { ++NumAssigned; }
	}
	UE_LOG(LogBlenderAssetBrowserEditor, Log,
		TEXT("Bulk tag '%s' applied to %d asset(s)."), *Clean, NumAssigned);
}

void FContentBrowserMenuExtension::ExecuteBulkSetRating(TArray<FAssetData> SelectedAssets, int32 Rating)
{
	UAssetLibrarySubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->IsReady()) { return; }
	FAssetLibraryDatabase* Db = Sub->GetDatabase();
	if (!Db) { return; }

	const int32 Clamped = FMath::Clamp(Rating, 0, 5);
	int32 NumUpdated = 0;
	for (const FAssetData& A : SelectedAssets)
	{
		const FString Path = A.PackageName.ToString().Left(BAB::MAX_PATH_LEN);
		if (Path.IsEmpty()) { continue; }
		const bool bOk = Db->Execute(
			TEXT("UPDATE assets SET rating=?, modified_at=CURRENT_TIMESTAMP "
			     "WHERE asset_path=? AND library_id IS NULL"),
			{ BAB::FBoundValue::MakeInt(Clamped), BAB::FBoundValue::MakeText(Path) });
		if (bOk) { ++NumUpdated; }
	}
	UE_LOG(LogBlenderAssetBrowserEditor, Log,
		TEXT("Bulk rating set to %d on %d asset(s)."), Clamped, NumUpdated);
}

