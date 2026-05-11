// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AITaggingModule.h"
#include "TagVocabulary.h"
#include "SigLIPInference.h"

DEFINE_LOG_CATEGORY(LogAITagging);

#define LOCTEXT_NAMESPACE "FAITaggingModule"

void FAITaggingModule::StartupModule()
{
	const bool bVocab = FTagVocabulary::LoadBuiltinVocabulary();
	const bool bEmb   = FTagVocabulary::LoadEmbeddings();
	// Try to bring up the ONNX runtime and load the SigLIP model. Failure
	// is silent — we just log the reason and leave inference disabled.
	const bool bRt = FSigLIPInference::EnsureRuntime();
	UE_LOG(LogAITagging, Log,
		TEXT("AITagging module started (vocabulary: %s; tag embeddings: %s; SigLIP inference: %s)."),
		bVocab ? TEXT("loaded") : TEXT("empty"),
		bEmb   ? TEXT("loaded") : TEXT("none"),
		bRt    ? TEXT("ready")  : TEXT("disabled"));
}

void FAITaggingModule::ShutdownModule()
{
	UE_LOG(LogAITagging, Log, TEXT("AITagging module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAITaggingModule, AITagging)
