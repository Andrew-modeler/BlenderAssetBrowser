// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AssetPreviewRenderer.h"
#include "AssetPreviewModule.h"

#include "ThumbnailRendering/ThumbnailManager.h"
#include "ObjectTools.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace
{
	// Sanity caps. We refuse to render arbitrary-sized thumbnails because
	// large render targets are an OOM vector.
	constexpr int32 MIN_SIZE = 32;
	constexpr int32 MAX_SIZE = 1024;

	/** Tries to load the object behind FAssetData. Returns null on any failure. */
	UObject* SafeLoadObject(const FAssetData& Data)
	{
		if (!Data.IsValid()) { return nullptr; }
		// LoadSynchronous is safe here — we're on game thread and the call
		// won't recurse into our plugin code.
		return Data.GetAsset();
	}

	/** Encode RGBA -> PNG using UE's IImageWrapper. Capped to 5 MB output. */
	bool EncodePng(const FObjectThumbnail& Thumb, TArray<uint8>& OutPng)
	{
		if (Thumb.GetImageWidth() <= 0 || Thumb.GetImageHeight() <= 0) { return false; }

		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> PngWrap = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!PngWrap.IsValid()) { return false; }

		const TArray<uint8>& Raw = Thumb.AccessImageData();
		if (Raw.Num() == 0) { return false; }

		if (!PngWrap->SetRaw(
				Raw.GetData(),
				Raw.Num(),
				Thumb.GetImageWidth(),
				Thumb.GetImageHeight(),
				ERGBFormat::BGRA,
				8))
		{
			return false;
		}

		// CompressionQuality 0 = default. Avoid uncompressed mode (large files).
		TArray64<uint8> Compressed = PngWrap->GetCompressed(0);
		if (Compressed.Num() == 0 || Compressed.Num() > 5 * 1024 * 1024)
		{
			UE_LOG(LogAssetPreview, Warning,
				TEXT("PNG output size %lld out of bounds (max 5 MB)"),
				static_cast<int64>(Compressed.Num()));
			return false;
		}

		OutPng.Reset();
		OutPng.Append(Compressed.GetData(), Compressed.Num());
		return true;
	}
}

bool FAssetPreviewRenderer::RenderToBytes(const FAssetData& Asset, int32 Size, TArray<uint8>& OutPng)
{
	check(IsInGameThread());

	if (Size < MIN_SIZE || Size > MAX_SIZE)
	{
		UE_LOG(LogAssetPreview, Warning, TEXT("Render size %d out of range [%d..%d]"),
			Size, MIN_SIZE, MAX_SIZE);
		return false;
	}

	UObject* Object = SafeLoadObject(Asset);
	if (!Object)
	{
		UE_LOG(LogAssetPreview, Warning, TEXT("Failed to load asset: %s"),
			*Asset.GetObjectPathString());
		return false;
	}

	// Render via UE's per-class ThumbnailRenderer. This is the same path the
	// Content Browser uses to make its own thumbnails, which guarantees:
	//   - StaticMesh / SkeletalMesh: 3D preview scene with built-in lighting
	//   - Material: sphere preview
	//   - Texture: flat with alpha
	//   - Blueprint: class icon + GeneratedClass capture
	FObjectThumbnail Thumb;
	ThumbnailTools::RenderThumbnail(
		Object,
		Size, Size,
		ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush,
		nullptr,
		&Thumb);

	if (Thumb.GetImageWidth() <= 0 || Thumb.GetImageHeight() <= 0)
	{
		UE_LOG(LogAssetPreview, Warning, TEXT("Thumbnail render produced empty image for %s"),
			*Asset.GetObjectPathString());
		return false;
	}

	if (!EncodePng(Thumb, OutPng))
	{
		UE_LOG(LogAssetPreview, Warning, TEXT("PNG encode failed for %s"),
			*Asset.GetObjectPathString());
		return false;
	}

	return true;
}

bool FAssetPreviewRenderer::RenderThumbnail(const FAssetData& Asset, int32 Size, const FString& OutAbsolutePath)
{
	check(IsInGameThread());

	if (OutAbsolutePath.IsEmpty()) { return false; }

	// Path safety: refuse traversal markers and require .png extension.
	if (OutAbsolutePath.Contains(TEXT("..")) || !OutAbsolutePath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogAssetPreview, Warning, TEXT("Refusing unsafe output path: %s"), *OutAbsolutePath);
		return false;
	}

	TArray<uint8> Png;
	if (!RenderToBytes(Asset, Size, Png)) { return false; }

	// Ensure parent directory.
	const FString Dir = FPaths::GetPath(OutAbsolutePath);
	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!Dir.IsEmpty() && !PFM.DirectoryExists(*Dir))
	{
		PFM.CreateDirectoryTree(*Dir);
	}

	if (!FFileHelper::SaveArrayToFile(Png, *OutAbsolutePath))
	{
		UE_LOG(LogAssetPreview, Warning, TEXT("Failed to write PNG: %s"), *OutAbsolutePath);
		return false;
	}

	UE_LOG(LogAssetPreview, Verbose, TEXT("Rendered thumbnail %dx%d -> %s"),
		Size, Size, *OutAbsolutePath);
	return true;
}
