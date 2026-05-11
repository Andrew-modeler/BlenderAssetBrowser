// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "BlenderAssetBrowserModule.h"

DEFINE_LOG_CATEGORY(LogBlenderAssetBrowser);

#define LOCTEXT_NAMESPACE "FBlenderAssetBrowserModule"

void FBlenderAssetBrowserModule::StartupModule()
{
	UE_LOG(LogBlenderAssetBrowser, Log, TEXT("BlenderAssetBrowser runtime module started."));
}

void FBlenderAssetBrowserModule::ShutdownModule()
{
	UE_LOG(LogBlenderAssetBrowser, Log, TEXT("BlenderAssetBrowser runtime module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlenderAssetBrowserModule, BlenderAssetBrowser)
