// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Minimal in-editor reference board: a docked panel where the user pastes
// URLs or filesystem paths, and the panel keeps the list in a JSON file
// under {ProjectSaved}/BlenderAssetBrowser/references.json.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;

class SReferenceBoard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SReferenceBoard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	static void ShowWindow();

private:
	void Load();
	void Save();
	FReply OnAddClicked();
	void RebuildList();

	TArray<FString>      Refs;
	TSharedPtr<SEditableTextBox> InputBox;
	TSharedPtr<class SVerticalBox> ListBox;
};
