/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
/* Real requester send path: libspdm_send_request -> chunk_encode ->
 * device_doe_send_message -> second FI -> PCI DWORD writes -> PCAP sink. */
#include <tuple>

#include "host_chunk_fault.h"
#include "host_gtest.h"

namespace {

using DoeParam = std::tuple<uint8_t, uint32_t, bool>;

class DoeSendTest : public ::testing::TestWithParam<DoeParam> {};

TEST_P(DoeSendTest, MailboxMatchesEncodedChunkAndPcap)
{
  EXPECT_HOST_PASS(doe_test_mailbox_boundary(std::get<0>(GetParam()),
                                             std::get<1>(GetParam()),
                                             std::get<2>(GetParam())));
}

TEST_P(DoeSendTest, RealChunkLoopStopsAfterFirstAck)
{
  EXPECT_HOST_PASS(doe_test_real_chunk_loop(std::get<0>(GetParam()),
                                            std::get<1>(GetParam()),
                                            std::get<2>(GetParam())));
}

/* Version x DataTransferSize x (injected | early-error ACK). */
INSTANTIATE_TEST_SUITE_P(
  Spdm, DoeSendTest,
  ::testing::Combine(::testing::Values<uint8_t>(0x12, 0x14),
                     ::testing::Values<uint32_t>(256, 476),
                     ::testing::Bool()));

}  // namespace
