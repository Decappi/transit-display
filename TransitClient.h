// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#pragma once

#include "TransitConfig.h"
#include "TransitState.h"

bool TransitClient_fetch(const TransitConfig& cfg, TransitState& state);
bool TransitClient_loadCache(TransitState& state);
bool TransitClient_saveCache(const TransitState& state);
