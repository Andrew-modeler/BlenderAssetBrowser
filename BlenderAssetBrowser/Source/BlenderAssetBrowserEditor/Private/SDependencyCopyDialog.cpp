// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SDependencyCopyDialog.h"
#include "BlenderAssetBrowserEditorModule.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "BABDepCopy"

namespace
{
	FString FmtBytes(int64 B)
	{
		if (B <= 0) { return TEXT("(unknown)"); }
		if (B < 1024) { return FString::Printf(TEXT("%lld B"), B); }
		if (B < 1024 * 1024) { return FString::Printf(TEXT("%.1f KB"), B / 1024.0); }
		if (B < 1024LL * 1024 * 1024) { return FString::Printf(TEXT("%.1f MB"), B / (1024.0 * 1024.0)); }
		return FString::Printf(TEXT("%.2f GB"), B / (1024.0 * 1024.0 * 1024.0));
	}
}

void SDependencyCopyDialog::Construct(const FArguments& InArgs, const FString& InRootPackage, const FString& InDestPath)
{
	Root    = InRootPackage;
	Dest    = InDestPath;
	Closure = FDependencyCopyHelper::Compute(Root);

	TSharedRef<SVerticalBox> DepList = SNew(SVerticalBox);
	for (const FString& P : Closure.AllPackages)
	{
		DepList->AddSlot().AutoHeight().Padding(2)
			[ SNew(STextBlock).Text(FText::FromString(P)) ];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
		.Padding(8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Copying: %s\nTo: %s\nClosure: %d package(s)  (~%s)"),
					*Root, *Dest, Closure.AllPackages.Num(), *FmtBytes(Closure.TotalDiskBytes))))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().FillHeight(1).Padding(4)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
				[
					SNew(SScrollBox) + SScrollBox::Slot()[ DepList ]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyWith", "Copy with dependencies"))
					.OnClicked_Lambda([this]() {
						Result = EChoice::CopyWithDeps;
						if (TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(AsShared()))
						{
							Win->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SButton)
					.Text(LOCTEXT("CopySingle", "Copy this asset only"))
					.OnClicked_Lambda([this]() {
						Result = EChoice::CopySingle;
						if (TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(AsShared()))
						{
							Win->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked_Lambda([this]() {
						Result = EChoice::Cancel;
						if (TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(AsShared()))
						{
							Win->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			]
		]
	];
}

SDependencyCopyDialog::EChoice SDependencyCopyDialog::ShowModal(const FString& RootPackage,
	const FString& DestPath, FDependencyClosure& OutClosure)
{
	TSharedPtr<SDependencyCopyDialog> Inner;
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "Copy with Dependencies"))
		.ClientSize(FVector2D(720, 480))
		.SupportsMaximize(true)
		.SupportsMinimize(false);

	Window->SetContent(SAssignNew(Inner, SDependencyCopyDialog, RootPackage, DestPath));
	GEditor->EditorAddModalWindow(Window);

	if (Inner.IsValid())
	{
		OutClosure = Inner->Closure;
		return Inner->Result;
	}
	return EChoice::Cancel;
}

#undef LOCTEXT_NAMESPACE
