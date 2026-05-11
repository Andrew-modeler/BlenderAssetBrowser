// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AssetPreviewModule.h"
#include "PreviewCacheManager.h"

DEFINE_LOG_CATEGORY(LogAssetPreview);

#define LOCTEXT_NAMESPACE "FAssetPreviewModule"

void FAssetPreviewModule::StartupModule()
{
	FPreviewCacheManager::Init();
	UE_LOG(LogAssetPreview, Log, TEXT("AssetPreview module started."));
}

void FAssetPreviewModule::ShutdownModule()
{
	UE_LOG(LogAssetPreview, Log, TEXT("AssetPreview module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetPreviewModule, AssetPreview)
