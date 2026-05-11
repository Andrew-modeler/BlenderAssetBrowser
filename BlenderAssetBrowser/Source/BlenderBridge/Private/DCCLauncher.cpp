// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "DCCLauncher.h"
#include "DCCReimportWatcher.h"
#include "BlenderBridgeModule.h"

#include "AssetLibraryTypes.h"
#include "AssetLibrarySettings.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

namespace { static FDCCReimportWatcher* GGlobalWatcher = nullptr; }

FDCCReimportWatcher* FDCCLauncher::GetGlobalWatcher() { return GGlobalWatcher; }
void FDCCLauncher::SetGlobalWatcher(FDCCReimportWatcher* InWatcher) { GGlobalWatcher = InWatcher; }

namespace
{
	bool IsSafePath(const FString& P)
	{
		if (P.IsEmpty() || P.Len() > BAB::MAX_PATH_LEN) { return false; }
		if (P.Contains(TEXT(".."))) { return false; }
		// SECURITY: reject characters that could break out of the argv-quoting
		// when we wrap the path in double quotes for CreateProc. " is illegal in
		// Windows filenames anyway, but we belt-and-suspenders it. The same for
		// any control character — those don't belong in a real source-file path.
		for (TCHAR C : P)
		{
			if (C == TEXT('"') || C == TEXT('\n') || C == TEXT('\r') || C == TEXT('\t')) { return false; }
			if (C < 32) { return false; }
		}
		IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
		return PFM.FileExists(*P);
	}

	FString ExeForKind(EDCCKind K)
	{
		const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>();
		if (!S) { return FString(); }
		switch (K)
		{
		case EDCCKind::Blender:           return S->BlenderExecutable.FilePath;
		case EDCCKind::SubstancePainter:  return S->SubstancePainterExecutable.FilePath;
		case EDCCKind::Photoshop:         return S->PhotoshopExecutable.FilePath;
		case EDCCKind::Max3ds:            return S->MaxExecutable.FilePath;
		}
		return FString();
	}
}

EDCCKind FDCCLauncher::GuessKindFromExtension(const FString& Filename)
{
	const FString Ext = FPaths::GetExtension(Filename, false).ToLower();
	if (Ext == TEXT("blend")) { return EDCCKind::Blender; }
	if (Ext == TEXT("spp"))   { return EDCCKind::SubstancePainter; }
	if (Ext == TEXT("psd") || Ext == TEXT("psb")) { return EDCCKind::Photoshop; }
	if (Ext == TEXT("max"))   { return EDCCKind::Max3ds; }
	return EDCCKind::Blender;
}

bool FDCCLauncher::OpenInDCC(EDCCKind Kind, const FString& SourceFile,
	const FString& AssociatedAssetPackage)
{
	if (!IsSafePath(SourceFile))
	{
		UE_LOG(LogBlenderBridge, Warning, TEXT("DCCLauncher: source file rejected: %s"), *SourceFile);
		return false;
	}

	const FString Exe = ExeForKind(Kind);
	if (Exe.IsEmpty())
	{
		UE_LOG(LogBlenderBridge, Warning,
			TEXT("DCCLauncher: no executable configured for kind %d."), static_cast<int32>(Kind));
		return false;
	}
	if (!Exe.EndsWith(TEXT(".exe"), ESearchCase::IgnoreCase)) { return false; }

	// argv-style launch — never shell-string.
	const FString Args = FString::Printf(TEXT("\"%s\""), *SourceFile);
	const FProcHandle Proc = FPlatformProcess::CreateProc(
		*Exe, *Args,
		/*bLaunchDetached*/    true,
		/*bLaunchHidden*/      false,
		/*bLaunchReallyHidden*/false,
		nullptr, 0, nullptr, nullptr);
	if (!Proc.IsValid())
	{
		UE_LOG(LogBlenderBridge, Warning, TEXT("DCCLauncher: failed to launch %s."), *Exe);
		return false;
	}

	// Bug #3 fix: register the source file with the watcher so that when the
	// user saves in the DCC, we auto-trigger a UE asset reimport.
	if (!AssociatedAssetPackage.IsEmpty() && GGlobalWatcher)
	{
		GGlobalWatcher->Register(SourceFile, AssociatedAssetPackage);
	}

	UE_LOG(LogBlenderBridge, Log, TEXT("DCCLauncher: opened %s in %s%s."),
		*SourceFile, *Exe,
		AssociatedAssetPackage.IsEmpty() ? TEXT("") : TEXT(" (watching for reimport)"));
	return true;
}
