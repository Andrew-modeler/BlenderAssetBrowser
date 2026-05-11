// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "CollectionManager.h"
#include "AssetLibrarySubsystem.h"
#include "AssetLibraryDatabase.h"
#include "BlenderAssetBrowserModule.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	BAB::FBoundValue B_Int(int64 X)             { return BAB::FBoundValue::MakeInt(X); }
	BAB::FBoundValue B_Text(const FString& S)   { return BAB::FBoundValue::MakeText(S); }

	FString TransformToJson(const FTransform& T)
	{
		const FVector L = T.GetLocation();
		const FRotator R = T.Rotator();
		const FVector S = T.GetScale3D();
		return FString::Printf(
			TEXT("{\"l\":[%f,%f,%f],\"r\":[%f,%f,%f],\"s\":[%f,%f,%f]}"),
			L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll, S.X, S.Y, S.Z);
	}

	FTransform TransformFromJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Obj;
		auto Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			return FTransform::Identity;
		}
		auto ReadVec = [&](const TCHAR* Field) -> FVector
		{
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (!Obj->TryGetArrayField(Field, Arr) || !Arr || Arr->Num() != 3)
			{
				return FVector::ZeroVector;
			}
			return FVector(
				(*Arr)[0]->AsNumber(),
				(*Arr)[1]->AsNumber(),
				(*Arr)[2]->AsNumber());
		};
		const FVector L = ReadVec(TEXT("l"));
		const FVector RV = ReadVec(TEXT("r"));
		const FVector S = ReadVec(TEXT("s"));
		return FTransform(FRotator(RV.X, RV.Y, RV.Z), L, S);
	}

	FString GetMeshAssetPath(AActor* Actor)
	{
		if (!Actor) { return FString(); }
		TArray<UStaticMeshComponent*> SMCs;
		Actor->GetComponents<UStaticMeshComponent>(SMCs);
		for (UStaticMeshComponent* SMC : SMCs)
		{
			if (SMC && SMC->GetStaticMesh())
			{
				return SMC->GetStaticMesh()->GetPathName();
			}
		}
		return FString();
	}
}

FCollectionManager::FCollectionManager(UAssetLibrarySubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
}

int64 FCollectionManager::CreateFromActors(const FString& Name, const TArray<AActor*>& Actors)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady() || Actors.Num() == 0) { return 0; }
	if (Name.IsEmpty() || Name.Len() > BAB::MAX_NAME_LEN) { return 0; }
	if (Actors.Num() > 1024) { return 0; } // sanity cap

	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return 0; }

	// Compute group pivot = centroid of actor locations.
	FVector Centroid = FVector::ZeroVector;
	int32 Count = 0;
	for (AActor* A : Actors)
	{
		if (!A) { continue; }
		Centroid += A->GetActorLocation();
		++Count;
	}
	if (Count == 0) { return 0; }
	Centroid /= static_cast<float>(Count);

	int64 NewId = 0;
	const bool bOk = Db->Transaction([&]() -> bool
	{
		if (!Db->Execute(
			TEXT("INSERT INTO collections (name, spawn_mode) VALUES (?, 'blueprint')"),
			{ B_Text(Name) })) { return false; }
		NewId = Db->LastInsertRowId();
		if (NewId <= 0) { return false; }

		int32 SortOrder = 0;
		for (AActor* A : Actors)
		{
			if (!A) { continue; }
			const FString MeshPath = GetMeshAssetPath(A);
			if (MeshPath.IsEmpty()) { continue; }

			// Look up the asset_id in our library, if marked. If not marked,
			// skip — the user must Mark as Asset first.
			int64 AssetId = 0;
			const FString PkgName = FPaths::GetBaseFilename(MeshPath, false);
			Db->QueryRows(
				TEXT("SELECT id FROM assets WHERE asset_path LIKE ? LIMIT 1"),
				{ B_Text(FString::Printf(TEXT("%%/%s"), *FPaths::GetBaseFilename(MeshPath))) },
				[&AssetId](const BAB::FRow& R) -> bool
				{
					AssetId = R.GetInt64(0);
					return false;
				});
			if (AssetId <= 0) { continue; }

			const FTransform RelativeXform(A->GetActorRotation(),
				A->GetActorLocation() - Centroid,
				A->GetActorScale3D());
			if (!Db->Execute(
				TEXT("INSERT INTO collection_items "
				     "(collection_id, asset_id, transform_json, sort_order) "
				     "VALUES (?, ?, ?, ?)"),
				{
					B_Int(NewId),
					B_Int(AssetId),
					B_Text(TransformToJson(RelativeXform)),
					B_Int(SortOrder++)
				})) { return false; }
		}
		return true;
	});

	if (!bOk) { return 0; }
	UE_LOG(LogBlenderAssetBrowser, Log,
		TEXT("Collection '%s' created with %d items (id=%lld)."), *Name, Count, NewId);
	return NewId;
}

int32 FCollectionManager::SpawnIntoWorld(int64 CollectionId, UWorld* World,
	const FVector& BaseLocation, bool /*bAsBlueprintContainer*/)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady() || !World || CollectionId <= 0) { return 0; }
	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return 0; }

	struct FItem { FString MeshPath; FTransform Rel; };
	TArray<FItem> Items;

	Db->QueryRows(
		TEXT("SELECT a.asset_path, ci.transform_json "
		     "FROM collection_items ci "
		     "JOIN assets a ON a.id = ci.asset_id "
		     "WHERE ci.collection_id=? ORDER BY ci.sort_order"),
		{ B_Int(CollectionId) },
		[&Items](const BAB::FRow& R) -> bool
		{
			FItem I;
			I.MeshPath = R.GetText(0);
			I.Rel = TransformFromJson(R.GetText(1));
			Items.Add(MoveTemp(I));
			return true;
		});

	int32 Spawned = 0;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (const FItem& I : Items)
	{
		// Try to load the StaticMesh. We could broaden this to any spawnable
		// asset type later; for v1 only meshes are supported in collections.
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *I.MeshPath);
		if (!Mesh) { continue; }
		const FVector WorldLoc = BaseLocation + I.Rel.GetLocation();
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
			WorldLoc, I.Rel.Rotator(), P);
		if (!Actor) { continue; }
		Actor->SetActorScale3D(I.Rel.GetScale3D());
		if (Actor->GetStaticMeshComponent())
		{
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		}
		++Spawned;
	}
	UE_LOG(LogBlenderAssetBrowser, Log,
		TEXT("Collection %lld spawned %d actors at %s."),
		CollectionId, Spawned, *BaseLocation.ToString());
	return Spawned;
}

bool FCollectionManager::DeleteCollection(int64 CollectionId)
{
	if (!Subsystem.IsValid() || !Subsystem->IsReady() || CollectionId <= 0) { return false; }
	return Subsystem->GetDatabase()->Execute(
		TEXT("DELETE FROM collections WHERE id=?"),
		{ B_Int(CollectionId) });
}
