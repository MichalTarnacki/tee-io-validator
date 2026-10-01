/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
#ifndef HOST_CHECK_H
#define HOST_CHECK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns NULL, or "file:line: expression" of the first failed check. */
const char *host_check_run(void (*body)(void *), void *argument);
__attribute__((noreturn)) void host_check_failed(const char *expression,
                                                 const char *file, int line);

#ifdef __cplusplus
}
#endif

#endif

/* Outside the guard: re-including this after <assert.h> re-applies it. */
#ifndef __cplusplus
#include <assert.h>
#undef assert
#define assert(e) ((e) ? (void)0 : host_check_failed(#e, __FILE__, __LINE__))
#endif
