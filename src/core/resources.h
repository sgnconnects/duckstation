// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#pragma once
#include "types.h"
#include <array>

namespace Resources {

// Adapted from https://icons8.com/icon/15970/target
constexpr u32 CROSSHAIR_IMAGE_WIDTH = 96;
constexpr u32 CROSSHAIR_IMAGE_HEIGHT = 96;
extern const std::array<u32, CROSSHAIR_IMAGE_WIDTH * CROSSHAIR_IMAGE_HEIGHT> CROSSHAIR_IMAGE_DATA;

constexpr int WINDOW_ICON_WIDTH = 64;
constexpr int WINDOW_ICON_HEIGHT = 64;
extern unsigned int WINDOW_ICON_DATA[WINDOW_ICON_WIDTH * WINDOW_ICON_HEIGHT];

constexpr int PLACEHOLDER_ICON_WIDTH = 128;
constexpr int PLACEHOLDER_ICON_HEIGHT = 96;
extern unsigned int PLACEHOLDER_ICON_DATA[PLACEHOLDER_ICON_WIDTH * PLACEHOLDER_ICON_HEIGHT];

} // namespace Resources
