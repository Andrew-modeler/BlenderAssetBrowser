// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "TagVocabulary.h"
#include "AITaggingModule.h"

#include "AssetLibraryTypes.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	static TArray<FString> GBuiltin;
	static TArray<FString> GUser;
	static TArray<float>   GEmbeddings;     // count*dim flat
	static int32           GEmbeddingDim = 0;
	static FCriticalSection GLock;

	bool IsValidTagText(const FString& Tag)
	{
		if (Tag.IsEmpty() || Tag.Len() > BAB::MAX_TAG_LEN) { return false; }
		for (TCHAR C : Tag)
		{
			const bool bOk = FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_') ||
			                 C == TEXT(' ') || C == TEXT('/');
			if (!bOk) { return false; }
		}
		return true;
	}
}

bool FTagVocabulary::LoadBuiltinVocabulary()
{
	FScopeLock Lock(&GLock);
	if (GBuiltin.Num() > 0) { return true; }

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlenderAssetBrowser"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogAITagging, Warning, TEXT("Plugin instance not found; cannot resolve tag vocabulary."));
		return false;
	}

	const FString JsonPath = Plugin->GetBaseDir() / TEXT("Resources/TagVocabulary/default_tags.json");
	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *JsonPath))
	{
		// Acceptable: file not shipped yet (we add it alongside the model).
		UE_LOG(LogAITagging, Log, TEXT("No default tag vocabulary at %s; using empty list."), *JsonPath);
		return false;
	}
	if (Contents.Len() > BAB::MAX_FILE_BYTES_JSON)
	{
		UE_LOG(LogAITagging, Warning, TEXT("Vocabulary file too large; refusing."));
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	auto Reader = TJsonReaderFactory<>::Create(Contents);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogAITagging, Warning, TEXT("Vocabulary JSON parse failed."));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
	if (!Root->TryGetArrayField(TEXT("tags"), Tags) || !Tags)
	{
		UE_LOG(LogAITagging, Warning, TEXT("Vocabulary JSON missing 'tags' array."));
		return false;
	}

	for (const auto& V : *Tags)
	{
		FString T = V->AsString();
		T.TrimStartAndEndInline();
		if (IsValidTagText(T)) { GBuiltin.Add(MoveTemp(T)); }
	}

	UE_LOG(LogAITagging, Log, TEXT("Loaded %d built-in tags."), GBuiltin.Num());
	return GBuiltin.Num() > 0;
}

const TArray<FString>& FTagVocabulary::GetBuiltinTags()
{
	FScopeLock Lock(&GLock);
	return GBuiltin;
}

bool FTagVocabulary::AddUserTag(const FString& Tag)
{
	FScopeLock Lock(&GLock);
	if (!IsValidTagText(Tag)) { return false; }
	GUser.AddUnique(Tag);
	return true;
}

TArray<FString> FTagVocabulary::GetAll()
{
	FScopeLock Lock(&GLock);
	TArray<FString> All;
	All.Reserve(GBuiltin.Num() + GUser.Num());
	All.Append(GBuiltin);
	All.Append(GUser);
	return All;
}

bool FTagVocabulary::IsEmbeddingsReady()
{
	FScopeLock Lock(&GLock);
	return GEmbeddingDim > 0 && GEmbeddings.Num() == GBuiltin.Num() * GEmbeddingDim;
}

const TArray<float>& FTagVocabulary::GetEmbeddingsFlat()
{
	FScopeLock Lock(&GLock);
	return GEmbeddings;
}

int32 FTagVocabulary::GetEmbeddingDim()
{
	FScopeLock Lock(&GLock);
	return GEmbeddingDim;
}

bool FTagVocabulary::LoadEmbeddings()
{
	FScopeLock Lock(&GLock);
	if (IsEmbeddingsReady()) { return true; }

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlenderAssetBrowser"));
	if (!Plugin.IsValid()) { return false; }

	const FString BinPath = Plugin->GetBaseDir() / TEXT("Resources/TagVocabulary/default_tag_embeddings.bin");
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *BinPath))
	{
		UE_LOG(LogAITagging, Log,
			TEXT("No tag embeddings at %s — AI tag suggestions disabled."),
			*BinPath);
		return false;
	}
	if (Bytes.Num() < 16) { return false; }
	if (Bytes.Num() > BAB::MAX_FILE_BYTES_JSON * 4)
	{
		// Cap at ~200 MB to be safe.
		UE_LOG(LogAITagging, Warning, TEXT("Embeddings file too large; refusing."));
		return false;
	}

	int32 Pos = 0;
	auto ReadInt32 = [&](int32& Out) -> bool
	{
		if (Pos + 4 > Bytes.Num()) { return false; }
		Out = static_cast<int32>(Bytes[Pos])
		    | (static_cast<int32>(Bytes[Pos + 1]) << 8)
		    | (static_cast<int32>(Bytes[Pos + 2]) << 16)
		    | (static_cast<int32>(Bytes[Pos + 3]) << 24);
		Pos += 4;
		return true;
	};

	// Magic "BAB1".
	if (!(Bytes[0] == 'B' && Bytes[1] == 'A' && Bytes[2] == 'B' && Bytes[3] == '1'))
	{
		UE_LOG(LogAITagging, Warning, TEXT("Embeddings file has wrong magic."));
		return false;
	}
	Pos = 4;

	int32 Count = 0, Dim = 0;
	if (!ReadInt32(Count) || !ReadInt32(Dim) || Count <= 0 || Count > 100000 ||
	    Dim <= 0 || Dim > 4096)
	{
		UE_LOG(LogAITagging, Warning, TEXT("Embeddings file has bad header."));
		return false;
	}

	// Security audit fix: bound the resulting allocation. Even with each
	// dimension under cap, the product Count*Dim can still grow large; we
	// limit total floats to 10 M (= 40 MB) which is plenty for a tag vocab.
	const int64 TotalFloats = static_cast<int64>(Count) * static_cast<int64>(Dim);
	if (TotalFloats > 10LL * 1024 * 1024)
	{
		UE_LOG(LogAITagging, Warning,
			TEXT("Embeddings file declares too many floats (%lld); refusing."), TotalFloats);
		return false;
	}

	// Skip tag names — we trust the vocabulary order from default_tags.json.
	// Cross-check counts: the loaded count should match our vocab size.
	for (int32 i = 0; i < Count; ++i)
	{
		int32 NameLen = 0;
		if (!ReadInt32(NameLen) || NameLen < 0 || NameLen > 256) { return false; }
		if (Pos + NameLen > Bytes.Num()) { return false; }
		Pos += NameLen;
	}

	const int32 VectorBytes = Count * Dim * sizeof(float);
	if (Pos + VectorBytes > Bytes.Num())
	{
		UE_LOG(LogAITagging, Warning, TEXT("Embeddings vector block truncated."));
		return false;
	}

	GEmbeddings.SetNumUninitialized(Count * Dim);
	FMemory::Memcpy(GEmbeddings.GetData(), Bytes.GetData() + Pos, VectorBytes);
	GEmbeddingDim = Dim;

	if (Count != GBuiltin.Num())
	{
		UE_LOG(LogAITagging, Warning,
			TEXT("Embeddings count %d != vocabulary count %d. Mismatched build."),
			Count, GBuiltin.Num());
	}
	UE_LOG(LogAITagging, Log,
		TEXT("Loaded %d tag embeddings (dim=%d) from %s."),
		Count, Dim, *BinPath);
	return true;
}
