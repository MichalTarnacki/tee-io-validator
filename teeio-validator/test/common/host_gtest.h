/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
#ifndef HOST_GTEST_H
#define HOST_GTEST_H

#include <type_traits>

#include "gtest/gtest.h"
#include "host_check.h"

/* The body is unwound with longjmp on failure: keep it free of C++ objects
 * with non-trivial destructors. */
template <typename Body>
::testing::AssertionResult HostRun(Body &&body)
{
  const char *failure = host_check_run(
    [](void *argument) {
      (*static_cast<std::remove_reference_t<Body> *>(argument))();
    },
    &body);
  if (failure == nullptr) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << failure;
}

#define EXPECT_HOST_PASS(...) EXPECT_TRUE(HostRun([&] { __VA_ARGS__; }))

#endif
