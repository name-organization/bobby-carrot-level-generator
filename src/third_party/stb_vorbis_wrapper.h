// stb_vorbis is C, so its declarations need C linkage when pulled into a C++
// translation unit. The implementation lives in stb_vorbis_impl.c, compiled
// as C exactly once.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#ifdef __cplusplus
}
#endif
