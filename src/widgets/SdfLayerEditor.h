#pragma once
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>

//
void DrawSdfLayerIdentity(const SdfLayerRefPtr &layer, const SdfPath &);

//
void DrawSdfLayerMetadata(const SdfLayerRefPtr &layer);

///
void DrawLayerActionPopupMenu(SdfLayerHandle layer);

void DrawLayerNavigation(SdfLayerRefPtr layer);

void DrawLayerSublayerStack(SdfLayerRefPtr layer);


void DrawSdfLayerEditorMenuBar(SdfLayerRefPtr layer);
