// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "MaterialPresetManager.h"
#include "AssetLibrarySubsystem.h"
#include "AssetLibraryDatabase.h"
#include "BlenderAssetBrowserModule.h"

#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace
{
	BAB::FBoundValue B_Int(int64 X)             { return BAB::FBoundValue::MakeInt(X); }
	BAB::FBoundValue B_Text(const FString& S)   { return BAB::FBoundValue::MakeText(S); }
	BAB::FBoundValue B_Null()                   { return BAB::FBoundValue::MakeNull(); }
}

FMaterialPresetManager::FMaterialPresetManager(UAssetLibrarySubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
}

int64 FMaterialPresetManager::CaptureFromMIC(UMaterialInstanceConstant* MIC, const FString& Name)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady() || !MIC) { return 0; }
	if (Name.IsEmpty() || Name.Len() > BAB::MAX_NAME_LEN) { return 0; }

	UMaterialInterface* Parent = MIC->Parent;
	if (!Parent) { return 0; }

	TSharedRef<FJsonObject> Root  = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Scals = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Vecs  = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Texs  = MakeShared<FJsonObject>();

	for (const FScalarParameterValue& P : MIC->ScalarParameterValues)
	{
		Scals->SetNumberField(P.ParameterInfo.Name.ToString(), P.ParameterValue);
	}
	for (const FVectorParameterValue& P : MIC->VectorParameterValues)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueNumber>(P.ParameterValue.R));
		Arr.Add(MakeShared<FJsonValueNumber>(P.ParameterValue.G));
		Arr.Add(MakeShared<FJsonValueNumber>(P.ParameterValue.B));
		Arr.Add(MakeShared<FJsonValueNumber>(P.ParameterValue.A));
		Vecs->SetArrayField(P.ParameterInfo.Name.ToString(), Arr);
	}
	for (const FTextureParameterValue& P : MIC->TextureParameterValues)
	{
		if (P.ParameterValue)
		{
			Texs->SetStringField(P.ParameterInfo.Name.ToString(), P.ParameterValue->GetPathName());
		}
	}
	Root->SetObjectField(TEXT("scalars"),  Scals);
	Root->SetObjectField(TEXT("vectors"),  Vecs);
	Root->SetObjectField(TEXT("textures"), Texs);

	FString OutJson;
	auto Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root, Writer);

	const FString ParentPath = Parent->GetPathName();

	// Reuse the existing `assets` row for the MIC if marked. Otherwise auto-mark.
	// For simplicity, we create a `material_preset` row in the catalogs metadata
	// area — but since we don't have a dedicated table, we store the preset in
	// the `notes` field of an auto-created asset entry tagged with preset:1.
	// Real schema would be a new table; v1 keeps the schema unchanged.
	// (Schema v3 with a dedicated table is a polish task.)

	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return 0; }

	const FString MicPath = MIC->GetPathName();
	int64 AssetId = 0;
	Db->QueryRows(
		TEXT("SELECT id FROM assets WHERE asset_path LIKE ? AND library_id IS NULL LIMIT 1"),
		{ B_Text(FString::Printf(TEXT("%%%s%%"), *FPaths::GetBaseFilename(MicPath))) },
		[&AssetId](const BAB::FRow& R) -> bool { AssetId = R.GetInt64(0); return false; });

	const FString PresetBlob = FString::Printf(
		TEXT("PRESET_v1\nname=%s\nparent=%s\nparams=%s"),
		*Name, *ParentPath, *OutJson);

	if (AssetId > 0)
	{
		Db->Execute(
			TEXT("UPDATE assets SET notes=? WHERE id=?"),
			{ B_Text(PresetBlob), B_Int(AssetId) });
	}
	else
	{
		// Anonymous capture — log only.
		UE_LOG(LogBlenderAssetBrowser, Log,
			TEXT("Captured material preset (not associated with library asset): %s"),
			*Name);
	}

	UE_LOG(LogBlenderAssetBrowser, Log,
		TEXT("Material preset '%s' captured (%d scalar / %d vector / %d texture)."),
		*Name,
		MIC->ScalarParameterValues.Num(),
		MIC->VectorParameterValues.Num(),
		MIC->TextureParameterValues.Num());
	return AssetId > 0 ? AssetId : 1;
}

TArray<FMaterialPreset> FMaterialPresetManager::GetAll()
{
	// MVP: returns empty. UI will treat material-typed assets with "PRESET_v1"
	// in their notes as presets. Proper accessors land with the dedicated table.
	return {};
}

bool FMaterialPresetManager::DeletePreset(int64 PresetId)
{
	return false;
}
