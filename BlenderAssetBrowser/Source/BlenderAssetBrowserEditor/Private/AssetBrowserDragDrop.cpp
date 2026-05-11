// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AssetBrowserDragDrop.h"
#include "BlenderAssetBrowserEditorModule.h"

#include "AssetLibraryTypes.h"

#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "BABDragDrop"

TSharedRef<FAssetBrowserDragDropOp> FAssetBrowserDragDropOp::New(const TArray<FAssetData>& InAssets)
{
	auto Op = MakeShared<FAssetBrowserDragDropOp>();
	Op->Assets = InAssets;
	// Cap payload at 256 entries — defends against pathological selections.
	if (Op->Assets.Num() > 256) { Op->Assets.SetNum(256); }
	Op->Construct();
	return Op;
}

namespace
{
	bool LooksLikeMaterialPath(const FAssetData& A)
	{
		const FString C = A.AssetClassPath.GetAssetName().ToString();
		return C == TEXT("Material") || C == TEXT("MaterialInstance") ||
		       C == TEXT("MaterialInstanceConstant");
	}

	bool LooksLikeStaticMeshPath(const FAssetData& A)
	{
		return A.AssetClassPath.GetAssetName() == FName(TEXT("StaticMesh"));
	}

	UMaterialInterface* LoadMaterialSafe(const FAssetData& Data)
	{
		if (!LooksLikeMaterialPath(Data)) { return nullptr; }
		UObject* Obj = Data.GetAsset();
		return Cast<UMaterialInterface>(Obj);
	}

	UStaticMesh* LoadStaticMeshSafe(const FAssetData& Data)
	{
		if (!LooksLikeStaticMeshPath(Data)) { return nullptr; }
		UObject* Obj = Data.GetAsset();
		return Cast<UStaticMesh>(Obj);
	}

	/** Apply material to a single slot of the actor's first matching mesh component.
	 *  HitSectionIndex < 0 means "fall back to slot 0". */
	bool ApplyMaterialToSlot(AActor* Actor, int32 HitSectionIndex, UMaterialInterface* Material)
	{
		if (!Actor || !Material) { return false; }

		// Try StaticMeshComponent first.
		TArray<UStaticMeshComponent*> SMCs;
		Actor->GetComponents<UStaticMeshComponent>(SMCs);
		for (UStaticMeshComponent* SMC : SMCs)
		{
			if (!SMC || !SMC->GetStaticMesh()) { continue; }
			const int32 Num = SMC->GetNumMaterials();
			if (Num <= 0) { continue; }
			const int32 SlotIdx = (HitSectionIndex >= 0 && HitSectionIndex < Num) ? HitSectionIndex : 0;
			SMC->Modify();
			SMC->SetMaterial(SlotIdx, Material);
			return true;
		}

		// Then SkeletalMeshComponent.
		TArray<USkeletalMeshComponent*> SKCs;
		Actor->GetComponents<USkeletalMeshComponent>(SKCs);
		for (USkeletalMeshComponent* SK : SKCs)
		{
			if (!SK) { continue; }
			const int32 Num = SK->GetNumMaterials();
			if (Num <= 0) { continue; }
			const int32 SlotIdx = (HitSectionIndex >= 0 && HitSectionIndex < Num) ? HitSectionIndex : 0;
			SK->Modify();
			SK->SetMaterial(SlotIdx, Material);
			return true;
		}

		return false;
	}

	AActor* SpawnStaticMeshActor(UWorld* World, const FVector& Loc, UStaticMesh* Mesh)
	{
		if (!World || !Mesh) { return nullptr; }
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Loc, FRotator::ZeroRotator, Params);
		if (Actor && Actor->GetStaticMeshComponent())
		{
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			Actor->SetActorLabel(Mesh->GetName());
		}
		return Actor;
	}
}

bool FAssetBrowserDragDropOp::ExecuteViewportDrop(UWorld* World, const FVector& HitLocation,
	AActor* HitActor, int32 HitSectionIndex) const
{
	if (!World) { return false; }
	if (Assets.Num() == 0) { return false; }

	FScopedTransaction Transaction(LOCTEXT("DropTx", "Drop from Blender Asset Browser"));
	bool bAny = false;

	for (const FAssetData& A : Assets)
	{
		// Material → existing actor's hit slot (only if we have a target).
		if (HitActor && LooksLikeMaterialPath(A))
		{
			if (UMaterialInterface* Mat = LoadMaterialSafe(A))
			{
				if (ApplyMaterialToSlot(HitActor, HitSectionIndex, Mat))
				{
					bAny = true;
					continue;
				}
			}
		}

		// StaticMesh → spawn actor at hit location.
		if (LooksLikeStaticMeshPath(A))
		{
			if (UStaticMesh* Mesh = LoadStaticMeshSafe(A))
			{
				if (SpawnStaticMeshActor(World, HitLocation, Mesh))
				{
					bAny = true;
					continue;
				}
			}
		}
	}

	return bAny;
}

#undef LOCTEXT_NAMESPACE
