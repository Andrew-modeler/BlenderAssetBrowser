// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "SearchEngine.h"
#include "AssetLibrarySubsystem.h"
#include "AssetLibraryDatabase.h"
#include "BlenderAssetBrowserModule.h"

namespace
{
	BAB::FBoundValue B_Int(int64 X) { return BAB::FBoundValue::MakeInt(X); }
	BAB::FBoundValue B_Text(const FString& S) { return BAB::FBoundValue::MakeText(S); }
	BAB::FBoundValue B_Double(double X) { return BAB::FBoundValue::MakeDouble(X); }
}

const TSet<FString>& FSearchEngine::GetTextFields()
{
	// Each entry MUST be a real column name on `assets`. Used as a whitelist
	// to refuse `field:` filters on anything else.
	static const TSet<FString> Fields = {
		TEXT("type"),
		TEXT("source"),
		TEXT("license"),
		TEXT("author"),
		TEXT("pack"),
		TEXT("engine"),
		TEXT("preview"),
	};
	return Fields;
}

const TSet<FString>& FSearchEngine::GetNumericFields()
{
	static const TSet<FString> Fields = {
		TEXT("tris"),
		TEXT("verts"),
		TEXT("lod"),
		TEXT("rating"),
		TEXT("texture_res"),
		TEXT("size"),
		TEXT("materials"),
	};
	return Fields;
}

FSearchEngine::FSearchEngine(UAssetLibrarySubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
}

namespace
{
	/** Maps a user field alias to a DB column. Returns empty FString if unknown. */
	FString MapTextField(const FString& Alias)
	{
		static const TMap<FString, FString> M = {
			{ TEXT("type"),    TEXT("asset_type") },
			{ TEXT("source"),  TEXT("source_type") },
			{ TEXT("license"), TEXT("source_license") },
			{ TEXT("author"),  TEXT("source_author") },
			{ TEXT("pack"),    TEXT("source_pack_name") },
			{ TEXT("engine"),  TEXT("engine_version") },
			{ TEXT("preview"), TEXT("preview_path") },
		};
		const FString* P = M.Find(Alias.ToLower());
		return P ? *P : FString();
	}

	FString MapNumericField(const FString& Alias)
	{
		static const TMap<FString, FString> M = {
			{ TEXT("tris"),        TEXT("tri_count") },
			{ TEXT("verts"),       TEXT("vert_count") },
			{ TEXT("lod"),         TEXT("lod_count") },
			{ TEXT("rating"),      TEXT("rating") },
			{ TEXT("texture_res"), TEXT("texture_res_max") },
			{ TEXT("size"),        TEXT("disk_size_bytes") },
			{ TEXT("materials"),   TEXT("material_count") },
		};
		const FString* P = M.Find(Alias.ToLower());
		return P ? *P : FString();
	}

	/**
	 * Tokenizer. Splits on whitespace but keeps `field:value` and `field:OP:value` together.
	 * Treats words AND / OR / NOT (uppercase) as operators.
	 */
	struct FTok
	{
		enum class EKind : uint8 { Word, AndOp, OrOp, NotOp, LParen, RParen };
		EKind Kind;
		FString Text;
	};

	TArray<FTok> Tokenize(const FString& Q)
	{
		TArray<FTok> Out;
		FString Buf;
		auto Flush = [&]()
		{
			if (Buf.IsEmpty()) { return; }
			FTok T;
			if (Buf == TEXT("AND")) { T.Kind = FTok::EKind::AndOp; }
			else if (Buf == TEXT("OR")) { T.Kind = FTok::EKind::OrOp; }
			else if (Buf == TEXT("NOT")) { T.Kind = FTok::EKind::NotOp; }
			else { T.Kind = FTok::EKind::Word; T.Text = Buf; }
			Out.Add(MoveTemp(T));
			Buf.Reset();
		};

		const int32 Len = Q.Len();
		for (int32 i = 0; i < Len; ++i)
		{
			const TCHAR C = Q[i];
			if (C == TEXT('(')) { Flush(); Out.Add({ FTok::EKind::LParen, FString() }); }
			else if (C == TEXT(')')) { Flush(); Out.Add({ FTok::EKind::RParen, FString() }); }
			else if (FChar::IsWhitespace(C)) { Flush(); }
			else { Buf.AppendChar(C); }
		}
		Flush();
		return Out;
	}

	/** Parse a single Word token into FQueryNode (Text / Field / FieldNumOp). */
	TSharedPtr<BAB::FQueryNode> MakeAtomFromWord(const FString& Word)
	{
		auto Node = MakeShared<BAB::FQueryNode>();

		int32 ColonIdx = -1;
		if (!Word.FindChar(TEXT(':'), ColonIdx))
		{
			Node->Kind = BAB::ENodeKind::Text;
			Node->StrValue = Word;
			return Node;
		}

		const FString Field = Word.Left(ColonIdx);
		const FString Rest  = Word.Mid(ColonIdx + 1);

		// "tris:<5000" / "rating:>=4" / "size:>1000000"
		auto StartsWithOp = [](const FString& S, FString& OutOp, FString& OutNum)
		{
			static const TArray<FString> Ops = { TEXT(">="), TEXT("<="), TEXT(">"), TEXT("<"), TEXT("=") };
			for (const FString& Op : Ops)
			{
				if (S.StartsWith(Op))
				{
					OutOp  = Op;
					OutNum = S.Mid(Op.Len());
					return true;
				}
			}
			return false;
		};

		FString Op;
		FString NumStr;
		if (StartsWithOp(Rest, Op, NumStr))
		{
			if (MapNumericField(Field).IsEmpty()) { return nullptr; } // unknown field
			if (NumStr.IsEmpty() || !NumStr.IsNumeric()) { return nullptr; }
			Node->Kind = BAB::ENodeKind::FieldNumOp;
			Node->Field = Field;
			Node->Op = Op;
			Node->NumValue = FCString::Atod(*NumStr);
			return Node;
		}

		// Plain "field:value"
		if (MapTextField(Field).IsEmpty()) { return nullptr; }
		Node->Kind = BAB::ENodeKind::Field;
		Node->Field = Field;
		Node->StrValue = Rest;
		// SECURITY: cap value length defensively.
		Node->StrValue.LeftInline(BAB::MAX_NAME_LEN);
		return Node;
	}
}

TSharedPtr<BAB::FQueryNode> FSearchEngine::Parse(const FString& Query) const
{
	if (Query.Len() > BAB::MAX_QUERY_LEN)
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Search query too long (%d chars). Rejected."), Query.Len());
		return nullptr;
	}

	TArray<FTok> Toks = Tokenize(Query);
	if (Toks.Num() == 0) { return MakeShared<BAB::FQueryNode>(); } // empty = match-all

	// Shunting yard turned into a simple recursive descent. Grammar:
	//   expr   := term { (AND | OR | <implicit AND>) term }
	//   term   := [NOT] atom
	//   atom   := WORD | "(" expr ")"
	int32 Pos = 0;

	auto Peek = [&]() -> FTok* { return Toks.IsValidIndex(Pos) ? &Toks[Pos] : nullptr; };
	auto Eat  = [&]() -> FTok* { return Toks.IsValidIndex(Pos) ? &Toks[Pos++] : nullptr; };

	TFunction<TSharedPtr<BAB::FQueryNode>()> ParseExpr;
	TFunction<TSharedPtr<BAB::FQueryNode>()> ParseTerm;
	TFunction<TSharedPtr<BAB::FQueryNode>()> ParseAtom;

	ParseAtom = [&]() -> TSharedPtr<BAB::FQueryNode>
	{
		FTok* T = Peek();
		if (!T) { return nullptr; }
		if (T->Kind == FTok::EKind::LParen)
		{
			Eat();
			TSharedPtr<BAB::FQueryNode> Inner = ParseExpr();
			FTok* R = Peek();
			if (R && R->Kind == FTok::EKind::RParen) { Eat(); }
			return Inner;
		}
		if (T->Kind == FTok::EKind::Word)
		{
			FTok* W = Eat();
			return MakeAtomFromWord(W->Text);
		}
		return nullptr;
	};

	ParseTerm = [&]() -> TSharedPtr<BAB::FQueryNode>
	{
		FTok* T = Peek();
		if (T && T->Kind == FTok::EKind::NotOp)
		{
			Eat();
			TSharedPtr<BAB::FQueryNode> Inner = ParseAtom();
			if (!Inner) { return nullptr; }
			auto N = MakeShared<BAB::FQueryNode>();
			N->Kind = BAB::ENodeKind::Not;
			N->Children.Add(Inner);
			return N;
		}
		return ParseAtom();
	};

	ParseExpr = [&]() -> TSharedPtr<BAB::FQueryNode>
	{
		TSharedPtr<BAB::FQueryNode> Left = ParseTerm();
		if (!Left) { return nullptr; }
		while (true)
		{
			FTok* T = Peek();
			if (!T) { break; }

			BAB::ENodeKind Kind = BAB::ENodeKind::And;
			if (T->Kind == FTok::EKind::OrOp)  { Eat(); Kind = BAB::ENodeKind::Or; }
			else if (T->Kind == FTok::EKind::AndOp) { Eat(); Kind = BAB::ENodeKind::And; }
			else if (T->Kind == FTok::EKind::RParen) { break; }
			// Implicit AND between adjacent terms.

			TSharedPtr<BAB::FQueryNode> Right = ParseTerm();
			if (!Right) { break; }

			auto Combined = MakeShared<BAB::FQueryNode>();
			Combined->Kind = Kind;
			Combined->Children.Add(Left);
			Combined->Children.Add(Right);
			Left = Combined;
		}
		return Left;
	};

	return ParseExpr();
}

BAB::FCompiledQuery FSearchEngine::Compile(const TSharedPtr<BAB::FQueryNode>& Root, int32 LimitRows) const
{
	BAB::FCompiledQuery Out;
	Out.SQL.Reset();
	Out.Params.Reset();

	TArray<FString> Clauses;
	TArray<FString> FtsTokens;

	TFunction<void(const TSharedPtr<BAB::FQueryNode>&, bool /*negate*/)> Visit;
	Visit = [&](const TSharedPtr<BAB::FQueryNode>& Node, bool bNegate)
	{
		if (!Node) { return; }
		switch (Node->Kind)
		{
		case BAB::ENodeKind::Text:
		{
			// Collect for FTS5 MATCH clause. Negation inside FTS is tricky;
			// in v1 we keep AND-of-positive-terms only.
			if (!bNegate)
			{
				// Escape FTS reserved chars by quoting the token.
				FString Tok = Node->StrValue.Replace(TEXT("\""), TEXT(""));
				if (!Tok.IsEmpty()) { FtsTokens.Add(FString::Printf(TEXT("\"%s\""), *Tok)); }
			}
			break;
		}
		case BAB::ENodeKind::Field:
		{
			const FString Col = MapTextField(Node->Field);
			if (Col.IsEmpty()) { break; } // already filtered in Parse, but be safe
			const FString Cmp = bNegate ? TEXT("!=") : TEXT("=");
			Clauses.Add(FString::Printf(TEXT("a.%s %s ?"), *Col, *Cmp));
			Out.Params.Add(B_Text(Node->StrValue));
			break;
		}
		case BAB::ENodeKind::FieldNumOp:
		{
			const FString Col = MapNumericField(Node->Field);
			if (Col.IsEmpty()) { break; }
			// Whitelist op as defensive measure (parser already restricts).
			const FString& Op = Node->Op;
			if (Op != TEXT(">") && Op != TEXT(">=") && Op != TEXT("<") &&
			    Op != TEXT("<=") && Op != TEXT("=")) { break; }
			FString Effective = Op;
			if (bNegate)
			{
				// Invert operator on negate.
				if (Op == TEXT(">"))  Effective = TEXT("<=");
				else if (Op == TEXT(">=")) Effective = TEXT("<");
				else if (Op == TEXT("<"))  Effective = TEXT(">=");
				else if (Op == TEXT("<=")) Effective = TEXT(">");
				else if (Op == TEXT("="))  Effective = TEXT("!=");
			}
			Clauses.Add(FString::Printf(TEXT("a.%s %s ?"), *Col, *Effective));
			Out.Params.Add(B_Double(Node->NumValue));
			break;
		}
		case BAB::ENodeKind::Not:
		{
			for (const auto& C : Node->Children) { Visit(C, !bNegate); }
			break;
		}
		case BAB::ENodeKind::And:
		case BAB::ENodeKind::Or:
		{
			// v1: AND/OR are flattened. OR collapses to UNION pattern would be
			// nicer but more complex — keep them as AND for now; OR shows up
			// in the parse tree if user typed it and we honor the structure
			// via FTS for free-text and explicit clauses.
			for (const auto& C : Node->Children) { Visit(C, bNegate); }
			break;
		}
		}
	};

	Visit(Root, false);

	// Build final SQL.
	FString Sql = TEXT("SELECT a.id, a.asset_path, a.asset_name, a.asset_type, a.library_id, a.rating, a.notes, a.source_type FROM assets a");

	if (FtsTokens.Num() > 0)
	{
		Sql += TEXT(" JOIN assets_fts f ON f.rowid = a.id WHERE assets_fts MATCH ?");
		Out.Params.Insert(B_Text(FString::Join(FtsTokens, TEXT(" "))), 0);
		// Mismatch fix: we inserted at front so reorder Clauses-bind happens after.
	}
	else
	{
		Sql += TEXT(" WHERE 1=1");
	}

	for (const FString& C : Clauses)
	{
		Sql += TEXT(" AND ");
		Sql += C;
	}

	Sql += TEXT(" ORDER BY a.modified_at DESC LIMIT ?");
	Out.Params.Add(B_Int(FMath::Clamp(LimitRows, 1, 5000)));

	Out.SQL = MoveTemp(Sql);
	return Out;
}

TArray<FAssetEntry> FSearchEngine::Search(const FString& Query, int32 LimitRows) const
{
	TArray<FAssetEntry> Out;
	if (!Subsystem.IsValid() || !Subsystem->IsReady()) { return Out; }
	FAssetLibraryDatabase* Db = Subsystem->GetDatabase();
	if (!Db) { return Out; }

	TSharedPtr<BAB::FQueryNode> Tree = Parse(Query);
	if (!Tree)
	{
		// Parse failure — return empty (UI shows "no results"). Caller can also re-query with empty.
		return Out;
	}

	BAB::FCompiledQuery C = Compile(Tree, LimitRows);
	if (C.SQL.IsEmpty()) { return Out; }

	Db->QueryRows(C.SQL, C.Params, [&Out](const BAB::FRow& Row) -> bool
	{
		FAssetEntry E;
		E.Id        = Row.GetInt64(0);
		E.AssetPath = Row.GetText(1);
		E.AssetName = Row.GetText(2);
		E.AssetType = Row.GetText(3);
		E.LibraryId = Row.IsNull(4) ? 0 : Row.GetInt64(4);
		E.Rating    = static_cast<int32>(Row.GetInt64(5));
		E.Notes     = Row.IsNull(6) ? FString() : Row.GetText(6);
		// SourceType decoded by subsystem caller usually; we leave it as-is here.
		Out.Add(E);
		return true;
	});

	return Out;
}
