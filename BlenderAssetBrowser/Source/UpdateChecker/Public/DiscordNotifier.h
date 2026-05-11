// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Send a single-line message to a Discord webhook URL. No authentication,
// no batching, no retry — just fire-and-forget. Webhook URL is per-user
// (set in Project Settings → BlenderAssetBrowser → DiscordWebhookUrl).

#pragma once

#include "CoreMinimal.h"

class FDiscordNotifier
{
public:
	/** Posts `Message` to the configured webhook. Returns immediately;
	 *  the HTTP call runs async on UE's thread pool. */
	static void Post(const FString& Message);
};
