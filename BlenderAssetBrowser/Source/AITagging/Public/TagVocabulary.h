// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Tag vocabulary for AI tagging. Holds the built-in tag list plus user-added
// tags, and (in a later phase) the pre-computed SigLIP2 text-encoder embeddings
// loaded from Resources/TagVocabulary/default_tag_embeddings.bin.

#pragma once

#include "CoreMinimal.h"

class AITAGGING_API FTagVocabulary
{
public:
	/** Load the bundled default vocabulary JSON. Returns true if loaded. */
	static bool LoadBuiltinVocabulary();

	/** Load pre-computed text-encoder embeddings from `default_tag_embeddings.bin`.
	 *  Format: magic "BAB1" + int32 count + int32 dim + (per-tag name) + flat float32 vectors.
	 *  Returns true if embeddings successfully loaded (one row per built-in tag).
	 *  Missing or malformed file is non-fatal — IsEmbeddingsReady() will return false. */
	static bool LoadEmbeddings();

	/** True if `LoadEmbeddings` succeeded. */
	static bool IsEmbeddingsReady();

	/** Built-in tags from default_tags.json (or empty if not loaded yet). */
	static const TArray<FString>& GetBuiltinTags();

	/** Flat embeddings buffer (count*dim floats). Empty if not loaded. */
	static const TArray<float>& GetEmbeddingsFlat();

	/** Dimension per embedding (768 for SigLIP2 base). 0 if not loaded. */
	static int32 GetEmbeddingDim();

	/** Adds a user-defined tag. Truncated/sanitized; rejected if invalid. */
	static bool AddUserTag(const FString& Tag);

	/** All known tags: builtin ∪ user-added. */
	static TArray<FString> GetAll();
};
