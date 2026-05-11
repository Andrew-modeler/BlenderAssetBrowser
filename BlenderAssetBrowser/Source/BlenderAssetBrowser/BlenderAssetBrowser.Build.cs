// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
// See the LICENSE file at repo root.

using UnrealBuildTool;
using System.IO;

public class BlenderAssetBrowser : ModuleRules
{
	public BlenderAssetBrowser(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
			"EditorSubsystem",
			"EditorScriptingUtilities",
			"AssetRegistry",
			"AssetTools",
			"DeveloperSettings",
			"Json",
			"JsonUtilities",
			"Projects",
			"InputCore"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public")
		});

		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private")
		});

		// SQLite amalgamation lives under Private/sqlite. UE compiles its .c file
		// alongside the rest of the module sources. Hardening defines below.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "sqlite"));
		PublicDefinitions.AddRange(new string[]
		{
			"SQLITE_DQS=0",
			"SQLITE_DEFAULT_FOREIGN_KEYS=1",
			"SQLITE_THREADSAFE=2",
			"SQLITE_OMIT_DEPRECATED",
			"SQLITE_OMIT_LOAD_EXTENSION",
			"SQLITE_OMIT_AUTHORIZATION",
			"SQLITE_OMIT_TCL_VARIABLE",
			"SQLITE_ENABLE_FTS5",
			"SQLITE_ENABLE_JSON1"
		});

		// Suppress upstream-only warnings inside sqlite3.c so the C amalgamation
		// compiles without -Werror noise. SQLite is well-tested upstream — we treat
		// it as a vendor blob and don't patch it.
		bEnableUndefinedIdentifierWarnings = false;
		ShadowVariableWarningLevel = WarningLevel.Off;
	}
}
