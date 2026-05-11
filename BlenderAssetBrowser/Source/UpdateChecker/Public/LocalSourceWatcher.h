// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Watches local source files (the original .fbx/.psd/.spp that an asset was
// imported from) and flags `update_state='source_changed'` when their mtime
// moves forward past the value we recorded at import time.

#pragma once

#include "CoreMinimal.h"

class UAssetLibrarySubsystem;

class FLocalSourceWatcher
{
public:
	/** One pass: walk all assets that have a recorded source file and update
	 *  their update_state. Returns the number of assets newly flagged. */
	static int32 Scan(UAssetLibrarySubsystem* Subsystem);
};
