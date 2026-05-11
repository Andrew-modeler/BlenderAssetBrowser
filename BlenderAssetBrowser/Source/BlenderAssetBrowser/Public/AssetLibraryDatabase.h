// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Thin SQLite wrapper. ALL queries use prepared statements + bound parameters.
// String concatenation into SQL is FORBIDDEN — see threat model in CHECKLIST.md.
//
// Thread model: SQLite is opened in serialized mode (THREADSAFE=2) but we
// also gate every call through `Mutex`. Treat the DB as a single-writer queue.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Templates/UniquePtr.h"

struct sqlite3;
struct sqlite3_stmt;

namespace BAB
{
	/** Bound parameter: discriminated union of (text|int64|double|null|blob). */
	struct FBoundValue
	{
		enum class EType : uint8 { Null, Int64, Double, Text, Blob };

		EType Type = EType::Null;
		int64 IntVal = 0;
		double DoubleVal = 0.0;
		FString TextVal;
		TArray<uint8> BlobVal;

		static FBoundValue MakeNull()                  { FBoundValue V; V.Type = EType::Null;   return V; }
		static FBoundValue MakeInt(int64 X)            { FBoundValue V; V.Type = EType::Int64;  V.IntVal = X; return V; }
		static FBoundValue MakeDouble(double X)        { FBoundValue V; V.Type = EType::Double; V.DoubleVal = X; return V; }
		static FBoundValue MakeText(const FString& S)  { FBoundValue V; V.Type = EType::Text;   V.TextVal = S; return V; }
		static FBoundValue MakeBlob(const TArray<uint8>& B) { FBoundValue V; V.Type = EType::Blob; V.BlobVal = B; return V; }
	};

	/** Row visitor invoked per result row. Index/name accessors below. */
	struct BLENDERASSETBROWSER_API FRow
	{
		struct sqlite3_stmt* Stmt = nullptr;

		int32 ColumnCount() const;
		FString GetText(int32 ColIdx) const;
		int64 GetInt64(int32 ColIdx) const;
		double GetDouble(int32 ColIdx) const;
		bool IsNull(int32 ColIdx) const;
		TArray<uint8> GetBlob(int32 ColIdx) const;
	};
}

/**
 * Hardened SQLite wrapper. NEVER pass untrusted input through `RawExecute`.
 * Prefer the typed helpers (Execute, QueryRows, Transaction).
 */
class BLENDERASSETBROWSER_API FAssetLibraryDatabase
{
public:
	FAssetLibraryDatabase();
	~FAssetLibraryDatabase();

	FAssetLibraryDatabase(const FAssetLibraryDatabase&) = delete;
	FAssetLibraryDatabase& operator=(const FAssetLibraryDatabase&) = delete;

	/** Open or create a DB file at `InAbsolutePath`. Validates the path is non-empty
	 *  and inside `InAllowedRoot` (caller guarantees this is a trusted writable area).
	 *  Returns false if path is unsafe or DB cannot be opened. */
	bool Open(const FString& InAbsolutePath, const FString& InAllowedRoot);

	/** Close the connection. Idempotent. */
	void Close();

	bool IsOpen() const { return Connection != nullptr; }

	/** Execute a prepared statement with bound parameters. Returns true on success.
	 *  `Sql` MUST be a constant string literal or hardcoded; bound params are the
	 *  ONLY way to inject runtime values. */
	bool Execute(const FString& Sql, const TArray<BAB::FBoundValue>& Params = {});

	/** Run a SELECT-style query and call `RowFn` for each row. Returns true on success. */
	bool QueryRows(const FString& Sql,
	               const TArray<BAB::FBoundValue>& Params,
	               TFunctionRef<bool(const BAB::FRow& Row)> RowFn);

	/** Last-inserted rowid (0 if none for current connection). */
	int64 LastInsertRowId() const;

	/** BEGIN/COMMIT/ROLLBACK around `Body`. If `Body` returns false or throws,
	 *  the transaction is rolled back. Returns the result of `Body`. */
	bool Transaction(TFunctionRef<bool()> Body);

	/** Apply pending schema migrations. Idempotent — safe to call on every open. */
	bool RunMigrations();

	/** Current schema version stored in DB. -1 if not initialized. */
	int32 GetSchemaVersion();

private:
	bool ApplyPragmas();

	/** Look up a prepared statement for `Sql` in the LRU cache, preparing one
	 *  on miss. Returned statement is reset and has all bindings cleared, ready
	 *  for new sqlite3_bind_* calls. Returns nullptr on prepare failure (the
	 *  caller must NOT finalize — the cache owns it). */
	struct sqlite3_stmt* GetOrPrepareCached(const FString& Sql);

	/** Finalize and drop every cached statement. Must be called while holding `Mutex`. */
	void FlushStatementCache();

	sqlite3* Connection = nullptr;
	mutable FCriticalSection Mutex;

	// Reused prepared statements. Cap is small on purpose — query surface area
	// inside the plugin is bounded (~30 unique statements). FIFO eviction is
	// simple and good enough; we do not need true LRU here.
	TMap<FString, struct sqlite3_stmt*> StmtCache;
	TArray<FString> StmtCacheOrder;
	static constexpr int32 kStmtCacheCap = 64;
};
