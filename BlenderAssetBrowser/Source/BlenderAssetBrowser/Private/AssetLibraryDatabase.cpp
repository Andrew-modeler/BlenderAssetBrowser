// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "AssetLibraryDatabase.h"
#include "AssetLibraryTypes.h"
#include "BlenderAssetBrowserModule.h"

#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformFileManager.h"

THIRD_PARTY_INCLUDES_START
#include "sqlite/sqlite3.h"
THIRD_PARTY_INCLUDES_END

namespace
{
	// Convert FString (UTF-16/32 internally) -> UTF-8 for sqlite3_bind_text.
	// Returned FTCHARToUTF8 lives only as long as the temporary; we call .Get()
	// on a stable variable to ensure pointer is valid through sqlite3_step.
	FString ResultMessage(sqlite3* Db)
	{
		const char* Msg = Db ? sqlite3_errmsg(Db) : "no connection";
		return FString(UTF8_TO_TCHAR(Msg));
	}
}

namespace BAB
{
	int32 FRow::ColumnCount() const
	{
		return Stmt ? sqlite3_column_count(Stmt) : 0;
	}

	FString FRow::GetText(int32 ColIdx) const
	{
		if (!Stmt) { return FString(); }
		const unsigned char* P = sqlite3_column_text(Stmt, ColIdx);
		return P ? FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(P))) : FString();
	}

	int64 FRow::GetInt64(int32 ColIdx) const
	{
		return Stmt ? static_cast<int64>(sqlite3_column_int64(Stmt, ColIdx)) : 0;
	}

	double FRow::GetDouble(int32 ColIdx) const
	{
		return Stmt ? sqlite3_column_double(Stmt, ColIdx) : 0.0;
	}

	bool FRow::IsNull(int32 ColIdx) const
	{
		return Stmt ? (sqlite3_column_type(Stmt, ColIdx) == SQLITE_NULL) : true;
	}

	TArray<uint8> FRow::GetBlob(int32 ColIdx) const
	{
		TArray<uint8> Out;
		if (!Stmt) { return Out; }
		const void* Data = sqlite3_column_blob(Stmt, ColIdx);
		const int Size = sqlite3_column_bytes(Stmt, ColIdx);
		if (Data && Size > 0)
		{
			Out.Append(static_cast<const uint8*>(Data), Size);
		}
		return Out;
	}
}

FAssetLibraryDatabase::FAssetLibraryDatabase() = default;

FAssetLibraryDatabase::~FAssetLibraryDatabase()
{
	Close();
}

bool FAssetLibraryDatabase::Open(const FString& InAbsolutePath, const FString& InAllowedRoot)
{
	FScopeLock Lock(&Mutex);

	if (Connection)
	{
		UE_LOG(LogBlenderAssetBrowser, Warning, TEXT("Database already open."));
		return true;
	}

	if (InAbsolutePath.IsEmpty())
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("Open: empty path."));
		return false;
	}

	// SECURITY: refuse to open a DB outside the configured allowed root.
	if (!BAB::IsPathInsideRoot(InAbsolutePath, InAllowedRoot))
	{
		UE_LOG(LogBlenderAssetBrowser, Error,
			TEXT("Open: path '%s' is outside allowed root '%s'. Refusing."),
			*InAbsolutePath, *InAllowedRoot);
		return false;
	}

	// Ensure parent directory exists.
	const FString Dir = FPaths::GetPath(InAbsolutePath);
	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	if (!Dir.IsEmpty() && !PFM.DirectoryExists(*Dir))
	{
		PFM.CreateDirectoryTree(*Dir);
	}

	const FTCHARToUTF8 Utf8Path(*InAbsolutePath);
	const int Flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_PRIVATECACHE;

	const int Rc = sqlite3_open_v2(Utf8Path.Get(), &Connection, Flags, nullptr);
	if (Rc != SQLITE_OK)
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("sqlite3_open_v2 failed: %s"),
			*ResultMessage(Connection));
		if (Connection)
		{
			sqlite3_close(Connection);
			Connection = nullptr;
		}
		return false;
	}

	if (!ApplyPragmas())
	{
		Close();
		return false;
	}

	UE_LOG(LogBlenderAssetBrowser, Log, TEXT("Database opened: %s"), *InAbsolutePath);
	return true;
}

void FAssetLibraryDatabase::Close()
{
	FScopeLock Lock(&Mutex);
	// Cached statements hold pointers into Connection — finalize them BEFORE
	// sqlite3_close, otherwise sqlite3_close fails with SQLITE_BUSY.
	FlushStatementCache();
	if (Connection)
	{
		sqlite3_close(Connection);
		Connection = nullptr;
	}
}

void FAssetLibraryDatabase::FlushStatementCache()
{
	// Mutex is held by callers (Close, GetOrPrepareCached on eviction).
	for (auto& Pair : StmtCache)
	{
		if (Pair.Value) { sqlite3_finalize(Pair.Value); }
	}
	StmtCache.Reset();
	StmtCacheOrder.Reset();
}

sqlite3_stmt* FAssetLibraryDatabase::GetOrPrepareCached(const FString& Sql)
{
	// Mutex is held by callers (Execute, QueryRows).
	if (!Connection) { return nullptr; }

	if (sqlite3_stmt** Found = StmtCache.Find(Sql))
	{
		sqlite3_stmt* Stmt = *Found;
		// Reset returns the statement to its just-prepared state. clear_bindings
		// drops the last bound values so leftover state can't leak into a new call.
		sqlite3_reset(Stmt);
		sqlite3_clear_bindings(Stmt);
		return Stmt;
	}

	sqlite3_stmt* NewStmt = nullptr;
	const FTCHARToUTF8 Utf8Sql(*Sql);
	const int Rc = sqlite3_prepare_v2(Connection, Utf8Sql.Get(), -1, &NewStmt, nullptr);
	if (Rc != SQLITE_OK)
	{
		const char* Msg = sqlite3_errmsg(Connection);
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("prepare failed: %s\nSQL: %s"),
			*FString(UTF8_TO_TCHAR(Msg ? Msg : "no message")), *Sql);
		if (NewStmt) { sqlite3_finalize(NewStmt); }
		return nullptr;
	}

	// FIFO eviction at capacity. The cap is sized for the entire plugin's
	// distinct-query set with a safety margin — we expect eviction to be rare.
	if (StmtCache.Num() >= kStmtCacheCap)
	{
		const FString Oldest = StmtCacheOrder[0];
		StmtCacheOrder.RemoveAt(0);
		if (sqlite3_stmt** Evicted = StmtCache.Find(Oldest))
		{
			if (*Evicted) { sqlite3_finalize(*Evicted); }
			StmtCache.Remove(Oldest);
		}
	}

	StmtCache.Add(Sql, NewStmt);
	StmtCacheOrder.Add(Sql);
	return NewStmt;
}

bool FAssetLibraryDatabase::ApplyPragmas()
{
	// These run once at open. They're safe constant SQL — no user input.
	struct FPragma { const TCHAR* Sql; const TCHAR* Note; };
	const FPragma Pragmas[] =
	{
		{ TEXT("PRAGMA journal_mode = WAL"),     TEXT("WAL for concurrent reads") },
		{ TEXT("PRAGMA synchronous = NORMAL"),   TEXT("safe + fast for desktop") },
		{ TEXT("PRAGMA foreign_keys = ON"),      TEXT("enforce FK constraints") },
		{ TEXT("PRAGMA temp_store = MEMORY"),    TEXT("don't write temp to disk") },
		{ TEXT("PRAGMA trusted_schema = OFF"),   TEXT("don't run unsafe schema funcs") },
	};

	for (const FPragma& P : Pragmas)
	{
		if (!Execute(P.Sql))
		{
			UE_LOG(LogBlenderAssetBrowser, Error, TEXT("Pragma failed (%s): %s"),
				P.Note, *ResultMessage(Connection));
			return false;
		}
	}
	return true;
}

namespace
{
	// Returns SQLITE_OK on success, sqlite error code otherwise. Caller logs.
	int BindParamsToStmt(sqlite3_stmt* Stmt, const TArray<BAB::FBoundValue>& Params)
	{
		for (int32 i = 0; i < Params.Num(); ++i)
		{
			const BAB::FBoundValue& V = Params[i];
			const int Pos = i + 1; // SQLite parameter index is 1-based.
			int Rc = SQLITE_OK;
			switch (V.Type)
			{
			case BAB::FBoundValue::EType::Null:
				Rc = sqlite3_bind_null(Stmt, Pos);
				break;
			case BAB::FBoundValue::EType::Int64:
				Rc = sqlite3_bind_int64(Stmt, Pos, V.IntVal);
				break;
			case BAB::FBoundValue::EType::Double:
				Rc = sqlite3_bind_double(Stmt, Pos, V.DoubleVal);
				break;
			case BAB::FBoundValue::EType::Text:
			{
				// SQLITE_TRANSIENT copies the bytes — safe since FTCHARToUTF8 is temp.
				const FTCHARToUTF8 Utf8Val(*V.TextVal);
				Rc = sqlite3_bind_text(Stmt, Pos, Utf8Val.Get(), Utf8Val.Length(), SQLITE_TRANSIENT);
				break;
			}
			case BAB::FBoundValue::EType::Blob:
				Rc = sqlite3_bind_blob(Stmt, Pos, V.BlobVal.GetData(), V.BlobVal.Num(), SQLITE_TRANSIENT);
				break;
			}
			if (Rc != SQLITE_OK) { return Rc; }
		}
		return SQLITE_OK;
	}
}

bool FAssetLibraryDatabase::Execute(const FString& Sql, const TArray<BAB::FBoundValue>& Params)
{
	FScopeLock Lock(&Mutex);
	if (!Connection) { return false; }
	if (Sql.Len() > 65536)
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("SQL statement too long (refused)."));
		return false;
	}

	sqlite3_stmt* Stmt = GetOrPrepareCached(Sql);
	if (!Stmt) { return false; }

	// SECURITY: prepared-statement reuse means we MUST reset state between calls
	// (done by GetOrPrepareCached). The cache never bypasses parameter binding —
	// the only path to pass runtime values stays sqlite3_bind_*.
	const int32 BindCount = sqlite3_bind_parameter_count(Stmt);
	if (Params.Num() != BindCount)
	{
		UE_LOG(LogBlenderAssetBrowser, Error,
			TEXT("Bind count mismatch: SQL expects %d, got %d. SQL=%s"),
			BindCount, Params.Num(), *Sql);
		return false;
	}

	if (BindParamsToStmt(Stmt, Params) != SQLITE_OK)
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("bind failed: %s"), *ResultMessage(Connection));
		return false;
	}

	const int Rc = sqlite3_step(Stmt);
	const bool bOk = (Rc == SQLITE_DONE || Rc == SQLITE_ROW);
	if (!bOk)
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("step failed: %s"), *ResultMessage(Connection));
	}
	// Reset (not finalize) returns the statement to the cache for reuse.
	sqlite3_reset(Stmt);
	sqlite3_clear_bindings(Stmt);
	return bOk;
}

bool FAssetLibraryDatabase::QueryRows(const FString& Sql,
	const TArray<BAB::FBoundValue>& Params,
	TFunctionRef<bool(const BAB::FRow& Row)> RowFn)
{
	FScopeLock Lock(&Mutex);
	if (!Connection) { return false; }
	if (Sql.Len() > 65536)
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("SQL too long (refused)."));
		return false;
	}

	sqlite3_stmt* Stmt = GetOrPrepareCached(Sql);
	if (!Stmt) { return false; }

	if (sqlite3_bind_parameter_count(Stmt) != Params.Num())
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("Bind count mismatch in QueryRows."));
		return false;
	}

	if (BindParamsToStmt(Stmt, Params) != SQLITE_OK)
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("bind failed: %s"), *ResultMessage(Connection));
		sqlite3_reset(Stmt);
		sqlite3_clear_bindings(Stmt);
		return false;
	}

	BAB::FRow Row;
	Row.Stmt = Stmt;
	int Rc = SQLITE_OK;
	while ((Rc = sqlite3_step(Stmt)) == SQLITE_ROW)
	{
		if (!RowFn(Row)) { break; } // caller can early-exit
	}

	const bool bOk = (Rc == SQLITE_ROW || Rc == SQLITE_DONE);
	if (!bOk)
	{
		UE_LOG(LogBlenderAssetBrowser, Error, TEXT("step failed: %s"), *ResultMessage(Connection));
	}
	// Reset (not finalize) — statement stays in the cache, ready for next call.
	sqlite3_reset(Stmt);
	sqlite3_clear_bindings(Stmt);
	return bOk;
}

int64 FAssetLibraryDatabase::LastInsertRowId() const
{
	FScopeLock Lock(&Mutex);
	return Connection ? static_cast<int64>(sqlite3_last_insert_rowid(Connection)) : 0;
}

bool FAssetLibraryDatabase::Transaction(TFunctionRef<bool()> Body)
{
	if (!Execute(TEXT("BEGIN IMMEDIATE")))
	{
		return false;
	}

	bool bOk = false;
	bool bThrew = true;
	struct FRollbackOnException
	{
		FAssetLibraryDatabase* Db;
		bool& Ok;
		bool& Threw;
		~FRollbackOnException()
		{
			if (Threw)
			{
				Db->Execute(TEXT("ROLLBACK"));
			}
			else if (Ok)
			{
				Db->Execute(TEXT("COMMIT"));
			}
			else
			{
				Db->Execute(TEXT("ROLLBACK"));
			}
		}
	} Guard{ this, bOk, bThrew };

	bOk = Body();
	bThrew = false;
	return bOk;
}

int32 FAssetLibraryDatabase::GetSchemaVersion()
{
	int32 Version = -1;
	QueryRows(TEXT("PRAGMA user_version"), {}, [&Version](const BAB::FRow& Row) -> bool
	{
		Version = static_cast<int32>(Row.GetInt64(0));
		return false;
	});
	return Version;
}

bool FAssetLibraryDatabase::RunMigrations()
{
	const int32 CurrentVersion = GetSchemaVersion();
	const int32 LatestVersion = 2;

	if (CurrentVersion >= LatestVersion)
	{
		return true;
	}

	return Transaction([&, CurrentVersion]() -> bool
	{
		// Migration v1 — initial schema.
		// All SQL here is hardcoded — no user input is ever interpolated.
		const TCHAR* InitialSchema =
			TEXT("CREATE TABLE IF NOT EXISTS catalogs (")
			TEXT("  id          INTEGER PRIMARY KEY AUTOINCREMENT,")
			TEXT("  parent_id   INTEGER REFERENCES catalogs(id) ON DELETE CASCADE,")
			TEXT("  name        TEXT NOT NULL,")
			TEXT("  color       TEXT,")
			TEXT("  icon        TEXT,")
			TEXT("  sort_order  INTEGER DEFAULT 0,")
			TEXT("  is_smart    INTEGER DEFAULT 0,")
			TEXT("  smart_query TEXT,")
			TEXT("  created_at  DATETIME DEFAULT CURRENT_TIMESTAMP")
			TEXT(");");
		if (!Execute(InitialSchema)) { return false; }

		const TCHAR* LibrariesTable =
			TEXT("CREATE TABLE IF NOT EXISTS libraries (")
			TEXT("  id          INTEGER PRIMARY KEY AUTOINCREMENT,")
			TEXT("  name        TEXT NOT NULL,")
			TEXT("  path        TEXT NOT NULL,")
			TEXT("  type        TEXT DEFAULT 'local',")
			TEXT("  priority    INTEGER DEFAULT 0,")
			TEXT("  is_visible  INTEGER DEFAULT 1,")
			TEXT("  created_at  DATETIME DEFAULT CURRENT_TIMESTAMP")
			TEXT(");");
		if (!Execute(LibrariesTable)) { return false; }

		const TCHAR* AssetsTable =
			TEXT("CREATE TABLE IF NOT EXISTS assets (")
			TEXT("  id              INTEGER PRIMARY KEY AUTOINCREMENT,")
			TEXT("  asset_path      TEXT NOT NULL,")
			TEXT("  asset_name      TEXT NOT NULL,")
			TEXT("  asset_type      TEXT NOT NULL,")
			TEXT("  library_id      INTEGER REFERENCES libraries(id),")
			TEXT("  rating          INTEGER DEFAULT 0,")
			TEXT("  notes           TEXT,")
			TEXT("  preview_path    TEXT,")
			TEXT("  preview_mesh    TEXT,")
			TEXT("  tri_count       INTEGER,")
			TEXT("  vert_count      INTEGER,")
			TEXT("  lod_count       INTEGER,")
			TEXT("  texture_res_max INTEGER,")
			TEXT("  has_collision   INTEGER,")
			TEXT("  collision_type  TEXT,")
			TEXT("  material_count  INTEGER,")
			TEXT("  disk_size_bytes INTEGER,")
			TEXT("  engine_version  TEXT,")
			TEXT("  source_type     TEXT,")
			TEXT("  source_pack_name TEXT,")
			TEXT("  source_pack_id  TEXT,")
			TEXT("  source_url      TEXT,")
			TEXT("  source_author   TEXT,")
			TEXT("  source_license  TEXT,")
			TEXT("  source_version  TEXT,")
			TEXT("  source_hash     TEXT,")
			TEXT("  imported_at     DATETIME,")
			TEXT("  latest_version  TEXT,")
			TEXT("  update_state    TEXT DEFAULT 'unknown',")
			TEXT("  changelog       TEXT,")
			TEXT("  created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,")
			TEXT("  modified_at     DATETIME DEFAULT CURRENT_TIMESTAMP,")
			TEXT("  UNIQUE(asset_path, library_id)")
			TEXT(");");
		if (!Execute(AssetsTable)) { return false; }

		if (!Execute(TEXT("CREATE TABLE IF NOT EXISTS asset_catalogs ("
		                  "  asset_id   INTEGER REFERENCES assets(id) ON DELETE CASCADE,"
		                  "  catalog_id INTEGER REFERENCES catalogs(id) ON DELETE CASCADE,"
		                  "  PRIMARY KEY (asset_id, catalog_id))"))) { return false; }

		if (!Execute(TEXT("CREATE TABLE IF NOT EXISTS tags ("
		                  "  id     INTEGER PRIMARY KEY AUTOINCREMENT,"
		                  "  name   TEXT NOT NULL UNIQUE,"
		                  "  color  TEXT,"
		                  "  parent TEXT,"
		                  "  count  INTEGER DEFAULT 0)"))) { return false; }

		if (!Execute(TEXT("CREATE TABLE IF NOT EXISTS asset_tags ("
		                  "  asset_id   INTEGER REFERENCES assets(id) ON DELETE CASCADE,"
		                  "  tag_id     INTEGER REFERENCES tags(id) ON DELETE CASCADE,"
		                  "  source     TEXT DEFAULT 'manual',"
		                  "  confidence REAL,"
		                  "  PRIMARY KEY (asset_id, tag_id))"))) { return false; }

		if (!Execute(TEXT("CREATE TABLE IF NOT EXISTS favorites ("
		                  "  asset_id INTEGER PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE,"
		                  "  pinned   INTEGER DEFAULT 0,"
		                  "  used_at  DATETIME DEFAULT CURRENT_TIMESTAMP)"))) { return false; }

		if (!Execute(TEXT("CREATE TABLE IF NOT EXISTS collections ("
		                  "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
		                  "  name         TEXT NOT NULL,"
		                  "  description  TEXT,"
		                  "  preview_path TEXT,"
		                  "  spawn_mode   TEXT DEFAULT 'blueprint',"
		                  "  created_at   DATETIME DEFAULT CURRENT_TIMESTAMP)"))) { return false; }

		if (!Execute(TEXT("CREATE TABLE IF NOT EXISTS collection_items ("
		                  "  collection_id INTEGER REFERENCES collections(id) ON DELETE CASCADE,"
		                  "  asset_id      INTEGER REFERENCES assets(id) ON DELETE CASCADE,"
		                  "  transform_json TEXT,"
		                  "  material_overrides_json TEXT,"
		                  "  sort_order    INTEGER DEFAULT 0,"
		                  "  PRIMARY KEY (collection_id, asset_id, sort_order))"))) { return false; }

		if (!Execute(TEXT("CREATE TABLE IF NOT EXISTS search_history ("
		                  "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
		                  "  query   TEXT NOT NULL,"
		                  "  used_at DATETIME DEFAULT CURRENT_TIMESTAMP)"))) { return false; }

		// Indexes
		if (!Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_assets_type ON assets(asset_type)"))) { return false; }
		if (!Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_assets_source ON assets(source_type)"))) { return false; }
		if (!Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_assets_library ON assets(library_id)"))) { return false; }
		if (!Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_assets_name ON assets(asset_name)"))) { return false; }
		if (!Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_tags_name ON tags(name)"))) { return false; }

		// FTS5 virtual table — fuzzy search index
		if (!Execute(TEXT("CREATE VIRTUAL TABLE IF NOT EXISTS assets_fts USING fts5("
		                  "  asset_name, notes, source_pack_name, source_author,"
		                  "  content='assets', content_rowid='id')"))) { return false; }

		if (CurrentVersion < 1)
		{
			if (!Execute(TEXT("PRAGMA user_version = 1"))) { return false; }
			UE_LOG(LogBlenderAssetBrowser, Log, TEXT("Schema migrated to version 1."));
		}

		// --- Migration v2 — asset_embeddings table (AI auto-tagging cache). ---
		if (CurrentVersion < 2)
		{
			if (!Execute(TEXT("CREATE TABLE IF NOT EXISTS asset_embeddings ("
			                  "  asset_id    INTEGER PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE,"
			                  "  model_id    TEXT NOT NULL,"        // e.g. "siglip2-base-p16-224"
			                  "  vector_dim  INTEGER NOT NULL,"
			                  "  vector_blob BLOB NOT NULL,"        // little-endian float32 pack
			                  "  computed_at DATETIME DEFAULT CURRENT_TIMESTAMP)"))) { return false; }

			if (!Execute(TEXT("PRAGMA user_version = 2"))) { return false; }
			UE_LOG(LogBlenderAssetBrowser, Log, TEXT("Schema migrated to version 2."));
		}
		return true;
	});
}
