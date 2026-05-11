// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AssetLibraryTypes.h"
#include "Misc/Paths.h"

namespace
{
	bool ContainsControlChars(const FString& S)
	{
		for (TCHAR C : S)
		{
			// Reject NUL, control chars, and DEL. Tab is allowed elsewhere but not in names/paths.
			if (C < 0x20 || C == 0x7F)
			{
				return true;
			}
		}
		return false;
	}
}

bool FCatalogEntry::Validate(FString& OutError) const
{
	if (Name.IsEmpty()) { OutError = TEXT("Catalog name is empty"); return false; }
	if (Name.Len() > BAB::MAX_NAME_LEN) { OutError = TEXT("Catalog name too long"); return false; }
	if (ContainsControlChars(Name)) { OutError = TEXT("Catalog name contains control characters"); return false; }
	if (Id < 0 || ParentId < 0) { OutError = TEXT("Negative IDs"); return false; }
	if (Id != 0 && Id == ParentId) { OutError = TEXT("Catalog cannot be its own parent"); return false; }
	if (SmartQuery.Len() > BAB::MAX_QUERY_LEN) { OutError = TEXT("Smart query too long"); return false; }
	if (!Color.IsEmpty() && (Color.Len() != 7 || !Color.StartsWith(TEXT("#"))))
	{
		OutError = TEXT("Color must be empty or '#rrggbb'"); return false;
	}
	return true;
}

bool FTagEntry::Validate(FString& OutError) const
{
	if (Name.IsEmpty()) { OutError = TEXT("Tag name is empty"); return false; }
	if (Name.Len() > BAB::MAX_TAG_LEN) { OutError = TEXT("Tag name too long"); return false; }
	if (ContainsControlChars(Name)) { OutError = TEXT("Tag contains control chars"); return false; }
	if (Id < 0) { OutError = TEXT("Negative tag ID"); return false; }

	// Hierarchy depth check: count slash separators, must not exceed MAX_TAG_DEPTH
	int32 Depth = 1;
	for (TCHAR C : Name) { if (C == TEXT('/')) { ++Depth; } }
	if (Depth > BAB::MAX_TAG_DEPTH) { OutError = TEXT("Tag hierarchy too deep"); return false; }

	// Allowed chars: alnum, slash, dash, underscore, space
	for (TCHAR C : Name)
	{
		const bool bOk = FChar::IsAlnum(C) || C == TEXT('/') || C == TEXT('-') ||
		                 C == TEXT('_') || C == TEXT(' ');
		if (!bOk) { OutError = TEXT("Tag has disallowed character"); return false; }
	}
	return true;
}

bool FLibraryEntry::Validate(FString& OutError) const
{
	if (Name.IsEmpty()) { OutError = TEXT("Library name is empty"); return false; }
	if (Name.Len() > BAB::MAX_NAME_LEN) { OutError = TEXT("Library name too long"); return false; }
	if (Path.IsEmpty()) { OutError = TEXT("Library path is empty"); return false; }
	if (Path.Len() > BAB::MAX_PATH_LEN) { OutError = TEXT("Library path too long"); return false; }
	if (ContainsControlChars(Name) || ContainsControlChars(Path))
	{
		OutError = TEXT("Library has control chars"); return false;
	}
	if (Type != TEXT("local") && Type != TEXT("network") && Type != TEXT("mounted"))
	{
		OutError = TEXT("Library type must be local/network/mounted"); return false;
	}
	return true;
}

bool FAssetEntry::Validate(FString& OutError) const
{
	if (AssetPath.IsEmpty()) { OutError = TEXT("Asset path is empty"); return false; }
	if (AssetPath.Len() > BAB::MAX_PATH_LEN) { OutError = TEXT("Asset path too long"); return false; }
	if (AssetName.Len() > BAB::MAX_NAME_LEN) { OutError = TEXT("Asset name too long"); return false; }
	if (AssetType.Len() > BAB::MAX_NAME_LEN) { OutError = TEXT("Asset type too long"); return false; }
	if (Notes.Len() > BAB::MAX_NOTES_LEN) { OutError = TEXT("Notes too long"); return false; }
	if (Rating < 0 || Rating > 5) { OutError = TEXT("Rating out of range [0..5]"); return false; }
	if (Id < 0 || LibraryId < 0) { OutError = TEXT("Negative IDs"); return false; }
	if (TriCount < 0 || VertCount < 0 || LodCount < 0 || TextureResMax < 0)
	{
		OutError = TEXT("Negative metadata counts"); return false;
	}
	if (DiskSizeBytes < 0) { OutError = TEXT("Negative disk size"); return false; }
	if (SourceUrl.Len() > BAB::MAX_URL_LEN) { OutError = TEXT("Source URL too long"); return false; }
	if (SourceAuthor.Len() > BAB::MAX_AUTHOR_LEN) { OutError = TEXT("Source author too long"); return false; }
	if (SourceLicense.Len() > BAB::MAX_LICENSE_LEN) { OutError = TEXT("Source license too long"); return false; }
	if (SourceVersion.Len() > BAB::MAX_VERSION_LEN) { OutError = TEXT("Source version too long"); return false; }
	if (SourceHash.Len() > BAB::MAX_HASH_LEN) { OutError = TEXT("Source hash too long"); return false; }
	if (ContainsControlChars(AssetPath) || ContainsControlChars(AssetName))
	{
		OutError = TEXT("Asset path/name contains control chars"); return false;
	}
	// Asset URL, if provided, must be HTTPS only — no http://, javascript:, file://
	if (!SourceUrl.IsEmpty() && !SourceUrl.StartsWith(TEXT("https://"), ESearchCase::CaseSensitive))
	{
		OutError = TEXT("Source URL must be HTTPS"); return false;
	}
	return true;
}

bool FCollectionEntry::Validate(FString& OutError) const
{
	if (Name.IsEmpty()) { OutError = TEXT("Collection name is empty"); return false; }
	if (Name.Len() > BAB::MAX_NAME_LEN) { OutError = TEXT("Collection name too long"); return false; }
	if (Description.Len() > BAB::MAX_NOTES_LEN) { OutError = TEXT("Description too long"); return false; }
	if (SpawnMode != TEXT("blueprint") && SpawnMode != TEXT("level_instance") && SpawnMode != TEXT("loose"))
	{
		OutError = TEXT("Spawn mode must be blueprint/level_instance/loose"); return false;
	}
	for (const FCollectionItem& Item : Items)
	{
		if (Item.AssetId <= 0) { OutError = TEXT("Collection item has invalid asset id"); return false; }
		if (Item.TransformJson.Len() > BAB::MAX_NOTES_LEN ||
		    Item.MaterialOverridesJson.Len() > BAB::MAX_NOTES_LEN)
		{
			OutError = TEXT("Collection item JSON too long"); return false;
		}
	}
	return true;
}

namespace BAB
{
	bool IsPathInsideRoot(const FString& InPath, const FString& AllowedRoot)
	{
		if (InPath.IsEmpty() || AllowedRoot.IsEmpty()) { return false; }

		FString CanonicalIn   = FPaths::ConvertRelativePathToFull(InPath);
		FString CanonicalRoot = FPaths::ConvertRelativePathToFull(AllowedRoot);

		FPaths::NormalizeDirectoryName(CanonicalIn);
		FPaths::NormalizeDirectoryName(CanonicalRoot);

		// Defensive: reject paths still containing ".." after normalization (shouldn't happen
		// after ConvertRelativePathToFull, but cheap belt-and-suspenders).
		if (CanonicalIn.Contains(TEXT("..")) || CanonicalRoot.Contains(TEXT("..")))
		{
			return false;
		}

		// Case-insensitive prefix match on Windows; case-sensitive elsewhere.
#if PLATFORM_WINDOWS
		const ESearchCase::Type CaseMode = ESearchCase::IgnoreCase;
#else
		const ESearchCase::Type CaseMode = ESearchCase::CaseSensitive;
#endif

		// Make sure root ends with a separator so /allowed/root2/foo doesn't match /allowed/root.
		FString RootWithSep = CanonicalRoot;
		if (!RootWithSep.EndsWith(TEXT("/"), CaseMode) && !RootWithSep.EndsWith(TEXT("\\"), CaseMode))
		{
			RootWithSep.AppendChar(TEXT('/'));
		}

		return CanonicalIn.Equals(CanonicalRoot, CaseMode) ||
		       CanonicalIn.StartsWith(RootWithSep, CaseMode);
	}

	FString SanitizeName(const FString& In, int32 MaxLen)
	{
		FString Out;
		Out.Reserve(FMath::Min(In.Len(), MaxLen));
		for (TCHAR C : In)
		{
			if (Out.Len() >= MaxLen) { break; }
			if (C >= 0x20 && C != 0x7F) { Out.AppendChar(C); }
		}
		return Out;
	}
}
