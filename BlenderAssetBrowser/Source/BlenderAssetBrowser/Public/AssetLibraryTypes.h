// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Core data types for the asset library.
// All structs are POD-ish (no UObject) and have Validate() for input sanitization.
//
// SECURITY NOTE: every struct enforces field-length limits inside Validate().
// Any data coming from disk (JSON sidecar) or DB MUST be Validate()-ed before
// downstream code uses it. Truncation > rejection so the editor never crashes
// on a malicious .assetlib that has 10 MB of NUL bytes in a name field.

#pragma once

#include "CoreMinimal.h"

namespace BAB
{
	// Hard limits — these are NOT just UI-cosmetic. They protect every
	// caller from crafted-input attacks (oversized strings, hash bombs).
	constexpr int32 MAX_PATH_LEN       = 1024;
	constexpr int32 MAX_NAME_LEN       = 256;
	constexpr int32 MAX_NOTES_LEN      = 4096;
	constexpr int32 MAX_TAG_LEN        = 128;
	constexpr int32 MAX_TAG_DEPTH      = 8;          // levels in "a/b/c/..." hierarchy
	constexpr int32 MAX_URL_LEN        = 2048;
	constexpr int32 MAX_AUTHOR_LEN     = 256;
	constexpr int32 MAX_LICENSE_LEN    = 64;
	constexpr int32 MAX_VERSION_LEN    = 64;
	constexpr int32 MAX_HASH_LEN       = 128;        // SHA-256 hex = 64; pad for SHA-512
	constexpr int32 MAX_QUERY_LEN      = 1024;
	constexpr int32 MAX_FILE_BYTES_FBX = 200 * 1024 * 1024;   // 200 MB cap on Assimp inputs
	constexpr int32 MAX_FILE_BYTES_JSON= 50 * 1024 * 1024;    // 50 MB cap on .assetlib
}

enum class EAssetLibrarySource : uint8
{
	Unknown,
	Local,        // imported from disk
	Fab,          // bought via Fab marketplace
	Megascans,    // Quixel Megascans pack
	Custom,       // user-tagged source
	Imported      // manually marked, no metadata
};

enum class EAssetUpdateState : uint8
{
	UpToDate,
	UpdateAvailable,
	SourceFileChanged,
	Unknown
};

struct BLENDERASSETBROWSER_API FCatalogEntry
{
	int64  Id        = 0;
	int64  ParentId  = 0;       // 0 = root
	FString Name;               // <= MAX_NAME_LEN
	FString Color;              // hex like "#aabbcc"
	FString Icon;               // icon identifier
	int32   SortOrder = 0;
	bool    bIsSmart  = false;
	FString SmartQuery;         // structured query, parsed not eval-ed
	FDateTime CreatedAt;

	bool Validate(FString& OutError) const;
};

struct BLENDERASSETBROWSER_API FTagEntry
{
	int64  Id    = 0;
	FString Name;        // hierarchical "env/foliage/grass" allowed; <= MAX_TAG_LEN
	FString Color;
	FString Parent;
	int32   Count = 0;

	bool Validate(FString& OutError) const;
};

struct BLENDERASSETBROWSER_API FLibraryEntry
{
	int64  Id   = 0;
	FString Name;               // friendly name
	FString Path;               // absolute disk path to library root
	FString Type = TEXT("local");
	int32   Priority = 0;
	bool    bIsVisible = true;
	FDateTime CreatedAt;

	bool Validate(FString& OutError) const;
};

struct BLENDERASSETBROWSER_API FAssetEntry
{
	int64  Id          = 0;
	FString AssetPath;          // /Game/.. for uassets, abs path for external
	FString AssetName;
	FString AssetType;          // "StaticMesh", "Material", ...
	int64  LibraryId   = 0;
	int32  Rating      = 0;     // 0-5
	FString Notes;
	FString PreviewPath;
	FString PreviewMesh;        // "Sphere"/"Cube"/"Plane"/... for materials

	// Technical metadata (auto-populated)
	int32  TriCount      = 0;
	int32  VertCount     = 0;
	int32  LodCount      = 0;
	int32  TextureResMax = 0;
	bool   bHasCollision = false;
	FString CollisionType;
	int32  MaterialCount = 0;
	int64  DiskSizeBytes = 0;
	FString EngineVersion;

	// Provenance
	EAssetLibrarySource SourceType = EAssetLibrarySource::Unknown;
	FString SourcePackName;
	FString SourcePackId;
	FString SourceUrl;
	FString SourceAuthor;
	FString SourceLicense;
	FString SourceVersion;
	FString SourceHash;
	FDateTime ImportedAt;

	// Update tracking
	FString LatestVersion;
	EAssetUpdateState UpdateState = EAssetUpdateState::Unknown;
	FString Changelog;

	FDateTime CreatedAt;
	FDateTime ModifiedAt;

	bool Validate(FString& OutError) const;
};

struct BLENDERASSETBROWSER_API FCollectionItem
{
	int64  AssetId        = 0;
	FString TransformJson;       // {"loc":[..],"rot":[..],"scale":[..]}
	FString MaterialOverridesJson;
	int32   SortOrder = 0;
};

struct BLENDERASSETBROWSER_API FCollectionEntry
{
	int64  Id = 0;
	FString Name;
	FString Description;
	FString PreviewPath;
	FString SpawnMode = TEXT("blueprint");   // blueprint | level_instance | loose
	FDateTime CreatedAt;

	TArray<FCollectionItem> Items;

	bool Validate(FString& OutError) const;
};

// Path helpers — every untrusted path passes through this before any FS op.
namespace BAB
{
	/**
	 * Returns true if `InPath` is a syntactically safe path under `AllowedRoot`.
	 * Resolves canonical form and checks for `..` traversal escape.
	 * SECURITY: this is the FIRST line of defense for path-traversal attacks
	 * via .assetlib JSON or Blender bridge manifests.
	 */
	BLENDERASSETBROWSER_API bool IsPathInsideRoot(const FString& InPath, const FString& AllowedRoot);

	/** Strips/sanitizes a string to safe ASCII subset for catalog/tag names. */
	BLENDERASSETBROWSER_API FString SanitizeName(const FString& In, int32 MaxLen);
}
