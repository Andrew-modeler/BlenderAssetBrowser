// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "LocalSourceWatcher.h"
#include "UpdateCheckerModule.h"

#include "AssetLibrarySubsystem.h"
#include "AssetLibraryDatabase.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	BAB::FBoundValue B_Int(int64 X) { return BAB::FBoundValue::MakeInt(X); }
	BAB::FBoundValue B_Text(const FString& S) { return BAB::FBoundValue::MakeText(S); }

	bool IsSafePath(const FString& P)
	{
		if (P.IsEmpty() || P.Len() > BAB::MAX_PATH_LEN) { return false; }
		if (P.Contains(TEXT(".."))) { return false; }
		return true;
	}
}

int32 FLocalSourceWatcher::Scan(UAssetLibrarySubsystem* Sub)
{
	if (!Sub || !Sub->IsReady()) { return 0; }
	FAssetLibraryDatabase* Db = Sub->GetDatabase();
	if (!Db) { return 0; }

	struct FCandidate { int64 Id; FString SourceHash; };
	// `source_hash` in our schema is used to remember the modification timestamp
	// at import time (as ISO-8601 string). We compare today's mtime against it.
	TArray<FCandidate> Candidates;

	Db->QueryRows(
		TEXT("SELECT id, source_hash FROM assets WHERE source_type='imported' AND source_hash IS NOT NULL"),
		{},
		[&Candidates](const BAB::FRow& Row) -> bool
		{
			FCandidate C;
			C.Id = Row.GetInt64(0);
			C.SourceHash = Row.GetText(1);
			Candidates.Add(MoveTemp(C));
			return true;
		});

	int32 Flagged = 0;
	IFileManager& FM = IFileManager::Get();

	for (const FCandidate& C : Candidates)
	{
		// SourceHash format we picked: "<unix_seconds>|<path>"
		int32 BarIdx = -1;
		if (!C.SourceHash.FindChar(TEXT('|'), BarIdx)) { continue; }
		const FString TsStr = C.SourceHash.Left(BarIdx);
		const FString Path  = C.SourceHash.Mid(BarIdx + 1);
		if (!IsSafePath(Path)) { continue; }

		const int64 OldTs = FCString::Atoi64(*TsStr);
		const FDateTime Now = FM.GetTimeStamp(*Path);
		if (Now == FDateTime::MinValue()) { continue; }
		const int64 NowTs = Now.ToUnixTimestamp();

		if (NowTs > OldTs)
		{
			Db->Execute(
				TEXT("UPDATE assets SET update_state='source_changed' WHERE id=?"),
				{ B_Int(C.Id) });
			++Flagged;
		}
	}

	if (Flagged > 0)
	{
		UE_LOG(LogUpdateChecker, Log, TEXT("Source-changed flags set on %d assets."), Flagged);
	}
	return Flagged;
}
