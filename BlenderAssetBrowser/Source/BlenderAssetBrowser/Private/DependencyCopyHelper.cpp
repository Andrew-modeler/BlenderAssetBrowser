// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "DependencyCopyHelper.h"
#include "AssetLibraryTypes.h"
#include "BlenderAssetBrowserModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

namespace
{
	constexpr int32 MAX_DEPS = 10000;

	bool IsSafePackagePath(const FString& P)
	{
		if (P.IsEmpty() || P.Len() > BAB::MAX_PATH_LEN) { return false; }
		if (P.Contains(TEXT(".."))) { return false; }
		return P.StartsWith(TEXT("/Game/")) || P.StartsWith(TEXT("/AssetLibraries/"));
	}
}

FDependencyClosure FDependencyCopyHelper::Compute(const FString& RootPackagePath)
{
	FDependencyClosure Out;
	if (!IsSafePackagePath(RootPackagePath))
	{
		UE_LOG(LogBlenderAssetBrowser, Warning,
			TEXT("DependencyClosure: refused unsafe root '%s'."), *RootPackagePath);
		return Out;
	}
	Out.RootPackagePath = RootPackagePath;

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry  = ARM.Get();

	TSet<FName>   Visited;
	TArray<FName> Frontier;
	Frontier.Add(*RootPackagePath);
	Visited.Add(*RootPackagePath);

	while (Frontier.Num() > 0 && Visited.Num() < MAX_DEPS)
	{
		const FName Cur = Frontier.Pop(EAllowShrinking::No);
		TArray<FName> Deps;
		Registry.GetDependencies(Cur, Deps, UE::AssetRegistry::EDependencyCategory::Package);
		for (const FName& D : Deps)
		{
			const FString DStr = D.ToString();
			// Only follow into /Game and /AssetLibraries; skip /Engine, /Script, /Memory.
			if (!IsSafePackagePath(DStr)) { continue; }
			bool bAlready = false;
			Visited.Add(D, &bAlready);
			if (!bAlready) { Frontier.Add(D); }
		}
	}

	if (Visited.Num() >= MAX_DEPS)
	{
		UE_LOG(LogBlenderAssetBrowser, Warning,
			TEXT("DependencyClosure: hit MAX_DEPS cap of %d. Result truncated."), MAX_DEPS);
	}

	Out.AllPackages.Reserve(Visited.Num());
	for (const FName& N : Visited) { Out.AllPackages.Add(N.ToString()); }

	// Bug #5 fix: real file sizes. The previous code multiplied path-string
	// length by 100, which had no relation to actual disk usage. We resolve
	// the package's .uasset path via FPackageName and stat it.
	IFileManager& FM = IFileManager::Get();
	for (const FString& P : Out.AllPackages)
	{
		FString DiskPath;
		if (FPackageName::TryConvertLongPackageNameToFilename(P, DiskPath, FPackageName::GetAssetPackageExtension()))
		{
			const int64 Sz = FM.FileSize(*DiskPath);
			if (Sz > 0) { Out.TotalDiskBytes += Sz; }
		}
	}

	UE_LOG(LogBlenderAssetBrowser, Log,
		TEXT("DependencyClosure: '%s' -> %d package(s), ~%lld bytes."),
		*RootPackagePath, Out.AllPackages.Num(), Out.TotalDiskBytes);
	return Out;
}

TArray<FString> FDependencyCopyHelper::CopyClosure(const FDependencyClosure& Closure,
	const FString& DestPath)
{
	TArray<FString> Created;
	if (!IsSafePackagePath(DestPath) || Closure.AllPackages.Num() == 0) { return Created; }

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry  = ARM.Get();

	for (const FString& Pkg : Closure.AllPackages)
	{
		TArray<FAssetData> Datas;
		Registry.GetAssetsByPackageName(*Pkg, Datas);
		for (const FAssetData& D : Datas)
		{
			UObject* Src = D.GetAsset();
			if (!Src) { continue; }
			const FString TargetName = D.AssetName.ToString();
			const FString TargetPkg  = DestPath + TEXT("/") + TargetName;
			UObject* Dup = AssetTools.DuplicateAsset(TargetName, DestPath, Src);
			if (Dup)
			{
				Created.Add(TargetPkg);
			}
		}
	}
	UE_LOG(LogBlenderAssetBrowser, Log,
		TEXT("CopyClosure: duplicated %d asset(s) into '%s'."),
		Created.Num(), *DestPath);
	return Created;
}

TArray<FString> FDependencyCopyHelper::CopySingle(const FString& PackagePath, const FString& DestPath)
{
	FDependencyClosure Single;
	Single.RootPackagePath = PackagePath;
	Single.AllPackages.Add(PackagePath);
	return CopyClosure(Single, DestPath);
}
