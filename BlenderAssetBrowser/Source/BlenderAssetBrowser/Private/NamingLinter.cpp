// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "NamingLinter.h"
#include "AssetLibrarySubsystem.h"
#include "AssetLibraryDatabase.h"
#include "BlenderAssetBrowserModule.h"

TArray<FNamingRule> FNamingLinter::GetDefaultRules()
{
	// UE-community-standard prefixes. Override later by loading from a JSON
	// in Config/ if/when we expose user-editable rules.
	return {
		{ TEXT("StaticMesh"),               TEXT("SM_"),  FString() },
		{ TEXT("SkeletalMesh"),             TEXT("SK_"),  FString() },
		{ TEXT("Material"),                 TEXT("M_"),   FString() },
		{ TEXT("MaterialInstance"),         TEXT("MI_"),  FString() },
		{ TEXT("MaterialInstanceConstant"), TEXT("MI_"),  FString() },
		{ TEXT("MaterialFunction"),         TEXT("MF_"),  FString() },
		{ TEXT("Texture2D"),                TEXT("T_"),   FString() },
		{ TEXT("TextureCube"),              TEXT("TC_"),  FString() },
		{ TEXT("TextureRenderTarget2D"),    TEXT("RT_"),  FString() },
		{ TEXT("Blueprint"),                TEXT("BP_"),  FString() },
		{ TEXT("AnimSequence"),             TEXT("A_"),   FString() },
		{ TEXT("AnimMontage"),              TEXT("AM_"),  FString() },
		{ TEXT("PoseAsset"),                TEXT("POSE_"),FString() },
		{ TEXT("NiagaraSystem"),            TEXT("NS_"),  FString() },
		{ TEXT("NiagaraEmitter"),           TEXT("NE_"),  FString() },
		{ TEXT("SoundCue"),                 TEXT("S_"),   FString() },
		{ TEXT("SoundWave"),                TEXT("S_"),   FString() },
		{ TEXT("DataAsset"),                TEXT("DA_"),  FString() },
	};
}

FString FNamingLinter::SuggestRename(const FString& Name, const FNamingRule& Rule)
{
	if (Name.IsEmpty()) { return Name; }
	// Already starts with the expected prefix? Return as-is.
	if (!Rule.Prefix.IsEmpty() && Name.StartsWith(Rule.Prefix, ESearchCase::CaseSensitive))
	{
		return Name;
	}
	return Rule.Prefix + Name + Rule.Suffix;
}

TArray<FNamingViolation> FNamingLinter::Scan(UAssetLibrarySubsystem* Subsystem,
                                              const TArray<FNamingRule>* CustomRules)
{
	TArray<FNamingViolation> Out;
	if (!Subsystem || !Subsystem->IsReady()) { return Out; }
	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return Out; }

	const TArray<FNamingRule> Rules = CustomRules ? *CustomRules : GetDefaultRules();

	// Lookup by type for O(1) check per row.
	TMap<FString, const FNamingRule*> RuleByType;
	for (const FNamingRule& R : Rules)
	{
		RuleByType.Add(R.AssetType, &R);
	}

	Db->QueryRows(
		TEXT("SELECT id, asset_name, asset_type FROM assets ORDER BY asset_type, asset_name"),
		{},
		[&](const BAB::FRow& Row) -> bool
		{
			const int64   Id   = Row.GetInt64(0);
			const FString Name = Row.GetText(1);
			const FString Type = Row.GetText(2);

			const FNamingRule* const* Found = RuleByType.Find(Type);
			if (!Found || !*Found) { return true; }   // no rule for this type
			const FNamingRule& R = **Found;

			const bool bPrefixOk = R.Prefix.IsEmpty() ||
				Name.StartsWith(R.Prefix, ESearchCase::CaseSensitive);
			const bool bSuffixOk = R.Suffix.IsEmpty() ||
				Name.EndsWith(R.Suffix, ESearchCase::CaseSensitive);
			if (bPrefixOk && bSuffixOk) { return true; }

			FNamingViolation V;
			V.AssetId        = Id;
			V.AssetName      = Name;
			V.AssetType      = Type;
			V.ExpectedPrefix = R.Prefix;
			V.SuggestedName  = SuggestRename(Name, R);
			Out.Add(MoveTemp(V));
			return true;
		});

	if (Out.Num() > 0)
	{
		UE_LOG(LogBlenderAssetBrowser, Log, TEXT("NamingLinter: %d violation(s)."), Out.Num());
	}
	return Out;
}
