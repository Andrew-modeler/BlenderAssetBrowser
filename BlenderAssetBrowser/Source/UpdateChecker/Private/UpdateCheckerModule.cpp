// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "UpdateCheckerModule.h"
#include "FabUpdateChecker.h"

DEFINE_LOG_CATEGORY(LogUpdateChecker);

#define LOCTEXT_NAMESPACE "FUpdateCheckerModule"

namespace { TUniquePtr<FFabUpdateChecker> GFabChecker; }

void FUpdateCheckerModule::StartupModule()
{
	GFabChecker = MakeUnique<FFabUpdateChecker>();
	GFabChecker->Initialize();
	UE_LOG(LogUpdateChecker, Log, TEXT("UpdateChecker module started (Fab scraper enabled by user setting)."));
}

void FUpdateCheckerModule::ShutdownModule()
{
	if (GFabChecker.IsValid())
	{
		GFabChecker->Shutdown();
		GFabChecker.Reset();
	}
	UE_LOG(LogUpdateChecker, Log, TEXT("UpdateChecker module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUpdateCheckerModule, UpdateChecker)
