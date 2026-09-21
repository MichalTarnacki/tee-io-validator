/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
/* Production PCI32 helpers with pread/pwrite/exit replaced by test doubles. */
#include <tuple>

#include "host_chunk_fault.h"
#include "host_gtest.h"

namespace {

class PciIoResultTest : public ::testing::TestWithParam<std::tuple<bool, int>> {};

TEST_P(PciIoResultTest, OnlyCompleteDwordSucceeds)
{
  EXPECT_HOST_PASS(pci_io_test_check(std::get<0>(GetParam()),
                                     std::get<1>(GetParam()), false));
}

/* Write/read x syscall result -1..4. */
INSTANTIATE_TEST_SUITE_P(Syscall, PciIoResultTest,
                         ::testing::Combine(::testing::Bool(),
                                            ::testing::Range(-1, 5)));

class PciIoEintrTest : public ::testing::TestWithParam<bool> {};

TEST_P(PciIoEintrTest, RetriesAfterEintr)
{
  EXPECT_HOST_PASS(pci_io_test_check(GetParam(), 4, true));
}

INSTANTIATE_TEST_SUITE_P(Write, PciIoEintrTest, ::testing::Bool());

}  // namespace
