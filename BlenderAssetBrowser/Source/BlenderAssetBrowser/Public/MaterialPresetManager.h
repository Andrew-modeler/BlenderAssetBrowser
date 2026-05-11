// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Material presets — capture the parameter delta of a MaterialInstanceConstant
// over its parent master material into a tiny JSON blob. Replays it onto a
// new MIC in another project.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "AssetLibraryTypes.h"

class UMaterialInstanceConstant;
class UMaterialInterface;
class UAssetLibrarySubsystem;

struct BLENDERASSETBROWSER_API FMaterialPreset
{
	int64   Id              = 0;
	int64   AssetId         = 0;   // FK into assets (the parent material)
	FString Name;
	FString ParentMaterialPath;    // /Game/.../M_Master
	FString ParametersJson;        // {"scalars":{"Roughness":0.4,...}, "vectors":{...}, "textures":{...}}
};

class BLENDERASSETBROWSER_API FMaterialPresetManager
{
public:
	explicit FMaterialPresetManager(UAssetLibrarySubsystem* InSubsystem);

	/** Capture parameter delta from an existing MIC. Returns the preset id, 0 on failure. */
	int64 CaptureFromMIC(UMaterialInstanceConstant* MIC, const FString& PresetName);

	/** List all presets in the library. */
	TArray<FMaterialPreset> GetAll();

	/** Delete by id. */
	bool DeletePreset(int64 PresetId);

private:
	TWeakObjectPtr<UAssetLibrarySubsystem> Subsystem;
};
