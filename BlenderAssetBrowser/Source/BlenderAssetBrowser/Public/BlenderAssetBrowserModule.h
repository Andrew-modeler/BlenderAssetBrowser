// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBlenderAssetBrowser, Log, All);

class FBlenderAssetBrowserModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FBlenderAssetBrowserModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FBlenderAssetBrowserModule>("BlenderAssetBrowser");
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("BlenderAssetBrowser");
	}
};
