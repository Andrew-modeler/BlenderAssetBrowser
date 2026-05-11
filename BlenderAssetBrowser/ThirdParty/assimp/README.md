# Assimp (Open Asset Import Library)

Bundled Windows binaries — built from Assimp v6.0.5 source with MSVC 14.44
(VS 2022 17.14), Release config, dynamic linking.

## Layout

```
ThirdParty/assimp/
  include/assimp/    # public headers
  lib/               # assimp-vc143-mt.lib (import library)
  bin/               # assimp-vc143-mt.dll (runtime)
```

## Version + build options

- Assimp 6.0.5 (https://github.com/assimp/assimp/releases/tag/v6.0.5)
- License: BSD 3-Clause
- Importers enabled: FBX, OBJ, GLTF, STL
- Importers disabled: everything else (smaller DLL)
- USD support disabled (tinyusdz had /WX issues with our toolchain).

To rebuild:
```powershell
$env:VCToolsVersion = "14.44.35207"
cmake -S source/assimp-6.0.5 -B build -G "Visual Studio 17 2022" -A x64 `
    -T "host=x64,version=14.44.35207" `
    -DCMAKE_INSTALL_PREFIX=install -DBUILD_SHARED_LIBS=ON `
    -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF `
    -DASSIMP_NO_EXPORT=ON `
    -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF `
    -DASSIMP_BUILD_FBX_IMPORTER=ON -DASSIMP_BUILD_OBJ_IMPORTER=ON `
    -DASSIMP_BUILD_GLTF_IMPORTER=ON -DASSIMP_BUILD_STL_IMPORTER=ON `
    -DASSIMP_WARNINGS_AS_ERRORS=OFF
cmake --build build --config Release --parallel 8 --target install
```

The `VCToolsVersion` env var + `-T host=x64,version=N` flag pin the
toolchain to 14.44; without that, CMake/VS picks the default 14.38 which
links against an older C++ runtime and fails on `__std_find_trivial_*`
SIMD helpers.

## Integration

`AssetPreview.Build.cs` detects the presence of `lib/assimp-vc143-mt.lib`
and sets `BAB_HAS_ASSIMP=1`. `assimp-vc143-mt.dll` is deployed via
`RuntimeDependencies` to the plugin's `Binaries/Win64/` folder.

`FExternalPreviewRenderer` (in `AssetPreview/Private/`) wraps the load
calls with safety limits (max mesh size, faces, vertices) and converts
the loaded `aiScene` into a transient `UStaticMesh` for rendering via
UE's own `ThumbnailTools` — no separate renderer required.
