// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Launch external DCC applications (Substance Painter, Photoshop, 3ds Max)
// on a source file recorded in an asset's AssetImportData. Same security
// posture as the Blender bridge: argv array launch, no shell expansion.

#pragma once

#include "CoreMinimal.h"

enum class EDCCKind : uint8
{
	Blender,
	SubstancePainter,
	Photoshop,
	Max3ds,
};

class FDCCLauncher
{
public:
	/** Open `SourceFile` in the matching DCC. Returns true on launch.
	 *  If `AssociatedAssetPackage` is non-empty, registers the file with
	 *  the global DCC reimport watcher so save-in-DCC triggers auto-reimport. */
	static bool OpenInDCC(EDCCKind Kind, const FString& SourceFile,
	                      const FString& AssociatedAssetPackage = FString());

	/** Look at the file extension and return the most likely DCC. */
	static EDCCKind GuessKindFromExtension(const FString& Filename);

	/** Access the global watcher (set by BlenderBridgeModule on startup). */
	static class FDCCReimportWatcher* GetGlobalWatcher();
	static void SetGlobalWatcher(class FDCCReimportWatcher* InWatcher);
};
