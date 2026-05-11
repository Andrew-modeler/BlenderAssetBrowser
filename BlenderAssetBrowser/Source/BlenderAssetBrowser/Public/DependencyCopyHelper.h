// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Compute the transitive dependency closure of an asset, then copy the
// graph into a target package path atomically (no redirectors).
//
// SECURITY:
//   - All package names validated as `/Game/...` or `/AssetLibraries/...`
//     before any FS operation.
//   - Closure capped at MAX_DEPS to avoid pathological graphs (10000).
//   - We use UE's `IAssetTools::CopyAssets` which is itself careful about
//     reference fixup; we don't reinvent path rewriting.

#pragma once

#include "CoreMinimal.h"

struct BLENDERASSETBROWSER_API FDependencyClosure
{
	FString               RootPackagePath;
	TArray<FString>       AllPackages;    // root + all transitive deps
	int64                 TotalDiskBytes = 0;
};

class BLENDERASSETBROWSER_API FDependencyCopyHelper
{
public:
	/** Compute the transitive dependency closure starting from `RootPackagePath`. */
	static FDependencyClosure Compute(const FString& RootPackagePath);

	/** Copy the closure under `DestPath` (a folder like "/Game/Imported/Foo").
	 *  Returns the list of newly-created object paths. */
	static TArray<FString> CopyClosure(const FDependencyClosure& Closure, const FString& DestPath);

	/** Copy a single asset without its dependencies. */
	static TArray<FString> CopySingle(const FString& PackagePath, const FString& DestPath);
};
