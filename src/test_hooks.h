#pragma once

// Fault injection exists only in the dedicated robustness test build.
#ifdef TIGER_SNIP_TESTING
namespace snip::testing
{
inline void (*graphicsCheckpoint)(unsigned) = nullptr;
inline void (*settingsCheckpoint)(unsigned) = nullptr;
inline void (*callbackCheckpoint)(const char *, unsigned) = nullptr;
inline void (*errorSink)(const char *) = nullptr;
} // namespace snip::testing
#endif
