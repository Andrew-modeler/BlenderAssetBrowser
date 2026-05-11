// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "FabUpdateChecker.h"
#include "UpdateCheckerModule.h"

#include "AssetLibraryDatabase.h"
#include "AssetLibrarySubsystem.h"
#include "AssetLibrarySettings.h"
#include "DiscordNotifier.h"

#include "Editor.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"

namespace
{
	constexpr int32 MAX_INFLIGHT       = 4;          // be polite to fab.com
	constexpr int32 MAX_BODY_BYTES     = 2 * 1024 * 1024;
	constexpr double DAILY_COOLDOWN_S  = 24.0 * 60.0 * 60.0;
	constexpr float  REQ_TIMEOUT_S     = 10.0f;

	BAB::FBoundValue B_Int(int64 X)              { return BAB::FBoundValue::MakeInt(X); }
	BAB::FBoundValue B_Text(const FString& S)    { return BAB::FBoundValue::MakeText(S); }

	UAssetLibrarySubsystem* GetSub()
	{
		if (!GEditor) { return nullptr; }
		return GEditor->GetEditorSubsystem<UAssetLibrarySubsystem>();
	}

	/** Cheap, tolerant version extractor. Looks for common Fab patterns:
	 *  "version":"1.2.3", "Version: 1.2.3", "v1.2.3" near "version" keyword.
	 *  Returns empty if nothing plausible is found.
	 *
	 *  We never invoke an HTML parser — that's an attack-surface multiplier. */
	FString ExtractVersion(const FString& Body)
	{
		const TCHAR* Keys[] = { TEXT("\"version\""), TEXT("Version:"), TEXT("version:") };
		for (const TCHAR* K : Keys)
		{
			const int32 Idx = Body.Find(K, ESearchCase::IgnoreCase);
			if (Idx < 0) { continue; }
			// Scan forward up to 64 chars; pick first run that looks like X.Y[.Z[.W]].
			const int32 ScanEnd = FMath::Min(Idx + 128, Body.Len());
			FString Build;
			bool bInVer = false;
			for (int32 i = Idx; i < ScanEnd; ++i)
			{
				const TCHAR C = Body[i];
				if (FChar::IsDigit(C) || (bInVer && C == TEXT('.')))
				{
					Build.AppendChar(C);
					bInVer = true;
				}
				else if (bInVer)
				{
					break;
				}
			}
			if (Build.Len() >= 3 && Build.Contains(TEXT(".")))
			{
				// Trim trailing dot if any.
				if (Build.EndsWith(TEXT("."))) { Build = Build.LeftChop(1); }
				return Build;
			}
		}
		return FString();
	}

	/** Cap-aware comparison: returns true if `Latest` is lexicographically newer. */
	bool IsNewer(const FString& Known, const FString& Latest)
	{
		if (Latest.IsEmpty()) { return false; }
		if (Known.IsEmpty())  { return true; }
		// Component-wise numeric comparison. Falls back to lexicographic.
		TArray<FString> KP, LP;
		Known.ParseIntoArray(KP, TEXT("."), true);
		Latest.ParseIntoArray(LP, TEXT("."), true);
		const int32 N = FMath::Max(KP.Num(), LP.Num());
		for (int32 i = 0; i < N; ++i)
		{
			const int32 K = (i < KP.Num()) ? FCString::Atoi(*KP[i]) : 0;
			const int32 L = (i < LP.Num()) ? FCString::Atoi(*LP[i]) : 0;
			if (L > K) { return true; }
			if (L < K) { return false; }
		}
		return false;
	}
}

void FFabUpdateChecker::Initialize()
{
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FFabUpdateChecker::TickDaily),
		60.0f);   // poll the cooldown gate once a minute

	// Immediate first pass if the user opted in.
	const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>();
	if (S && S->bEnableFabUpdateCheck)
	{
		CheckNow();
	}
}

void FFabUpdateChecker::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

bool FFabUpdateChecker::TickDaily(float DeltaTime)
{
	const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>();
	if (!S || !S->bEnableFabUpdateCheck) { return true; }

	const double Now = FPlatformTime::Seconds();
	if (Now - LastCheckedSeconds < DAILY_COOLDOWN_S) { return true; }

	LastCheckedSeconds = Now;
	CheckNow();
	return true;
}

void FFabUpdateChecker::CheckNow()
{
	UAssetLibrarySubsystem* Sub = GetSub();
	if (!Sub || !Sub->IsReady()) { return; }
	FAssetLibraryDatabase* Db = Sub->GetDatabase();
	if (!Db) { return; }

	UE_LOG(LogUpdateChecker, Log, TEXT("Fab update check: starting pass."));

	struct FCandidate { int64 Id; FString Url; FString Version; };
	TArray<FCandidate> Cands;

	Db->QueryRows(
		TEXT("SELECT id, source_url, source_version FROM assets "
		     "WHERE source_type='fab' AND source_url IS NOT NULL AND source_url != ''"),
		{},
		[&Cands](const BAB::FRow& R) -> bool
		{
			FCandidate C;
			C.Id      = R.GetInt64(0);
			C.Url     = R.GetText(1);
			C.Version = R.IsNull(2) ? FString() : R.GetText(2);
			Cands.Add(MoveTemp(C));
			return true;
		});

	for (const FCandidate& C : Cands)
	{
		// SECURITY: HTTPS only. URL was validated at insert via Validate(),
		// but defense-in-depth here too.
		if (!C.Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase)) { continue; }
		EnqueueOne(C.Id, C.Url, C.Version);
	}
}

void FFabUpdateChecker::EnqueueOne(int64 AssetId, const FString& Url, const FString& KnownVersion)
{
	// Throttle: at most MAX_INFLIGHT outstanding requests. The runtime ticker
	// re-fires daily, so retries happen naturally on the next pass.
	if (InFlight >= MAX_INFLIGHT) { return; }
	++InFlight;

	TSharedRef<IHttpRequest> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Url);
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("User-Agent"),
		TEXT("BlenderAssetBrowser/0.1 (one-shot update check)"));
	Req->SetTimeout(REQ_TIMEOUT_S);
	// Security audit fix: refuse cross-host redirects. The default UE HTTP
	// client follows 30x; if a Fab URL points to a CDN that has been
	// hijacked or redirects to an attacker domain, we'd otherwise blindly
	// parse the response. Mitigation: refuse if the final URL host differs
	// from the requested host. (Pre-check during OnComplete.)
	const FString RequestedHost = FGenericPlatformHttp::GetUrlDomain(Url);
	Req->OnHeaderReceived().BindLambda(
		[RequestedHost](FHttpRequestPtr R, const FString&, const FString&)
		{
			if (!R.IsValid()) { return; }
			const FString FinalUrl = R->GetURL();
			const FString FinalHost = FGenericPlatformHttp::GetUrlDomain(FinalUrl);
			if (!FinalHost.Equals(RequestedHost, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogUpdateChecker, Warning,
					TEXT("Fab update: refused cross-host redirect %s -> %s"),
					*RequestedHost, *FinalHost);
				R->CancelRequest();
			}
		});

	Req->OnProcessRequestComplete().BindLambda(
		[this, AssetId, KnownVersion]
		(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
		{
			InFlight = FMath::Max(0, InFlight - 1);

			if (!bSucceeded || !Response.IsValid()) { return; }
			if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300) { return; }

			const FString Body = Response->GetContentAsString();
			if (Body.Len() == 0 || Body.Len() > MAX_BODY_BYTES)
			{
				UE_LOG(LogUpdateChecker, Verbose, TEXT("Fab body too large/empty for asset %lld."), AssetId);
				return;
			}

			const FString Latest = ExtractVersion(Body);
			if (Latest.IsEmpty())
			{
				UE_LOG(LogUpdateChecker, Verbose, TEXT("Fab page version not detectable for asset %lld."), AssetId);
				return;
			}
			if (!IsNewer(KnownVersion, Latest)) { return; }

			UAssetLibrarySubsystem* Sub = GetSub();
			if (!Sub || !Sub->IsReady()) { return; }
			FAssetLibraryDatabase* Db = Sub->GetDatabase();
			if (!Db) { return; }

			Db->Execute(
				TEXT("UPDATE assets SET latest_version=?, update_state='update_available' "
				     "WHERE id=?"),
				{ B_Text(Latest), B_Int(AssetId) });
			UE_LOG(LogUpdateChecker, Log,
				TEXT("Asset %lld: %s -> %s (update flagged)."),
				AssetId, *KnownVersion, *Latest);

			// Best-effort Discord notify. Quietly no-ops if no webhook configured.
			FString AssetName;
			Db->QueryRows(
				TEXT("SELECT asset_name FROM assets WHERE id=?"),
				{ B_Int(AssetId) },
				[&AssetName](const BAB::FRow& R) -> bool { AssetName = R.GetText(0); return false; });
			if (!AssetName.IsEmpty())
			{
				FDiscordNotifier::Post(FString::Printf(
					TEXT(":package: Update available for **%s**: %s → %s"),
					*AssetName, *KnownVersion, *Latest));
			}
		});

	Req->ProcessRequest();
}
