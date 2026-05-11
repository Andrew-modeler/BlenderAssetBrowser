// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Static analyzer for asset names. Configurable per-type prefix rules; reports
// every asset whose name doesn't match. Read-only by default — batch rename
// is a separate explicit action (see ApplyFix).

#pragma once

#include "CoreMinimal.h"
#include "AssetLibraryTypes.h"

struct BLENDERASSETBROWSER_API FNamingRule
{
	FString AssetType;     // e.g. "StaticMesh"
	FString Prefix;        // e.g. "SM_"
	FString Suffix;        // optional
};

struct BLENDERASSETBROWSER_API FNamingViolation
{
	int64   AssetId = 0;
	FString AssetName;
	FString AssetType;
	FString ExpectedPrefix;
	FString SuggestedName;   // current + prefix applied (no regex magic, just prepend)
};

class UAssetLibrarySubsystem;

class BLENDERASSETBROWSER_API FNamingLinter
{
public:
	/** Default rules baked in. Override via Settings later. */
	static TArray<FNamingRule> GetDefaultRules();

	/** Scan the library for violations. Returns list (possibly empty). */
	static TArray<FNamingViolation> Scan(UAssetLibrarySubsystem* Subsystem,
	                                     const TArray<FNamingRule>* Rules = nullptr);

	/** Suggest a corrected name. Pure function, no DB write. */
	static FString SuggestRename(const FString& CurrentName, const FNamingRule& Rule);
};
