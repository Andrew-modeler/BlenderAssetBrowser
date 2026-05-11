# SQLite Amalgamation

Bundled vendor copy of SQLite. SQLite is in the public domain.
Source: https://www.sqlite.org/

## Version

- SQLite 3.47.2 (amalgamation, 2024-12-07)
- Downloaded from: https://www.sqlite.org/2024/sqlite-amalgamation-3470200.zip
- Source ZIP SHA256: `AA73D8748095808471DEAA8E6F34AA700E37F2F787F4425744F53FDD15A89C40`

## Bundled files (verify integrity)

| File | SHA256 |
|---|---|
| `sqlite3.c` | `B947B84B8C45D1513ECF2DF8F46BF099D9F6D7A0ACAC0928061DDBB4EA6FE54B` |
| `sqlite3.h` | `B4B4CBCC2BD6DDA8EAF348103C330C971676A59B3732CCA068098F574DE43994` |
| `sqlite3ext.h` | `B184DD1586D935133D37AD76FA353FAF0A1021FF2FDEDEEDCC3498FFF74BBB94` |

To verify a clone: `Get-FileHash sqlite3.c -Algorithm SHA256` (PowerShell) or
`sha256sum sqlite3.c` (Linux/macOS) and compare against the table above.

## Compile-time defines (set in `BlenderAssetBrowser.Build.cs`)

These defines harden SQLite for our use case:

| Define | Value | Reason |
|---|---|---|
| `SQLITE_DQS` | `0` | Disallow double-quoted string literals (security) |
| `SQLITE_DEFAULT_FOREIGN_KEYS` | `1` | Foreign keys ON by default |
| `SQLITE_THREADSAFE` | `2` | Multi-thread mode (we still serialize via mutex) |
| `SQLITE_OMIT_DEPRECATED` | (defined) | Drop legacy unsafe APIs |
| `SQLITE_OMIT_LOAD_EXTENSION` | (defined) | No runtime extension loading (security) |
| `SQLITE_OMIT_AUTHORIZATION` | (defined) | We never use authorizer |
| `SQLITE_OMIT_TCL_VARIABLE` | (defined) | No TCL bindings |
| `SQLITE_ENABLE_FTS5` | (defined) | Full-text search for fuzzy queries |
| `SQLITE_ENABLE_JSON1` | (defined) | JSON functions for metadata fields |

## License

SQLite is in the public domain. See `THIRD_PARTY_NOTICES.txt` at plugin root.
