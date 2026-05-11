# Microsoft ONNX Runtime

Bundled prebuilt Windows binaries for AI auto-tagging.

## Version

- **ONNX Runtime 1.20.1** (Windows x64, CPU)
- Source: https://github.com/microsoft/onnxruntime/releases/tag/v1.20.1
- License: MIT (see THIRD_PARTY_NOTICES.txt at plugin root)

## Why 1.20.1 specifically

UE 5.6 ships its own `onnxruntime.dll` (v1.20) inside
`Engine/Plugins/NNE/NNERuntimeORT/`. When our plugin's
`UnrealEditor-AITagging.dll` declares `onnxruntime.dll` as a dependency,
the Windows loader resolves to whichever `onnxruntime.dll` is loaded first,
which is usually UE's NNE one.

ORT's C API is versioned: `OrtGetApiBase()->GetApi(ORT_API_VERSION)` returns
`nullptr` if the requested API version exceeds what the loaded DLL supports.
The 1.26 header declares `ORT_API_VERSION = 23`; the 1.20 DLL caps at 20.
Linking 1.26 headers against UE's 1.20 DLL → `Ort::Env::Env()` access
violation on `api_ == nullptr`.

By pinning to 1.20.1 headers we match UE's bundled DLL exactly.

## Layout

```
ThirdParty/onnxruntime/
  extracted/onnxruntime-win-x64-1.20.1/
    include/  *.h
    lib/      onnxruntime.dll, onnxruntime.lib, onnxruntime_providers_shared.{dll,lib}
```

## Build integration

`AITagging.Build.cs` detects the SDK presence and sets `BAB_HAS_ONNXRUNTIME=1`.
`onnxruntime.dll` is deployed alongside the plugin via `RuntimeDependencies`.
Delay-loading is **deliberately off** — see PHASE_STATUS.md for the crash
history this avoids.

The C++ wrapper is included with `ORT_API_MANUAL_INIT` defined locally so
we control when `OrtGetApiBase()->GetApi()` is called.

## Bumping the version

If you want to ship newer ORT:
1. Check what UE bundles (`%UE_ROOT%/Engine/Plugins/NNE/NNERuntimeORT/`).
2. Either match UE exactly, or rename our bundled DLL to a unique name
   (e.g. `bab_onnxruntime.dll`) so Windows can keep both loaded.
3. Update `OnnxDir` path in `AITagging.Build.cs`.
