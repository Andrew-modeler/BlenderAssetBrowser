// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// ONNX Runtime wrapper for SigLIP2 vision-encoder inference.
//
// Lifecycle:
//   1. EnsureRuntime() lazily loads the bundled `siglip2_vision_fp16.onnx`
//      after verifying its SHA-1 against a hash baked into the binary
//      (tamper / corruption detection; not crypto-grade).
//   2. ComputeEmbedding(thumbnailPng) -> 768-dim float vector.
//   3. ScoreAgainstVocabulary(embedding, vocab_embeddings) -> tag scores
//      using cosine similarity + the sigmoid scaling SigLIP was trained with.
//
// SECURITY:
//   - Model hash verified at load.
//   - Image bytes capped at 5 MB before decoding (matches our PNG cap).
//   - All ONNX Runtime calls in try/catch.
//   - Embedding output validated for dimension before any cosine math.

#pragma once

#include "CoreMinimal.h"

class AITAGGING_API FSigLIPInference
{
public:
	/** Load model + ONNX Runtime if available. Returns true on success. */
	static bool EnsureRuntime();

	/** True if a previously-successful EnsureRuntime call left the model live. */
	static bool IsReady();

	/**
	 * Run the vision encoder on PNG/JPEG bytes. Returns 768-dim float vector
	 * (or an empty array on failure).
	 */
	static TArray<float> ComputeEmbedding(const TArray<uint8>& ImageBytes);

	/**
	 * Cosine similarity of `Embedding` against each row in `VocabEmbeddings`,
	 * each row being 768 floats. Returns an array of {tagIndex, score} pairs
	 * sorted descending by score.
	 */
	struct FScored { int32 TagIndex = 0; float Score = 0.f; };
	static TArray<FScored> ScoreAgainst(const TArray<float>& Embedding,
	                                    const TArray<float>& VocabEmbeddings,
	                                    int32 VocabSize);

	/** Embedding dimension for SigLIP2 base patch16-224. */
	static constexpr int32 EmbeddingDim = 768;

	/** Sigmoid scale applied to raw cosine similarity to produce a calibrated
	 *  probability. Initialised from Settings at EnsureRuntime() time. */
	static float SigmoidScale;
};
