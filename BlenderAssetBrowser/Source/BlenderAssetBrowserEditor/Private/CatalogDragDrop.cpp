// Copyright 2026 BlenderAssetBrowser Contributors
// Licensed under the Apache License, Version 2.0 (the "License");

#include "CatalogDragDrop.h"

TSharedRef<FCatalogDragDropOp> FCatalogDragDropOp::New(int64 InCatalogId)
{
	auto Op = MakeShared<FCatalogDragDropOp>();
	Op->CatalogId = InCatalogId;
	Op->Construct();
	return Op;
}
