// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Search engine. Supports:
//   - Free text   : "rock"  (FTS5 fuzzy across name/notes/source fields)
//   - Field filter: "type:StaticMesh"  "tag:foliage"  "source:fab"
//   - Numeric op  : "tris:<5000"  "rating:>=4"  "texture_res:>2048"
//   - Boolean     : "wood AND NOT wet"  "rock OR stone"
//
// SECURITY: query parser is a hand-written tokenizer, NOT eval. The parser
// produces a tree of typed nodes; the SQL builder turns nodes into parameterized
// WHERE clauses (no concat of user text). User input never reaches the DB as SQL.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "AssetLibraryTypes.h"
#include "AssetLibraryDatabase.h"

namespace BAB
{
	enum class ENodeKind : uint8
	{
		Text,            // free-text token, goes to FTS5
		Field,           // field:value  (e.g. "type:StaticMesh")
		FieldNumOp,      // field:OP:value  (e.g. "tris:<:5000")
		And, Or, Not
	};

	struct FQueryNode
	{
		ENodeKind Kind = ENodeKind::Text;
		FString   Field;        // for Field/FieldNumOp
		FString   Op;           // "<", "<=", ">", ">=", "="  (for FieldNumOp)
		FString   StrValue;     // for Text/Field
		double    NumValue = 0; // for FieldNumOp
		TArray<TSharedPtr<FQueryNode>> Children; // for And/Or/Not
	};

	/** Compiled, parameterized SQL. */
	struct FCompiledQuery
	{
		FString SQL;
		TArray<BAB::FBoundValue> Params;
	};
}

class UAssetLibrarySubsystem;

class BLENDERASSETBROWSER_API FSearchEngine
{
public:
	explicit FSearchEngine(UAssetLibrarySubsystem* InSubsystem);

	/** Parse user query string. Returns null on parse failure (logs reason). */
	TSharedPtr<BAB::FQueryNode> Parse(const FString& Query) const;

	/** Compile parsed tree into parameterized SQL targeting the `assets` table. */
	BAB::FCompiledQuery Compile(const TSharedPtr<BAB::FQueryNode>& Root, int32 LimitRows = 500) const;

	/** Run a query string end-to-end and return matching assets. */
	TArray<FAssetEntry> Search(const FString& Query, int32 LimitRows = 500) const;

	/** Whitelist of fields that user queries can filter on. */
	static const TSet<FString>& GetTextFields();
	static const TSet<FString>& GetNumericFields();

private:
	TWeakObjectPtr<UAssetLibrarySubsystem> Subsystem;
};
