// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "BlenderBridgeManager.h"
#include "BlenderBridgeModule.h"

#include "AssetLibraryTypes.h"
#include "AssetLibrarySettings.h"

#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "EditorReimportHandler.h"
#include "ObjectTools.h"

namespace
{
	bool IsSafeUEPath(const FString& P)
	{
		if (P.IsEmpty() || P.Len() > BAB::MAX_PATH_LEN) { return false; }
		if (P.Contains(TEXT(".."))) { return false; }
		if (!P.StartsWith(TEXT("/Game/")) && !P.StartsWith(TEXT("/AssetLibraries/")))
		{
			return false;
		}
		return true;
	}

	bool ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutObj)
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path)) { return false; }
		if (Contents.Len() > BAB::MAX_FILE_BYTES_JSON) { return false; }
		auto Reader = TJsonReaderFactory<>::Create(Contents);
		return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
	}

	bool WriteJsonFile(const FString& Path, const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		auto Writer = TJsonWriterFactory<>::Create(&Out);
		if (!FJsonSerializer::Serialize(Obj, Writer)) { return false; }
		return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString GetBlenderExe()
	{
		const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>();
		FString Exe = S ? S->BlenderExecutable.FilePath : FString();

		if (Exe.IsEmpty())
		{
			// Common Windows default. Users override in Settings.
			static const TCHAR* Candidates[] =
			{
				TEXT("C:/Program Files/Blender Foundation/Blender 4.3/blender.exe"),
				TEXT("C:/Program Files/Blender Foundation/Blender 4.2/blender.exe"),
				TEXT("C:/Program Files/Blender Foundation/Blender 4.1/blender.exe"),
				TEXT("C:/Program Files/Blender Foundation/Blender 4.0/blender.exe"),
				TEXT("C:/Program Files/Blender Foundation/Blender 3.6/blender.exe"),
			};
			IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
			for (const TCHAR* C : Candidates)
			{
				if (PFM.FileExists(C)) { Exe = C; break; }
			}
		}

		if (!Exe.IsEmpty() && !Exe.EndsWith(TEXT(".exe"), ESearchCase::IgnoreCase))
		{
			Exe.Empty();
		}
		return Exe;
	}
}

FBlenderBridgeManager::FBlenderBridgeManager() = default;

FBlenderBridgeManager::~FBlenderBridgeManager()
{
	Shutdown();
}

FString FBlenderBridgeManager::GetExchangeRoot() const
{
	const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>();
	FString Rel = S ? S->ExchangeSubfolder : TEXT("BlenderAssetBrowser/Exchange");
	if (Rel.Contains(TEXT("..")) || Rel.Contains(TEXT(":")))
	{
		Rel = TEXT("BlenderAssetBrowser/Exchange");
	}
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / Rel;
}

void FBlenderBridgeManager::EnsureFolders()
{
	ExchangeRoot = GetExchangeRoot();
	IncomingDir  = ExchangeRoot / TEXT("incoming");
	OutgoingDir  = ExchangeRoot / TEXT("outgoing");

	IPlatformFile& PFM = FPlatformFileManager::Get().GetPlatformFile();
	for (const FString& Dir : { ExchangeRoot, IncomingDir, OutgoingDir })
	{
		if (!PFM.DirectoryExists(*Dir)) { PFM.CreateDirectoryTree(*Dir); }
	}
}

void FBlenderBridgeManager::WriteProjectManifest()
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Obj->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Obj->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Obj->SetStringField(TEXT("plugin_version"), TEXT("0.1.0"));
	WriteJsonFile(ExchangeRoot / TEXT("manifest.json"), Obj);
}

void FBlenderBridgeManager::Initialize()
{
	EnsureFolders();
	WriteProjectManifest();

	FDirectoryWatcherModule& DW = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* Watcher = DW.Get();
	if (Watcher)
	{
		Watcher->RegisterDirectoryChangedCallback_Handle(
			IncomingDir,
			IDirectoryWatcher::FDirectoryChanged::CreateRaw(
				this, &FBlenderBridgeManager::OnIncomingDirChanged),
			DirectoryWatcherHandle);
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FBlenderBridgeManager::TickQueue),
		1.0f);

	UE_LOG(LogBlenderBridge, Log, TEXT("BlenderBridge initialized at %s"), *ExchangeRoot);
}

void FBlenderBridgeManager::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (DirectoryWatcherHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher")))
	{
		FDirectoryWatcherModule& DW = FModuleManager::GetModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		if (IDirectoryWatcher* W = DW.Get())
		{
			W->UnregisterDirectoryChangedCallback_Handle(IncomingDir, DirectoryWatcherHandle);
		}
		DirectoryWatcherHandle.Reset();
	}
}

void FBlenderBridgeManager::OnIncomingDirChanged(const TArray<FFileChangeData>& Changes)
{
	for (const FFileChangeData& Ch : Changes)
	{
		const FString& F = Ch.Filename;
		// Only care about .meta.json drops — Blender writes those LAST.
		if (!F.EndsWith(TEXT(".meta.json"), ESearchCase::IgnoreCase)) { continue; }

		FPending P;
		P.FbxPath = F.LeftChop(FString(TEXT(".meta.json")).Len()) + TEXT(".fbx");

		// SECURITY: belt-and-suspenders — even though DirectoryWatcher only fires on
		// files inside IncomingDir, and Windows filenames cannot contain '..', we
		// re-check that the derived FBX path stays inside ExchangeRoot before
		// queuing it for reimport.
		if (!BAB::IsPathInsideRoot(P.FbxPath, ExchangeRoot))
		{
			UE_LOG(LogBlenderBridge, Warning,
				TEXT("OnIncomingDirChanged: derived FBX path escaped ExchangeRoot, dropped: %s"),
				*P.FbxPath);
			continue;
		}

		FScopeLock Lock(&QueueLock);
		ReimportQueue.Add(MoveTemp(P));
	}
}

bool FBlenderBridgeManager::TickQueue(float DeltaTime)
{
	TArray<FPending> Local;
	{
		FScopeLock Lock(&QueueLock);
		Local = MoveTemp(ReimportQueue);
		ReimportQueue.Reset();
	}

	for (const FPending& P : Local)
	{
		const FString MetaPath = P.FbxPath.LeftChop(FString(TEXT(".fbx")).Len()) + TEXT(".meta.json");
		ProcessIncomingMeta(MetaPath);
	}
	return true;
}

void FBlenderBridgeManager::ProcessIncomingMeta(const FString& MetaPath)
{
	TSharedPtr<FJsonObject> Obj;
	if (!ReadJsonFile(MetaPath, Obj))
	{
		UE_LOG(LogBlenderBridge, Warning, TEXT("Bad meta JSON: %s"), *MetaPath);
		return;
	}

	// Schema: target_ue_path (required, string), is_new (optional, bool), overwrite (optional, bool)
	FString TargetUEPath;
	bool bOverwrite = false;
	bool bIsNew = false;

	if (!Obj->TryGetStringField(TEXT("target_ue_path"), TargetUEPath) || !IsSafeUEPath(TargetUEPath))
	{
		UE_LOG(LogBlenderBridge, Warning, TEXT("Meta missing or unsafe target_ue_path: %s"), *MetaPath);
		return;
	}
	Obj->TryGetBoolField(TEXT("overwrite"), bOverwrite);
	Obj->TryGetBoolField(TEXT("is_new"), bIsNew);

	const FString FbxPath = MetaPath.LeftChop(FString(TEXT(".meta.json")).Len()) + TEXT(".fbx");
	// SECURITY: same defence as in OnIncomingDirChanged — never let a path that
	// can be influenced by file naming leave ExchangeRoot.
	if (!BAB::IsPathInsideRoot(FbxPath, ExchangeRoot))
	{
		UE_LOG(LogBlenderBridge, Warning,
			TEXT("ProcessIncomingMeta: derived FBX path escaped ExchangeRoot, dropped: %s"),
			*FbxPath);
		return;
	}
	IFileManager& FM = IFileManager::Get();
	if (!FM.FileExists(*FbxPath))
	{
		UE_LOG(LogBlenderBridge, Warning, TEXT("FBX file missing for meta %s"), *MetaPath);
		return;
	}
	const int64 FbxSize = FM.FileSize(*FbxPath);
	if (FbxSize <= 0 || FbxSize > BAB::MAX_FILE_BYTES_FBX)
	{
		UE_LOG(LogBlenderBridge, Warning, TEXT("FBX size %lld out of range for %s"), FbxSize, *FbxPath);
		return;
	}

	// Resolve destination folder + asset name from TargetUEPath.
	// TargetUEPath is /Game/Folder/AssetName  (no .uasset extension)
	const int32 LastSlash = TargetUEPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (LastSlash < 0) { return; }
	const FString DestPath = TargetUEPath.Left(LastSlash);
	const FString AssetName = TargetUEPath.Mid(LastSlash + 1);
	if (AssetName.IsEmpty() || AssetName.Len() > BAB::MAX_NAME_LEN) { return; }

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	// Import or reimport.
	if (bIsNew || !FPackageName::DoesPackageExist(TargetUEPath))
	{
		UAutomatedAssetImportData* Data = NewObject<UAutomatedAssetImportData>();
		Data->bReplaceExisting = bOverwrite;
		Data->DestinationPath = DestPath;
		Data->Filenames = { FbxPath };
		AssetTools.ImportAssetsAutomated(Data);
		UE_LOG(LogBlenderBridge, Log, TEXT("Imported %s -> %s"), *FbxPath, *TargetUEPath);
	}
	else
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FAssetData Data = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(TargetUEPath));
		if (Data.IsValid())
		{
			if (UObject* Existing = Data.GetAsset())
			{
				// Reimport API for static/skeletal meshes.
				const TArray<UObject*> Objs = { Existing };
				const TArray<FString> Paths = { FbxPath };
				// Modern API: FReimportManager
				if (GEditor)
				{
					if (auto* ReimportMgr = FReimportManager::Instance())
					{
						ReimportMgr->ReimportAsync(Existing, /*bAskForNewFileIfMissing*/ false,
							/*bShowNotification*/ true, FbxPath);
						UE_LOG(LogBlenderBridge, Log, TEXT("Reimport queued: %s <- %s"),
							*TargetUEPath, *FbxPath);
					}
				}
			}
		}
	}
}

bool FBlenderBridgeManager::EditInBlender(const FString& UEAssetPath)
{
	if (!IsSafeUEPath(UEAssetPath))
	{
		UE_LOG(LogBlenderBridge, Warning, TEXT("EditInBlender: unsafe path %s"), *UEAssetPath);
		return false;
	}

	const FString BlenderExe = GetBlenderExe();
	if (BlenderExe.IsEmpty())
	{
		UE_LOG(LogBlenderBridge, Warning,
			TEXT("Blender executable not configured. Set in Project Settings → Blender Asset Browser."));
		return false;
	}

	// Look up the actual UObject so we can export it as FBX.
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetData Data = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(UEAssetPath));
	if (!Data.IsValid())
	{
		UE_LOG(LogBlenderBridge, Warning, TEXT("Asset not found: %s"), *UEAssetPath);
		return false;
	}

	// Phase 2.4 stops at "launch Blender, point it at the existing source file
	// if there is one". A proper FBX exporter wire-up (UFbxExportFactory) is
	// next in the polish pass — for now, if we have an import source on disk,
	// just open that.
	UObject* Obj = Data.GetAsset();
	FString SourceFile;
	if (Obj)
	{
		for (TFieldIterator<FObjectProperty> It(Obj->GetClass()); It; ++It)
		{
			FObjectProperty* Prop = *It;
			if (Prop->PropertyClass &&
				Prop->PropertyClass->GetName() == TEXT("AssetImportData"))
			{
				if (UObject* Imp = Prop->GetObjectPropertyValue_InContainer(Obj))
				{
					if (FProperty* Sources = Imp->GetClass()->FindPropertyByName(TEXT("SourceData")))
					{
						// Best-effort; deeper plumbing is a polish task.
					}
				}
				break;
			}
		}
	}

	if (SourceFile.IsEmpty())
	{
		UE_LOG(LogBlenderBridge, Log,
			TEXT("No source FBX recorded for %s — full export flow is a polish task."),
			*UEAssetPath);
		// Still launch Blender so user can work — they'll save back and we'll catch it.
	}

	// Launch Blender via CreateProc (argv array). NEVER use a shell string —
	// argument injection via crafted asset names is a real attack surface.
	FString Params;
	if (!SourceFile.IsEmpty()) { Params = FString::Printf(TEXT("\"%s\""), *SourceFile); }
	const FProcHandle Proc = FPlatformProcess::CreateProc(
		*BlenderExe,
		*Params,
		/*bLaunchDetached*/ true,
		/*bLaunchHidden*/   false,
		/*bLaunchReallyHidden*/ false,
		/*OutProcessID*/    nullptr,
		/*PriorityModifier*/ 0,
		nullptr,
		nullptr);

	return Proc.IsValid();
}
