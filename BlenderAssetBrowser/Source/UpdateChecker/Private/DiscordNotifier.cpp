// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "DiscordNotifier.h"
#include "UpdateCheckerModule.h"
#include "AssetLibrarySettings.h"
#include "AssetLibraryTypes.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace
{
	/** Discord webhook URL must look like https://discord.com/api/webhooks/<id>/<token>.
	 *  We hard-check the prefix to refuse arbitrary URLs. */
	bool IsAllowedWebhook(const FString& Url)
	{
		if (Url.IsEmpty() || Url.Len() > 512) { return false; }
		const bool bPrefix =
			Url.StartsWith(TEXT("https://discord.com/api/webhooks/"), ESearchCase::IgnoreCase) ||
			Url.StartsWith(TEXT("https://discordapp.com/api/webhooks/"), ESearchCase::IgnoreCase);
		if (!bPrefix) { return false; }
		// Block obvious injection / control chars.
		for (TCHAR C : Url) { if (C < 0x20 || C == TEXT('\n')) { return false; } }
		return true;
	}

	/** Encode a string as JSON-safe inline value (no surrounding quotes). */
	FString EscapeJson(const FString& S)
	{
		FString Out;
		Out.Reserve(S.Len() + 8);
		for (TCHAR C : S)
		{
			switch (C)
			{
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\n'): Out += TEXT("\\n");  break;
			case TEXT('\r'): Out += TEXT("\\r");  break;
			case TEXT('\t'): Out += TEXT("\\t");  break;
			default:
				if (C < 0x20) { Out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(C)); }
				else          { Out.AppendChar(C); }
			}
		}
		return Out;
	}
}

void FDiscordNotifier::Post(const FString& Message)
{
	const UAssetLibrarySettings* S = GetDefault<UAssetLibrarySettings>();
	if (!S) { return; }

	const FString Url = S->DiscordWebhookUrl;
	if (!IsAllowedWebhook(Url))
	{
		UE_LOG(LogUpdateChecker, Verbose,
			TEXT("DiscordNotifier: webhook URL missing or rejected. Skipping."));
		return;
	}

	FString Trimmed = Message;
	if (Trimmed.Len() > 1900) { Trimmed = Trimmed.Left(1900) + TEXT("..."); }
	const FString Body = FString::Printf(TEXT("{\"content\":\"%s\"}"), *EscapeJson(Trimmed));

	TSharedRef<IHttpRequest> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Url);
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("User-Agent"),   TEXT("BlenderAssetBrowser/0.1"));
	Req->SetContentAsString(Body);
	Req->SetTimeout(10.0f);
	Req->OnProcessRequestComplete().BindLambda(
		[](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			if (!bOk || !Resp.IsValid()) { return; }
			if (Resp->GetResponseCode() < 200 || Resp->GetResponseCode() >= 300)
			{
				UE_LOG(LogUpdateChecker, Warning,
					TEXT("Discord webhook POST returned %d."), Resp->GetResponseCode());
			}
		});
	Req->ProcessRequest();
}
