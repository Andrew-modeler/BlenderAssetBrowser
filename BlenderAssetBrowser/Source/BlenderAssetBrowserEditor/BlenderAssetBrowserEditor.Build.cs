// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

using UnrealBuildTool;
using System.IO;

public class BlenderAssetBrowserEditor : ModuleRules
{
	public BlenderAssetBrowserEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"BlenderAssetBrowser",
			"AssetPreview",
			"AITagging",
			"UpdateChecker"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
			"InputCore",
			"AssetRegistry",
			"AssetTools",
			"ContentBrowser",
			"ContentBrowserData",
			"ToolMenus",
			"EditorFramework",
			"EditorStyle",
			"EditorWidgets",
			"DesktopPlatform",
			"PropertyEditor",
			"WorkspaceMenuStructure",
			"DeveloperSettings",
			"ApplicationCore",
			"Projects",
			"Json",
			"JsonUtilities",
			"LevelEditor",
			"ViewportInteraction"
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
