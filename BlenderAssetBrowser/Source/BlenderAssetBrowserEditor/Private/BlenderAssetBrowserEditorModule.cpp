// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "BlenderAssetBrowserEditorModule.h"
#include "ContentBrowserMenuExtension.h"
#include "AssetBrowserCommands.h"
#include "SAssetBrowserWindow.h"
#include "SQuickPicker.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/InputChord.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "LevelEditor.h"

DEFINE_LOG_CATEGORY(LogBlenderAssetBrowserEditor);

#define LOCTEXT_NAMESPACE "FBlenderAssetBrowserEditorModule"

const FName FBlenderAssetBrowserEditorModule::AssetBrowserTabName = TEXT("BlenderAssetBrowserTab");

void FBlenderAssetBrowserEditorModule::StartupModule()
{
	UE_LOG(LogBlenderAssetBrowserEditor, Log, TEXT("BlenderAssetBrowserEditor module started."));
	RegisterTabSpawners();
	BindCommands();

	ContentBrowserExtension = MakeUnique<FContentBrowserMenuExtension>();
	ContentBrowserExtension->Register();
}

void FBlenderAssetBrowserEditorModule::ShutdownModule()
{
	if (ContentBrowserExtension.IsValid())
	{
		ContentBrowserExtension->Unregister();
		ContentBrowserExtension.Reset();
	}
	UnbindCommands();
	UnregisterTabSpawners();
	UE_LOG(LogBlenderAssetBrowserEditor, Log, TEXT("BlenderAssetBrowserEditor module shut down."));
}

void FBlenderAssetBrowserEditorModule::BindCommands()
{
	FAssetBrowserCommands::Register();
	CommandList = MakeShared<FUICommandList>();

	CommandList->MapAction(
		FAssetBrowserCommands::Get().OpenAssetBrowser,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(AssetBrowserTabName);
		}),
		FCanExecuteAction());

	CommandList->MapAction(
		FAssetBrowserCommands::Get().OpenQuickPicker,
		FExecuteAction::CreateStatic(&SQuickPicker::ShowOverlay),
		FCanExecuteAction());

	// Append our chord-handling list to the Level Editor so the hotkey works
	// even when no plugin widget has focus.
	if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		FLevelEditorModule& LE = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LE.GetGlobalLevelEditorActions()->Append(CommandList.ToSharedRef());
		UE_LOG(LogBlenderAssetBrowserEditor, Log,
			TEXT("Hotkeys appended to Level Editor (Ctrl+Shift+Space = Quick Picker)."));
	}
}

void FBlenderAssetBrowserEditorModule::UnbindCommands()
{
	CommandList.Reset();
	if (FAssetBrowserCommands::IsRegistered())
	{
		FAssetBrowserCommands::Unregister();
	}
}

void FBlenderAssetBrowserEditorModule::RegisterTabSpawners()
{
	const TSharedRef<FWorkspaceItem> MenuRoot = WorkspaceMenu::GetMenuStructure().GetToolsCategory();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		AssetBrowserTabName,
		FOnSpawnTab::CreateRaw(this, &FBlenderAssetBrowserEditorModule::SpawnAssetBrowserTab))
		.SetDisplayName(LOCTEXT("AssetBrowserTabTitle", "Blender Asset Browser"))
		.SetTooltipText(LOCTEXT("AssetBrowserTabTooltip", "Open the centralized asset library browser."))
		.SetGroup(MenuRoot)
		.SetIcon(FSlateIcon());
}

void FBlenderAssetBrowserEditorModule::UnregisterTabSpawners()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(AssetBrowserTabName))
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AssetBrowserTabName);
	}
}

TSharedRef<SDockTab> FBlenderAssetBrowserEditorModule::SpawnAssetBrowserTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SAssetBrowserWindow)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlenderAssetBrowserEditorModule, BlenderAssetBrowserEditor)
