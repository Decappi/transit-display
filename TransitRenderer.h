// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#pragma once

#include "AmoledMatrix.h"
#include "TransitState.h"

void TransitRenderer_renderVehiclesDirect(AmoledMatrix& matrix, const TransitState& state, const TransitState& prev, float t);
void TransitRenderer_renderStatic(AmoledMatrix& matrix, const TransitState& state);
void TransitRenderer_renderError(AmoledMatrix& matrix, const char* message);
void TransitRenderer_renderTooltipDirect(AmoledMatrix& matrix, const char* text, int16_t tapX, int16_t tapY);
