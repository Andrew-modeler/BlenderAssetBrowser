// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

using UnrealBuildTool;
using System.IO;

public class AITagging : ModuleRules
{
	public AITagging(ReadOnlyTargetRules Target) : base(Target)
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
			"Projects",
			"ImageWrapper",
			"RenderCore",
			"Json",
			"JsonUtilities"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public")
		});

		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private")
		});

		// ONNX Runtime — detected automatically. If the bundled dist is present
		// we link & ship the DLL; otherwise inference stays disabled at runtime
		// (FSigLIPInference::IsReady returns false).
		string OnnxDir   = Path.Combine(PluginDirectory, "ThirdParty", "onnxruntime",
		                                "extracted", "onnxruntime-win-x64-1.20.1");
		string OnnxInc   = Path.Combine(OnnxDir, "include");
		string OnnxLib   = Path.Combine(OnnxDir, "lib", "onnxruntime.lib");
		string OnnxDll   = Path.Combine(OnnxDir, "lib", "onnxruntime.dll");

		if (Target.Platform == UnrealTargetPlatform.Win64 &&
		    File.Exists(OnnxLib) && File.Exists(OnnxDll))
		{
			PublicIncludePaths.Add(OnnxInc);
			PublicAdditionalLibraries.Add(OnnxLib);
			RuntimeDependencies.Add("$(BinaryOutputDir)/onnxruntime.dll", OnnxDll);
			// No delay-load — Windows imports onnxruntime.dll alongside our DLL
			// at startup. This avoids the ORT C++ wrapper crashing when its
			// api_ pointer can't be resolved via the delay-load helper.
			PublicDefinitions.Add("BAB_HAS_ONNXRUNTIME=1");
		}
		else
		{
			PublicDefinitions.Add("BAB_HAS_ONNXRUNTIME=0");
		}
	}
}
