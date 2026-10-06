#pragma once
#include <cstddef>

namespace snip
{
inline constexpr size_t MaxPaletteColors = 64;
inline constexpr int PaletteFirst = 2000;
inline constexpr int RecentFirst = 2200;
static_assert(PaletteFirst + MaxPaletteColors <= RecentFirst);
} // namespace snip
