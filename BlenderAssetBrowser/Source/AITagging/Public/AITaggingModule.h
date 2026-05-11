// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAITagging, Log, All);

class FAITaggingModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
