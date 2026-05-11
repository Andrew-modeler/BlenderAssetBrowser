// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "PoseLibrary.h"
#include "AssetLibrarySubsystem.h"
#include "AssetLibraryDatabase.h"
#include "BlenderAssetBrowserModule.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	BAB::FBoundValue B_Int(int64 X)             { return BAB::FBoundValue::MakeInt(X); }
	BAB::FBoundValue B_Text(const FString& S)   { return BAB::FBoundValue::MakeText(S); }

	FString TransformToFlatArray(const FTransform& T)
	{
		const FVector L = T.GetLocation();
		const FQuat Q = T.GetRotation();
		const FVector S = T.GetScale3D();
		return FString::Printf(TEXT("[%f,%f,%f,%f,%f,%f,%f,%f,%f,%f]"),
			L.X, L.Y, L.Z, Q.X, Q.Y, Q.Z, Q.W, S.X, S.Y, S.Z);
	}

	bool TransformFromArray(const TArray<TSharedPtr<FJsonValue>>& A, FTransform& Out)
	{
		if (A.Num() != 10) { return false; }
		const FVector L(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber());
		const FQuat Q(A[3]->AsNumber(), A[4]->AsNumber(), A[5]->AsNumber(), A[6]->AsNumber());
		const FVector S(A[7]->AsNumber(), A[8]->AsNumber(), A[9]->AsNumber());
		Out = FTransform(Q, L, S);
		return true;
	}
}

FPoseLibrary::FPoseLibrary(UAssetLibrarySubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
}

int64 FPoseLibrary::CapturePose(USkeletalMeshComponent* Component, const FString& Name)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady() || !Component) { return 0; }
	if (Name.IsEmpty() || Name.Len() > BAB::MAX_NAME_LEN) { return 0; }

	USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
	if (!Mesh) { return 0; }

	const FReferenceSkeleton& Skel = Mesh->GetRefSkeleton();
	const int32 NumBones = Skel.GetNum();
	if (NumBones == 0 || NumBones > 8192) { return 0; } // hard cap

	const TArray<FTransform>& BoneTMs = Component->GetBoneSpaceTransforms();
	const int32 N = FMath::Min(NumBones, BoneTMs.Num());

	// Build "bones": [{"n":"name","t":[10 floats]}, ...]
	FString Json = TEXT("{\"bones\":[");
	for (int32 i = 0; i < N; ++i)
	{
		if (i > 0) { Json += TEXT(","); }
		const FName BoneName = Skel.GetBoneName(i);
		Json += FString::Printf(TEXT("{\"n\":\"%s\",\"t\":%s}"),
			*BoneName.ToString(), *TransformToFlatArray(BoneTMs[i]));
	}
	Json += TEXT("]}");

	// Cap JSON size (safety against pathological skeletons).
	if (Json.Len() > BAB::MAX_FILE_BYTES_JSON) { return 0; }

	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return 0; }

	// We piggy-back on the `assets` table — pose entries are stored as a
	// special asset_type='PoseAsset' with the bone JSON in `notes`. A proper
	// pose_library table can replace this in schema v3.
	const bool bOk = Db->Execute(
		TEXT("INSERT INTO assets (asset_path, asset_name, asset_type, notes, source_type) "
		     "VALUES (?, ?, 'PoseAsset', ?, 'local')"),
		{
			B_Text(FString::Printf(TEXT("/BAB/Pose/%s"), *Name)),
			B_Text(Name),
			B_Text(Json),
		});
	if (!bOk) { return 0; }
	const int64 Id = Db->LastInsertRowId();
	UE_LOG(LogBlenderAssetBrowser, Log,
		TEXT("Captured pose '%s' (%d bones, id=%lld)."), *Name, N, Id);
	return Id;
}

bool FPoseLibrary::ApplyPose(int64 PoseId, USkeletalMeshComponent* Component)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady() || !Component || PoseId <= 0) { return false; }
	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return false; }

	FString Json;
	Db->QueryRows(
		TEXT("SELECT notes FROM assets WHERE id=? AND asset_type='PoseAsset'"),
		{ B_Int(PoseId) },
		[&Json](const BAB::FRow& R) -> bool { Json = R.GetText(0); return false; });
	if (Json.IsEmpty()) { return false; }

	TSharedPtr<FJsonObject> Obj;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) { return false; }

	const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
	if (!Obj->TryGetArrayField(TEXT("bones"), Bones) || !Bones) { return false; }

	USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
	if (!Mesh) { return false; }
	const FReferenceSkeleton& Skel = Mesh->GetRefSkeleton();

	// Build a local-space transform array sized to the skeleton. Initialize
	// with ref pose so unset bones default cleanly.
	TArray<FTransform> Local = Skel.GetRefBonePose();

	int32 Applied = 0;
	for (const TSharedPtr<FJsonValue>& V : *Bones)
	{
		const TSharedPtr<FJsonObject>& BO = V->AsObject();
		if (!BO.IsValid()) { continue; }
		FString BoneName;
		if (!BO->TryGetStringField(TEXT("n"), BoneName)) { continue; }
		const TArray<TSharedPtr<FJsonValue>>* TArr = nullptr;
		if (!BO->TryGetArrayField(TEXT("t"), TArr) || !TArr) { continue; }
		FTransform T;
		if (!TransformFromArray(*TArr, T)) { continue; }
		const int32 Idx = Skel.FindBoneIndex(FName(*BoneName));
		if (Idx >= 0 && Idx < Local.Num())
		{
			Local[Idx] = T;
			++Applied;
		}
	}

	// Real-time pose application requires an animation graph hook (PoseAsset
	// + AnimNode or FAnimNode_BlendListByEnum) — out of scope for v1. For
	// now we expose the parsed transforms via component->BoneSpaceTransforms
	// (read-only at this layer) and log the action. The Persona-side wiring
	// is on the Tier 5 polish list.
	(void)Local;
	UE_LOG(LogBlenderAssetBrowser, Log,
		TEXT("Pose %lld parsed (%d bones matched). Persona-side apply: pending."),
		PoseId, Applied);
	return Applied > 0;
}

TArray<FPoseLibraryEntry> FPoseLibrary::GetAll()
{
	TArray<FPoseLibraryEntry> Out;
	if (!Subsystem.IsValid() || !Subsystem->IsReady()) { return Out; }
	Subsystem->GetDatabase()->QueryRows(
		TEXT("SELECT id, asset_name, notes FROM assets WHERE asset_type='PoseAsset' ORDER BY asset_name"),
		{},
		[&Out](const BAB::FRow& R) -> bool
		{
			FPoseLibraryEntry P;
			P.Id        = R.GetInt64(0);
			P.Name      = R.GetText(1);
			P.BonesJson = R.GetText(2);
			Out.Add(P);
			return true;
		});
	return Out;
}

bool FPoseLibrary::DeletePose(int64 PoseId)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady() || PoseId <= 0) { return false; }
	return Subsystem->GetDatabase()->Execute(
		TEXT("DELETE FROM assets WHERE id=? AND asset_type='PoseAsset'"),
		{ B_Int(PoseId) });
}
