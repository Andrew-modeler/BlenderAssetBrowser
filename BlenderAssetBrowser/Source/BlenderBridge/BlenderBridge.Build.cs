// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

using UnrealBuildTool;
using System.IO;

public class BlenderBridge : ModuleRules
{
	public BlenderBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"BlenderAssetBrowser"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
			"AssetRegistry",
			"AssetTools",
			"DirectoryWatcher",
			"DesktopPlatform",
			"Projects",
			"Json",
			"JsonUtilities",
			"ApplicationCore",
			"DeveloperSettings"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public")
		});

		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private")
		});
	}
}
