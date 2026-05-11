// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Pose Library — store a snapshot of a Skeletal Mesh's bone transforms as a
// lightweight asset that can be re-applied to compatible skeletons.
//
// The MVP captures local-space bone transforms keyed by bone name. Applying
// matches bones by name (compatible skeletons share names). Blending and
// retargeting are polish for a later phase.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "AssetLibraryTypes.h"

class USkeletalMeshComponent;
class UAssetLibrarySubsystem;

struct BLENDERASSETBROWSER_API FPoseLibraryEntry
{
	int64   Id = 0;
	FString Name;
	FString SkeletonPath;
	FString BonesJson;          // [{"n":"bone","t":[...]}, ...]
};

class BLENDERASSETBROWSER_API FPoseLibrary
{
public:
	explicit FPoseLibrary(UAssetLibrarySubsystem* InSubsystem);

	/** Capture local bone transforms from `Component`. Returns id, 0 on failure. */
	int64 CapturePose(USkeletalMeshComponent* Component, const FString& PoseName);

	/** Apply pose by id onto `Component` (bones matched by name). */
	bool ApplyPose(int64 PoseId, USkeletalMeshComponent* Component);

	/** List all captured poses. */
	TArray<FPoseLibraryEntry> GetAll();

	bool DeletePose(int64 PoseId);

private:
	TWeakObjectPtr<UAssetLibrarySubsystem> Subsystem;
};
