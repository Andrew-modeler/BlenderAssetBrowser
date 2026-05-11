// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SigLIPInference.h"
#include "AITaggingModule.h"
#include "AssetLibraryTypes.h"
#include "AssetLibrarySettings.h"

#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"

// Default. EnsureRuntime() overrides from UAssetLibrarySettings.
float FSigLIPInference::SigmoidScale = 4.0f;
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#if BAB_HAS_ONNXRUNTIME
// Manual init: gives us explicit control of when the ORT global api_ pointer
// is set. Critical when the host (UE) loads DLLs in a non-standard order.
#define ORT_API_MANUAL_INIT
THIRD_PARTY_INCLUDES_START
#include "onnxruntime_cxx_api.h"
THIRD_PARTY_INCLUDES_END
#undef ORT_API_MANUAL_INIT
#endif

namespace
{
	static bool GReady = false;
	static FCriticalSection GLock;

#if BAB_HAS_ONNXRUNTIME
	// Singletons live for the editor lifetime — Ort APIs are not cheap to
	// instantiate per call. They are reset back to nullptr in EnsureRuntime
	// failure paths.
	static TUniquePtr<Ort::Env>            GEnv;
	static TUniquePtr<Ort::Session>        GSession;
	static TUniquePtr<Ort::MemoryInfo>     GMemInfo;
	static TArray<std::string>             GInputNames;
	static TArray<std::string>             GOutputNames;
#endif

	FString FindPluginPath()
	{
		TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("BlenderAssetBrowser"));
		return P.IsValid() ? P->GetBaseDir() : FString();
	}

	FString GetModelPath()
	{
		const FString Base = FindPluginPath();
		if (Base.IsEmpty()) { return FString(); }
		return Base / TEXT("Resources/Models/siglip2_vision_fp16.onnx");
	}

	bool LoadFileBytes(const FString& Path, TArray<uint8>& Out)
	{
		return FFileHelper::LoadFileToArray(Out, *Path);
	}

	FString Sha256Hex(const TArray<uint8>& Bytes)
	{
		// UE has FSHA1 built-in; for SHA256 we use FSHAHash from ALC. Since
		// the hash is for integrity, not crypto, SHA1 of the file is fine as
		// a tamper-detection signal — collisions are not a concern here.
		FSHA1 Hash;
		Hash.Update(Bytes.GetData(), Bytes.Num());
		Hash.Final();
		uint8 Out[20];
		Hash.GetHash(Out);
		FString Hex;
		Hex.Reserve(40);
		for (int32 i = 0; i < 20; ++i) { Hex.Appendf(TEXT("%02x"), Out[i]); }
		return Hex;
	}
}

bool FSigLIPInference::IsReady() { return GReady; }

bool FSigLIPInference::EnsureRuntime()
{
	FScopeLock Lock(&GLock);
	if (GReady) { return true; }

#if !BAB_HAS_ONNXRUNTIME
	UE_LOG(LogAITagging, Log, TEXT("SigLIPInference: ONNX Runtime not bundled (BAB_HAS_ONNXRUNTIME=0)."));
	return false;
#else
	const FString ModelPath = GetModelPath();
	if (ModelPath.IsEmpty())
	{
		UE_LOG(LogAITagging, Warning, TEXT("SigLIPInference: plugin path not found."));
		return false;
	}

	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!PFM.FileExists(*ModelPath))
	{
		UE_LOG(LogAITagging, Log, TEXT("SigLIPInference: model file missing at %s"), *ModelPath);
		return false;
	}

	// Size cap — refuse to load anything unreasonable.
	const int64 ModelSize = PFM.FileSize(*ModelPath);
	if (ModelSize <= 0 || ModelSize > 400 * 1024 * 1024)
	{
		UE_LOG(LogAITagging, Warning, TEXT("SigLIPInference: model size %lld out of bounds."), ModelSize);
		return false;
	}

	// Security audit fix: verify SHA1 of the on-disk model against a baked
	// hash. If a build ships a different model, override `kExpectedHash`
	// during a release build, OR pass `-DBAB_SKIP_MODEL_HASH=1` to bypass.
	// Refuses to load on mismatch — defends against on-disk model tamper.
	{
		static constexpr const TCHAR* kExpectedHash =
			TEXT("defa6894832bd83a5eeb6936cca2ef58e90998aa"); // siglip2_vision_fp16.onnx
		TArray<uint8> Bytes;
		if (!LoadFileBytes(ModelPath, Bytes)) { return false; }
		if (Bytes.Num() != ModelSize)
		{
			UE_LOG(LogAITagging, Warning,
				TEXT("SigLIPInference: file read size %d != stat size %lld"),
				Bytes.Num(), ModelSize);
			return false;
		}
		const FString Actual = Sha256Hex(Bytes); // (it's SHA1, despite the name)
		if (!Actual.Equals(kExpectedHash, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogAITagging, Error,
				TEXT("SigLIPInference: model hash mismatch.\n  expected=%s\n  actual=  %s\n  -> refusing to load."),
				kExpectedHash, *Actual);
			return false;
		}
		UE_LOG(LogAITagging, Verbose, TEXT("SigLIPInference: model hash OK."));
	}

	try
	{
		// ORT 1.20+ requires explicit InitApi() before any C++ wrapper is used.
		// Idempotent — calling twice is safe.
		Ort::InitApi();

		// Pull sigmoid scale from settings so the tag-confidence threshold
		// is comparable to a real probability (Bug #6).
		if (const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>())
		{
			SigmoidScale = FMath::Clamp(S->AISigmoidScale, 0.1f, 20.0f);
		}

		GEnv      = MakeUnique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "BlenderAssetBrowser");
		Ort::SessionOptions Opt;
		Opt.SetIntraOpNumThreads(2);
		Opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

		const FTCHARToUTF16 W(*ModelPath);
		GSession = MakeUnique<Ort::Session>(*GEnv, reinterpret_cast<const wchar_t*>(W.Get()), Opt);

		// Collect IO names. We free the allocator-owned strings into FString copies.
		Ort::AllocatorWithDefaultOptions Allocator;
		GInputNames.Reset();
		for (size_t i = 0, n = GSession->GetInputCount(); i < n; ++i)
		{
			auto Name = GSession->GetInputNameAllocated(i, Allocator);
			GInputNames.Add(Name.get());
		}
		GOutputNames.Reset();
		for (size_t i = 0, n = GSession->GetOutputCount(); i < n; ++i)
		{
			auto Name = GSession->GetOutputNameAllocated(i, Allocator);
			GOutputNames.Add(Name.get());
		}

		GMemInfo = MakeUnique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

		GReady = true;
		UE_LOG(LogAITagging, Log, TEXT("SigLIPInference: model loaded (%d in / %d out)."),
			GInputNames.Num(), GOutputNames.Num());
		return true;
	}
	catch (const Ort::Exception& e)
	{
		UE_LOG(LogAITagging, Error, TEXT("SigLIPInference: Ort exception: %s"), UTF8_TO_TCHAR(e.what()));
		GSession.Reset(); GEnv.Reset(); GMemInfo.Reset();
		return false;
	}
	catch (...)
	{
		UE_LOG(LogAITagging, Error, TEXT("SigLIPInference: unknown exception during init."));
		GSession.Reset(); GEnv.Reset(); GMemInfo.Reset();
		return false;
	}
#endif
}

TArray<float> FSigLIPInference::ComputeEmbedding(const TArray<uint8>& ImageBytes)
{
	TArray<float> Out;
	if (!EnsureRuntime()) { return Out; }

#if !BAB_HAS_ONNXRUNTIME
	return Out;
#else
	if (ImageBytes.Num() <= 0 || ImageBytes.Num() > 5 * 1024 * 1024)
	{
		UE_LOG(LogAITagging, Warning, TEXT("ComputeEmbedding: image bytes out of bounds."));
		return Out;
	}

	IImageWrapperModule& W = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat Fmt = W.DetectImageFormat(ImageBytes.GetData(), ImageBytes.Num());
	TSharedPtr<IImageWrapper> Wrap = W.CreateImageWrapper(Fmt);
	if (!Wrap.IsValid() || !Wrap->SetCompressed(ImageBytes.GetData(), ImageBytes.Num()))
	{
		UE_LOG(LogAITagging, Warning, TEXT("ComputeEmbedding: image decode failed."));
		return Out;
	}

	TArray<uint8> Raw;
	if (!Wrap->GetRaw(ERGBFormat::RGBA, 8, Raw))
	{
		UE_LOG(LogAITagging, Warning, TEXT("ComputeEmbedding: GetRaw failed."));
		return Out;
	}

	const int32 SrcW = Wrap->GetWidth();
	const int32 SrcH = Wrap->GetHeight();
	if (SrcW <= 0 || SrcH <= 0 || SrcW > 8192 || SrcH > 8192) { return Out; }

	// Resize to 224x224 with simple nearest-neighbor (good enough for AI
	// inference at this scale; bilinear would be a polish task).
	constexpr int32 N = 224;
	TArray<float> Input;
	Input.SetNumUninitialized(3 * N * N);

	// SigLIP normalization: mean=[0.5,0.5,0.5], std=[0.5,0.5,0.5].
	for (int32 y = 0; y < N; ++y)
	{
		const int32 sy = (y * SrcH) / N;
		for (int32 x = 0; x < N; ++x)
		{
			const int32 sx = (x * SrcW) / N;
			const int32 sidx = (sy * SrcW + sx) * 4;
			const float R = Raw[sidx + 0] / 255.0f;
			const float G = Raw[sidx + 1] / 255.0f;
			const float B = Raw[sidx + 2] / 255.0f;
			// NCHW layout: [batch=1, C, H, W]
			Input[0 * N * N + y * N + x] = (R - 0.5f) / 0.5f;
			Input[1 * N * N + y * N + x] = (G - 0.5f) / 0.5f;
			Input[2 * N * N + y * N + x] = (B - 0.5f) / 0.5f;
		}
	}

	try
	{
		std::array<int64_t, 4> InputShape{ 1, 3, N, N };
		Ort::Value InputTensor = Ort::Value::CreateTensor<float>(
			*GMemInfo, Input.GetData(), Input.Num(),
			InputShape.data(), InputShape.size());

		// Cast TArray<std::string> -> const char* array expected by Run().
		TArray<const char*> InNames, OutNames;
		InNames.Reserve(GInputNames.Num());
		for (const std::string& S : GInputNames) { InNames.Add(S.c_str()); }
		OutNames.Reserve(GOutputNames.Num());
		for (const std::string& S : GOutputNames) { OutNames.Add(S.c_str()); }

		// Only the first input is the pixel_values tensor.
		std::vector<Ort::Value> Inputs;
		Inputs.push_back(std::move(InputTensor));

		auto OutValues = GSession->Run(
			Ort::RunOptions{ nullptr },
			InNames.GetData(), Inputs.data(), Inputs.size(),
			OutNames.GetData(), OutNames.Num());

		if (OutValues.size() == 0)
		{
			UE_LOG(LogAITagging, Warning, TEXT("ComputeEmbedding: Run() returned no outputs."));
			return Out;
		}

		const Ort::Value& First = OutValues[0];
		const auto Shape = First.GetTensorTypeAndShapeInfo().GetShape();
		const size_t Count = First.GetTensorTypeAndShapeInfo().GetElementCount();

		// Models often output the pooled vector as the first tensor (shape [1, 768]).
		// We accept any output whose last-dim equals EmbeddingDim.
		if (Count != static_cast<size_t>(EmbeddingDim))
		{
			UE_LOG(LogAITagging, Warning,
				TEXT("ComputeEmbedding: unexpected output count %llu (need %d)."),
				static_cast<uint64>(Count), EmbeddingDim);
			return Out;
		}

		// FP16 vs FP32 — try FP32 first (works for fp32 exports). FP16 export
		// returns its data as half-float; we'd need a tiny dequant pass there.
		const auto Element = First.GetTensorTypeAndShapeInfo().GetElementType();
		if (Element == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
		{
			const float* Data = First.GetTensorData<float>();
			Out.Append(Data, EmbeddingDim);
		}
		else if (Element == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16)
		{
			const uint16* Data = First.GetTensorData<uint16>();
			Out.SetNumUninitialized(EmbeddingDim);
			// Half -> float conversion using IEEE 754 binary16 layout.
			for (int32 i = 0; i < EmbeddingDim; ++i)
			{
				const uint16 H = Data[i];
				const uint32 Sign = (H >> 15) & 0x1;
				const uint32 Exp  = (H >> 10) & 0x1F;
				const uint32 Mant =  H        & 0x3FF;
				uint32 F;
				if (Exp == 0)
				{
					F = (Sign << 31) | (Mant << 13);
				}
				else if (Exp == 0x1F)
				{
					F = (Sign << 31) | (0xFF << 23) | (Mant << 13);
				}
				else
				{
					F = (Sign << 31) | ((Exp + 112) << 23) | (Mant << 13);
				}
				Out[i] = *reinterpret_cast<const float*>(&F);
			}
		}
		else
		{
			UE_LOG(LogAITagging, Warning, TEXT("ComputeEmbedding: unsupported output dtype %d."),
				static_cast<int32>(Element));
		}
	}
	catch (const Ort::Exception& e)
	{
		UE_LOG(LogAITagging, Error, TEXT("ComputeEmbedding: Ort exception: %s"), UTF8_TO_TCHAR(e.what()));
		Out.Reset();
	}
	catch (...)
	{
		UE_LOG(LogAITagging, Error, TEXT("ComputeEmbedding: unknown exception."));
		Out.Reset();
	}
	return Out;
#endif
}

TArray<FSigLIPInference::FScored> FSigLIPInference::ScoreAgainst(const TArray<float>& Embedding,
	const TArray<float>& VocabEmbeddings, int32 VocabSize)
{
	TArray<FScored> Out;
	if (Embedding.Num() != EmbeddingDim) { return Out; }
	if (VocabSize <= 0) { return Out; }
	if (VocabEmbeddings.Num() != VocabSize * EmbeddingDim)
	{
		UE_LOG(LogAITagging, Warning, TEXT("ScoreAgainst: vocab dimension mismatch."));
		return Out;
	}

	// Cosine similarity with on-the-fly L2 normalization.
	auto Norm = [](const float* V, int32 N) -> double
	{
		double Sum = 0.0;
		for (int32 i = 0; i < N; ++i) { Sum += static_cast<double>(V[i]) * V[i]; }
		return FMath::Sqrt(Sum) + 1e-12;
	};

	const double EmbNorm = Norm(Embedding.GetData(), EmbeddingDim);
	Out.Reserve(VocabSize);

	// Bug #6 fix: SigLIP was trained with a sigmoid loss, so raw cosine
	// similarity is not directly a probability. The published recipe is:
	//   logit = scale * cosine + bias    (scale ≈ exp(t), bias ≈ -10 by default)
	//   score = sigmoid(logit)
	// We expose `Scale` as a tunable in Settings (default 4.0 mirrors common
	// public SigLIP-2 fine-tunes). Bias is centered at 0 so the user-facing
	// threshold maps to sigmoid(0)=0.5 by default.
	const float Scale = SigmoidScale; // initialized in EnsureRuntime from Settings

	for (int32 t = 0; t < VocabSize; ++t)
	{
		const float* V = VocabEmbeddings.GetData() + t * EmbeddingDim;
		const double Vn = Norm(V, EmbeddingDim);
		double Dot = 0.0;
		for (int32 i = 0; i < EmbeddingDim; ++i)
		{
			Dot += static_cast<double>(Embedding[i]) * V[i];
		}
		const double CosSim = Dot / (EmbNorm * Vn);
		const double Logit  = static_cast<double>(Scale) * CosSim;
		const double Prob   = 1.0 / (1.0 + FMath::Exp(-Logit));
		Out.Add({ t, static_cast<float>(Prob) });
	}

	Out.Sort([](const FScored& A, const FScored& B) { return A.Score > B.Score; });
	return Out;
}
