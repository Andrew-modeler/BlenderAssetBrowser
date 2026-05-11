// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Project-level settings for the plugin. Lives in Project Settings under
// "Plugins / Blender Asset Browser". All paths are validated on commit.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"

#include "AssetLibrarySettings.generated.h"

USTRUCT()
struct FExternalLibraryConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Library")
	FString Name;

	UPROPERTY(EditAnywhere, Category = "Library", meta = (ContentDir))
	FDirectoryPath Path;

	UPROPERTY(EditAnywhere, Category = "Library")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, Category = "Library")
	bool bVisible = true;
};

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "Blender Asset Browser"))
class BLENDERASSETBROWSER_API UAssetLibrarySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAssetLibrarySettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** External library folders. Cross-project / shared sources. */
	UPROPERTY(Config, EditAnywhere, Category = "Libraries")
	TArray<FExternalLibraryConfig> Libraries;

	/** Path to Blender executable for the bridge feature. Auto-detected if empty. */
	UPROPERTY(Config, EditAnywhere, Category = "Blender Bridge", meta = (FilePathFilter = "exe"))
	FFilePath BlenderExecutable;

	/** Substance Painter executable. Empty disables the integration. */
	UPROPERTY(Config, EditAnywhere, Category = "External DCC", meta = (FilePathFilter = "exe"))
	FFilePath SubstancePainterExecutable;

	/** Adobe Photoshop executable. Empty disables the integration. */
	UPROPERTY(Config, EditAnywhere, Category = "External DCC", meta = (FilePathFilter = "exe"))
	FFilePath PhotoshopExecutable;

	/** 3ds Max executable. Empty disables the integration. */
	UPROPERTY(Config, EditAnywhere, Category = "External DCC", meta = (FilePathFilter = "exe"))
	FFilePath MaxExecutable;

	/** Folder used to exchange files with Blender. Must be writable. */
	UPROPERTY(Config, EditAnywhere, Category = "Blender Bridge")
	FString ExchangeSubfolder = TEXT("BlenderAssetBrowser/Exchange");

	/** Default thumbnail size in the asset grid. */
	UPROPERTY(Config, EditAnywhere, Category = "UI", meta = (ClampMin = "32", ClampMax = "512"))
	int32 ThumbnailSizeDefault = 128;

	/** Compute AI tags automatically when an asset is marked. */
	UPROPERTY(Config, EditAnywhere, Category = "AI Tagging")
	bool bEnableAutoTagging = false;

	/** Confidence threshold for AI-suggested tags (0.0-1.0, sigmoid-calibrated). */
	UPROPERTY(Config, EditAnywhere, Category = "AI Tagging", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AITagConfidenceThreshold = 0.3f;

	/** Sigmoid scale for SigLIP cosine→probability (SigLIP defaults ≈ 4-5). */
	UPROPERTY(Config, EditAnywhere, Category = "AI Tagging", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float AISigmoidScale = 4.0f;

	/** Show a review dialog after AI tagging instead of auto-assigning. */
	UPROPERTY(Config, EditAnywhere, Category = "AI Tagging")
	bool bAITagReviewBeforeApply = false;

	/** Include the asset_embeddings index in `.assetlib` exports. */
	UPROPERTY(Config, EditAnywhere, Category = "VCS")
	bool bExportEmbeddingsInSidecar = false;

	/** Daily Fab update check (HTTPS scrape). */
	UPROPERTY(Config, EditAnywhere, Category = "Update Checking")
	bool bEnableFabUpdateCheck = false;

	/** Discord webhook URL — must start with https://discord.com/api/webhooks/. */
	UPROPERTY(Config, EditAnywhere, Category = "Update Checking")
	FString DiscordWebhookUrl;

	/** UI theme name (registered FSlateStyleSet). */
	UPROPERTY(Config, EditAnywhere, Category = "UI")
	FString Theme = TEXT("Default");

	/** Quick Picker hotkey. Default Ctrl+Shift+Space. Empty key disables binding. */
	UPROPERTY(Config, EditAnywhere, Category = "Hotkeys")
	FKey QuickPickerKey = EKeys::SpaceBar;

	UPROPERTY(Config, EditAnywhere, Category = "Hotkeys")
	bool bQuickPickerCtrl = true;

	UPROPERTY(Config, EditAnywhere, Category = "Hotkeys")
	bool bQuickPickerShift = true;

	UPROPERTY(Config, EditAnywhere, Category = "Hotkeys")
	bool bQuickPickerAlt = false;

	/** Warn when mounting a library whose engine_version is newer than the running editor. */
	UPROPERTY(Config, EditAnywhere, Category = "Libraries")
	bool bWarnOnNewerEngineVersion = true;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
