// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AssetBrowserCommands.h"
#include "AssetLibrarySettings.h"
#include "Styling/AppStyle.h"
#include "Framework/Commands/InputChord.h"

#define LOCTEXT_NAMESPACE "BABCommands"

FAssetBrowserCommands::FAssetBrowserCommands()
	: TCommands<FAssetBrowserCommands>(
		TEXT("BlenderAssetBrowser"),
		LOCTEXT("BABContext", "Blender Asset Browser"),
		NAME_None,
		FAppStyle::GetAppStyleSetName())
{
}

void FAssetBrowserCommands::RegisterCommands()
{
	UI_COMMAND(OpenAssetBrowser,
		"Open Blender Asset Browser",
		"Opens the BlenderAssetBrowser dockable window.",
		EUserInterfaceActionType::Button,
		FInputChord());

	// Quick Picker chord — user-configurable via Project Settings.
	const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>();
	const FKey Key = (S && S->QuickPickerKey.IsValid()) ? S->QuickPickerKey : EKeys::SpaceBar;
	int32 ModBits = 0;
	if (!S || S->bQuickPickerCtrl)  { ModBits |= EModifierKey::Control; }
	if (!S || S->bQuickPickerShift) { ModBits |= EModifierKey::Shift; }
	if (S && S->bQuickPickerAlt)    { ModBits |= EModifierKey::Alt; }

	UI_COMMAND(OpenQuickPicker,
		"Quick Asset Picker",
		"Spotlight-style overlay to find and spawn an asset.",
		EUserInterfaceActionType::Button,
		FInputChord(Key, static_cast<EModifierKey::Type>(ModBits)));
}

#undef LOCTEXT_NAMESPACE
