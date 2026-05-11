// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Daily HTTPS scrape of Fab marketplace pages to compare the version we
// recorded at import time against what's currently published.
//
// SECURITY:
//   - HTTPS-only URLs (validated by FAssetEntry::Validate at insert time).
//   - HTTP body capped to 2 MB.
//   - 10s request timeout.
//   - User-Agent identifies the plugin (transparency).
//   - No execution of any response content. We parse with a tiny stateful
//     regex-equivalent string scanner; we never feed the body to a JS/HTML
//     engine. Unknown / changed page structure logs a warning and marks
//     the asset's update_state as "unknown" instead of crashing.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class UAssetLibrarySubsystem;

class FFabUpdateChecker
{
public:
	void Initialize();
	void Shutdown();

	/** Run a check pass right now (skipping the daily cooldown). */
	void CheckNow();

private:
	bool TickDaily(float DeltaTime);
	void EnqueueOne(int64 AssetId, const FString& Url, const FString& KnownVersion);

	FTSTicker::FDelegateHandle TickerHandle;
	double                     LastCheckedSeconds = 0.0;
	int32                      InFlight           = 0;
};
