// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

using UnrealBuildTool;
using System.IO;

public class AssetPreview : ModuleRules
{
	public AssetPreview(ReadOnlyTargetRules Target) : base(Target)
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
			"RenderCore",
			"RHI",
			"AdvancedPreviewScene",
			"InputCore",
			"AssetRegistry",
			"ImageWrapper",
			"ImageWriteQueue",
			"MeshDescription",
			"StaticMeshDescription",
			"MeshConversion"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public")
		});

		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private")
		});

		// Assimp — only enabled if dev libs were dropped into ThirdParty/assimp/.
		string AssimpInc = Path.Combine(PluginDirectory, "ThirdParty", "assimp", "include");
		string AssimpLib = Path.Combine(PluginDirectory, "ThirdParty", "assimp", "lib", "assimp-vc143-mt.lib");
		string AssimpDll = Path.Combine(PluginDirectory, "ThirdParty", "assimp", "bin", "assimp-vc143-mt.dll");

		if (Target.Platform == UnrealTargetPlatform.Win64 &&
		    File.Exists(AssimpLib) && File.Exists(AssimpDll) && Directory.Exists(AssimpInc))
		{
			PublicIncludePaths.Add(AssimpInc);
			PublicAdditionalLibraries.Add(AssimpLib);
			RuntimeDependencies.Add("$(BinaryOutputDir)/assimp-vc143-mt.dll", AssimpDll);
			PublicDelayLoadDLLs.Add("assimp-vc143-mt.dll");
			PublicDefinitions.Add("BAB_HAS_ASSIMP=1");
		}
		else
		{
			PublicDefinitions.Add("BAB_HAS_ASSIMP=0");
		}

		// Filament is intentionally not used: the External FBX preview pipeline
		// routes through a transient UStaticMesh + UE ThumbnailTools, which is
		// simpler and produces visually consistent thumbnails.
		PublicDefinitions.Add("BAB_HAS_FILAMENT=0");
	}
}
