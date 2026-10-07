#pragma once

// Fault injection exists only in dedicated test builds.
#ifdef ORI_SNIP_TESTING
namespace snip::testing
{
inline void (*graphicsCheckpoint)(unsigned) = nullptr;
inline void (*settingsCheckpoint)(unsigned) = nullptr;
inline void (*callbackCheckpoint)(const char *, unsigned) = nullptr;
inline void (*errorSink)(const char *) = nullptr;
inline void (*fileSaveCheckpoint)(const wchar_t *, const wchar_t *) = nullptr;
} // namespace snip::testing
#endif
