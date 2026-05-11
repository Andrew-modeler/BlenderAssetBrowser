// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Modal dialog shown when the user copies a library asset across projects.
// Surfaces the transitive dependency closure (count + approximate size) and
// lets the user pick: copy with deps, copy single, or cancel.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "DependencyCopyHelper.h"

class SDependencyCopyDialog : public SCompoundWidget
{
public:
	enum class EChoice : uint8 { Cancel, CopySingle, CopyWithDeps };

	SLATE_BEGIN_ARGS(SDependencyCopyDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FString& InRootPackage, const FString& InDestPath);

	/** Show as a modal and return the user's choice + the closure that was inspected. */
	static EChoice ShowModal(const FString& RootPackage, const FString& DestPath, FDependencyClosure& OutClosure);

private:
	EChoice  Result   = EChoice::Cancel;
	FString  Root;
	FString  Dest;
	FDependencyClosure Closure;
};
