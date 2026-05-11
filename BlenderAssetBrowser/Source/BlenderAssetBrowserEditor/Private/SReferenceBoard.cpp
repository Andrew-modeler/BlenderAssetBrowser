// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SReferenceBoard.h"
#include "BlenderAssetBrowserEditorModule.h"
#include "AssetLibraryTypes.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "BABRefBoard"

namespace
{
	FString StorePath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())
			/ TEXT("BlenderAssetBrowser/references.json");
	}

	bool IsSafeRef(const FString& R)
	{
		if (R.IsEmpty() || R.Len() > 1024) { return false; }
		// Block control chars.
		for (TCHAR C : R) { if (C < 0x20) { return false; } }
		return true;
	}
}

void SReferenceBoard::Load()
{
	Refs.Reset();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *StorePath())) { return; }
	if (Json.Len() > BAB::MAX_FILE_BYTES_JSON) { return; }
	TSharedPtr<FJsonObject> Obj;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) { return; }
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj->TryGetArrayField(TEXT("refs"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const FString S = V->AsString();
			if (IsSafeRef(S)) { Refs.Add(S); }
		}
	}
}

void SReferenceBoard::Save()
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& R : Refs)
	{
		Arr.Add(MakeShared<FJsonValueString>(R));
	}
	Obj->SetArrayField(TEXT("refs"), Arr);
	FString Out;
	auto Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj, Writer);
	FFileHelper::SaveStringToFile(Out, *StorePath(),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void SReferenceBoard::Construct(const FArguments& InArgs)
{
	Load();
	ChildSlot
	[
		SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel"))).Padding(8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(2)
				[
					SAssignNew(InputBox, SEditableTextBox)
					.HintText(LOCTEXT("Hint", "Paste URL or path, then Enter / Add"))
					.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type T) {
						if (T == ETextCommit::OnEnter) { OnAddClicked(); }
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[ SNew(SButton).Text(LOCTEXT("Add", "Add"))
					.OnClicked(this, &SReferenceBoard::OnAddClicked) ]
			]
			+ SVerticalBox::Slot().FillHeight(1).Padding(2)
			[
				SNew(SScrollBox) + SScrollBox::Slot()
				[ SAssignNew(ListBox, SVerticalBox) ]
			]
		]
	];
	RebuildList();
}

void SReferenceBoard::RebuildList()
{
	if (!ListBox.IsValid()) { return; }
	ListBox->ClearChildren();
	for (int32 i = 0; i < Refs.Num(); ++i)
	{
		const int32 Idx = i;
		const FString& Val = Refs[i];
		ListBox->AddSlot().AutoHeight().Padding(2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).Padding(4, 2)
			[ SNew(STextBlock).Text(FText::FromString(Val)).AutoWrapText(true) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton).Text(LOCTEXT("Copy", "Copy"))
				.OnClicked_Lambda([Val]() {
					FPlatformApplicationMisc::ClipboardCopy(*Val);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton).Text(LOCTEXT("Del", "X"))
				.OnClicked_Lambda([this, Idx]() {
					if (Refs.IsValidIndex(Idx))
					{
						Refs.RemoveAt(Idx);
						Save();
						RebuildList();
					}
					return FReply::Handled();
				})
			]
		];
	}
}

FReply SReferenceBoard::OnAddClicked()
{
	if (!InputBox.IsValid()) { return FReply::Handled(); }
	FString Text = InputBox->GetText().ToString();
	Text.TrimStartAndEndInline();
	if (!IsSafeRef(Text)) { return FReply::Handled(); }
	Refs.Add(Text);
	Save();
	InputBox->SetText(FText::GetEmpty());
	RebuildList();
	return FReply::Handled();
}

void SReferenceBoard::ShowWindow()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "Reference Board"))
		.ClientSize(FVector2D(560, 600))
		.SupportsMaximize(true).SupportsMinimize(true);
	Window->SetContent(SNew(SReferenceBoard));
	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
