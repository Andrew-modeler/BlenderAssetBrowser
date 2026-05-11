// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "BlenderBridgeModule.h"
#include "BlenderBridgeManager.h"
#include "DCCReimportWatcher.h"
#include "DCCLauncher.h"

DEFINE_LOG_CATEGORY(LogBlenderBridge);

#define LOCTEXT_NAMESPACE "FBlenderBridgeModule"

namespace
{
	TUniquePtr<FBlenderBridgeManager> GManager;
	TUniquePtr<FDCCReimportWatcher>   GWatcher;
}

void FBlenderBridgeModule::StartupModule()
{
	GManager = MakeUnique<FBlenderBridgeManager>();
	GManager->Initialize();
	GWatcher = MakeUnique<FDCCReimportWatcher>();
	GWatcher->Initialize();
	// Bug #3 fix part 2: publish the watcher so DCCLauncher can register
	// files when the user launches Substance/Photoshop/Max.
	FDCCLauncher::SetGlobalWatcher(GWatcher.Get());
	UE_LOG(LogBlenderBridge, Log, TEXT("BlenderBridge module started (Blender + DCC reimport watcher live)."));
}

void FBlenderBridgeModule::ShutdownModule()
{
	FDCCLauncher::SetGlobalWatcher(nullptr);
	if (GWatcher.IsValid())
	{
		GWatcher->Shutdown();
		GWatcher.Reset();
	}
	if (GManager.IsValid())
	{
		GManager->Shutdown();
		GManager.Reset();
	}
	UE_LOG(LogBlenderBridge, Log, TEXT("BlenderBridge module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlenderBridgeModule, BlenderBridge)
