// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// FUICommandList wrapper that exposes plugin shortcuts to the level editor.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"

class FAssetBrowserCommands : public TCommands<FAssetBrowserCommands>
{
public:
	FAssetBrowserCommands();
	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenAssetBrowser;   // open dockable window
	TSharedPtr<FUICommandInfo> OpenQuickPicker;    // Ctrl+Shift+Space overlay
};
