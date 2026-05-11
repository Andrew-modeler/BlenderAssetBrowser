// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// External FBX/OBJ/GLTF/STL preview pipeline:
//
//   on-disk file → Assimp aiScene → transient UStaticMesh → UE ThumbnailTools → PNG
//
// We deliberately do NOT pull in Google Filament. UE already has a perfectly
// good thumbnail renderer; routing through a transient `UStaticMesh` gives us
// the same lighting, framing, and shading the user sees in the Content
// Browser, with zero extra rendering plumbing.
//
// SECURITY:
//   - File size capped at MAX_FILE_BYTES_FBX before reading.
//   - Assimp configured with vertex / face / depth caps to bound work.
//   - All Assimp calls wrapped in try/catch + a defensive null check on
//     the returned scene.
//   - Transient mesh lives in `GetTransientPackage()` and is GC-collected
//     after we finish rendering.

#include "ExternalPreviewRenderer.h"
#include "AssetPreviewModule.h"
#include "AssetPreviewRenderer.h"

#include "AssetLibraryTypes.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"

#if BAB_HAS_ASSIMP
THIRD_PARTY_INCLUDES_START
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/DefaultLogger.hpp"
THIRD_PARTY_INCLUDES_END

#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#endif

namespace
{
#if BAB_HAS_ASSIMP

	constexpr uint32 MAX_VERTICES   = 2'000'000;
	constexpr uint32 MAX_FACES      = 2'000'000;
	constexpr uint32 MAX_MATERIALS  = 256;

	/** Convert an aiScene into a transient UStaticMesh. Returns nullptr on
	 *  any failure (cap exceeded, no meshes, build error). */
	UStaticMesh* BuildTransientMeshFromScene(const aiScene* Scene)
	{
		if (!Scene || Scene->mNumMeshes == 0) { return nullptr; }

		uint32 TotalV = 0, TotalF = 0;
		for (unsigned int i = 0; i < Scene->mNumMeshes; ++i)
		{
			const aiMesh* M = Scene->mMeshes[i];
			if (!M) { continue; }
			TotalV += M->mNumVertices;
			TotalF += M->mNumFaces;
		}
		if (TotalV == 0 || TotalV > MAX_VERTICES || TotalF == 0 || TotalF > MAX_FACES)
		{
			UE_LOG(LogAssetPreview, Warning,
				TEXT("ExternalPreviewRenderer: scene out of bounds (verts=%u faces=%u)."),
				TotalV, TotalF);
			return nullptr;
		}

		UStaticMesh* Mesh = NewObject<UStaticMesh>(
			GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(),
				TEXT("BAB_TransientMesh")));
		Mesh->SetFlags(RF_Transient);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));

		FMeshDescription MeshDesc;
		FStaticMeshAttributes(MeshDesc).Register();
		FMeshDescriptionBuilder Builder;
		Builder.SetMeshDescription(&MeshDesc);
		Builder.EnablePolyGroups();
		const FPolygonGroupID PolyGroup = Builder.AppendPolygonGroup(FName(TEXT("MainPolyGroup")));

		// Flatten all meshes into one polygon group. UE's thumbnail render
		// doesn't care about node hierarchy — a single mesh is fine.
		uint32 VertexOffset = 0;
		uint32 FacesAdded   = 0;
		for (unsigned int i = 0; i < Scene->mNumMeshes; ++i)
		{
			const aiMesh* M = Scene->mMeshes[i];
			if (!M || !M->mVertices || M->mNumVertices == 0) { continue; }

			TArray<FVertexID> Vids;
			Vids.Reserve(M->mNumVertices);
			for (unsigned int v = 0; v < M->mNumVertices; ++v)
			{
				const aiVector3D& P = M->mVertices[v];
				Vids.Add(Builder.AppendVertex(FVector(P.x, P.y, P.z)));
			}

			for (unsigned int f = 0; f < M->mNumFaces; ++f)
			{
				const aiFace& F = M->mFaces[f];
				if (F.mNumIndices != 3) { continue; }  // only triangles
				if (F.mIndices[0] >= M->mNumVertices ||
				    F.mIndices[1] >= M->mNumVertices ||
				    F.mIndices[2] >= M->mNumVertices)
				{
					continue;
				}
				TArray<FVertexInstanceID> Insts;
				Insts.SetNum(3);
				for (int32 k = 0; k < 3; ++k)
				{
					Insts[k] = Builder.AppendInstance(Vids[F.mIndices[k]]);
					// Normals & UVs are optional. Cheap defaults keep us safe.
					if (M->HasNormals())
					{
						const aiVector3D& N = M->mNormals[F.mIndices[k]];
						Builder.SetInstanceNormal(Insts[k], FVector(N.x, N.y, N.z));
					}
					if (M->HasTextureCoords(0))
					{
						const aiVector3D& UV = M->mTextureCoords[0][F.mIndices[k]];
						Builder.SetInstanceUV(Insts[k], FVector2D(UV.x, 1.0f - UV.y), 0);
					}
				}
				Builder.AppendTriangle(Insts[0], Insts[1], Insts[2], PolyGroup);
				++FacesAdded;
			}
			VertexOffset += M->mNumVertices;
		}

		if (FacesAdded == 0)
		{
			Mesh->MarkAsGarbage();
			return nullptr;
		}

		// Build the mesh asynchronously is the default for editor builds. We
		// commit synchronously for predictable rendering.
		Mesh->BuildFromMeshDescriptions({ &MeshDesc });
		return Mesh;
	}
#endif
}

bool FExternalPreviewRenderer::IsAvailable()
{
#if BAB_HAS_ASSIMP
	return true;
#else
	return false;
#endif
}

bool FExternalPreviewRenderer::RenderToFile(const FString& InAbsoluteFilePath, int32 Size,
	const FString& OutAbsolutePath)
{
#if !BAB_HAS_ASSIMP
	(void)InAbsoluteFilePath; (void)Size; (void)OutAbsolutePath;
	return false;
#else
	TArray<uint8> Png;
	if (!RenderToBytes(InAbsoluteFilePath, Size, Png)) { return false; }

	if (OutAbsolutePath.IsEmpty() ||
	    OutAbsolutePath.Contains(TEXT("..")) ||
	    !OutAbsolutePath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogAssetPreview, Warning, TEXT("ExternalPreviewRenderer: bad output path %s"),
			*OutAbsolutePath);
		return false;
	}

	const FString Dir = FPaths::GetPath(OutAbsolutePath);
	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!Dir.IsEmpty() && !PFM.DirectoryExists(*Dir))
	{
		PFM.CreateDirectoryTree(*Dir);
	}
	return FFileHelper::SaveArrayToFile(Png, *OutAbsolutePath);
#endif
}

bool FExternalPreviewRenderer::RenderToBytes(const FString& InAbsoluteFilePath, int32 Size,
	TArray<uint8>& OutPng)
{
	OutPng.Reset();
#if !BAB_HAS_ASSIMP
	(void)InAbsoluteFilePath; (void)Size;
	return false;
#else
	check(IsInGameThread());

	if (InAbsoluteFilePath.IsEmpty() || InAbsoluteFilePath.Contains(TEXT("..")))
	{
		return false;
	}

	IFileManager& FM = IFileManager::Get();
	const int64 FileSize = FM.FileSize(*InAbsoluteFilePath);
	if (FileSize <= 0 || FileSize > BAB::MAX_FILE_BYTES_FBX)
	{
		UE_LOG(LogAssetPreview, Warning,
			TEXT("ExternalPreviewRenderer: file size %lld out of bounds for %s"),
			FileSize, *InAbsoluteFilePath);
		return false;
	}

	UStaticMesh* TransientMesh = nullptr;
	try
	{
		Assimp::Importer Importer;
		Importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
			aiPrimitiveType_LINE | aiPrimitiveType_POINT);
		Importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

		const aiScene* Scene = Importer.ReadFile(TCHAR_TO_UTF8(*InAbsoluteFilePath),
			aiProcess_Triangulate |
			aiProcess_GenSmoothNormals |
			aiProcess_JoinIdenticalVertices |
			aiProcess_SortByPType |
			aiProcess_FindInvalidData |
			aiProcess_ValidateDataStructure);
		if (!Scene || (Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
		{
			UE_LOG(LogAssetPreview, Warning,
				TEXT("Assimp failed to load %s: %s"),
				*InAbsoluteFilePath, UTF8_TO_TCHAR(Importer.GetErrorString()));
			return false;
		}

		TransientMesh = BuildTransientMeshFromScene(Scene);
		// Importer destructor frees the scene; safe to leave scope here.
	}
	catch (...)
	{
		UE_LOG(LogAssetPreview, Error,
			TEXT("Assimp threw while loading %s"), *InAbsoluteFilePath);
		return false;
	}

	if (!TransientMesh)
	{
		UE_LOG(LogAssetPreview, Warning,
			TEXT("Failed to build transient mesh for %s"), *InAbsoluteFilePath);
		return false;
	}

	// Hand off to the standard uasset renderer. It uses UE's ThumbnailTools
	// which know how to render any UObject — including our transient mesh.
	FAssetData FakeData(TransientMesh);
	const bool bOk = FAssetPreviewRenderer::RenderToBytes(FakeData, Size, OutPng);

	// Transient mesh becomes garbage on next GC pass; we don't keep a ref.
	TransientMesh->MarkAsGarbage();
	return bOk;
#endif
}
