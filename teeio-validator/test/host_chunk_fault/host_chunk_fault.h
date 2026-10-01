/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
#ifndef HOST_CHUNK_FAULT_H
#define HOST_CHUNK_FAULT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* chunk_fault_test.c */
void chunk_test_dedicated_dispatch(void);
void chunk_test_oversized(uint8_t version);
void chunk_test_missing(unsigned chunks, uint8_t version);
void chunk_test_abandon_and_recovery(unsigned failure);
void chunk_test_der_bounds(void);
void chunk_test_signature_fail_closed(void);
void chunk_test_rejecting_positive_control(void);

#ifdef __cplusplus
}
#endif

#endif
