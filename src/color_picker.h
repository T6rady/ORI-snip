#pragma once
#include "model.h"
#include <windows.h>

namespace snip
{
std::wstring colorHex(Color value);
std::optional<Color> parseColorHex(std::wstring text);
Color spectrumColor(float hue, float saturation, float value);
std::optional<Color> pickPaletteColor(HINSTANCE instance, HWND owner, Color value,
                                      bool editing = false, void (*test)(HWND) = nullptr,
                                      const wchar_t *title = nullptr,
                                      const wchar_t *action = nullptr);
#ifdef ORI_SNIP_TESTING
LRESULT CALLBACK spectrumProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
INT_PTR CALLBACK colorPickerProcedure(HWND, UINT, WPARAM, LPARAM);
#endif
} // namespace snip
