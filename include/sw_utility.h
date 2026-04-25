#ifndef SW_UTILITY_H
#define SW_UTILITY_H

#include "sw_export.h"
#include "syphax/s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

SW_API b8 sw_matches_query(const c8* text, const c8* query, b8 case_sensitive);

#ifdef __cplusplus
}
#endif

#endif
