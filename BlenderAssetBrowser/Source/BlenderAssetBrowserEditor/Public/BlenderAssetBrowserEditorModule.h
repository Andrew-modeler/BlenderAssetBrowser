// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBlenderAssetBrowserEditor, Log, All);

class FBlenderAssetBrowserEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FBlenderAssetBrowserEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FBlenderAssetBrowserEditorModule>("BlenderAssetBrowserEditor");
	}

private:
	void RegisterTabSpawners();
	void UnregisterTabSpawners();

	TSharedRef<class SDockTab> SpawnAssetBrowserTab(const class FSpawnTabArgs& Args);

	static const FName AssetBrowserTabName;

	TUniquePtr<class FContentBrowserMenuExtension> ContentBrowserExtension;

	TSharedPtr<class FUICommandList> CommandList;
	void BindCommands();
	void UnbindCommands();
};
