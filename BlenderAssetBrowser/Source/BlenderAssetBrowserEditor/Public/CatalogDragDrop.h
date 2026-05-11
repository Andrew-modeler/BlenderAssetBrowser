// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");
//
// Drag-drop payload for catalog tree reorder/reparent.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"

class FCatalogDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FCatalogDragDropOp, FDragDropOperation)

	static TSharedRef<FCatalogDragDropOp> New(int64 InCatalogId);

	int64 GetCatalogId() const { return CatalogId; }

private:
	int64 CatalogId = 0;
};
