/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
/* Production chunk driver with real FI, DOE framing and CHUNK_SEND responder;
 * only recovery's high-level libspdm operations are mocked. */
#include "host_chunk_fault.h"
#include "host_gtest.h"

namespace {

constexpr uint8_t kSpdm12 = 0x12;
constexpr uint8_t kSpdm14 = 0x14;

class ChunkVersionTest : public ::testing::TestWithParam<uint8_t> {};

TEST(ChunkFaultTest, DedicatedDriversDispatchExactly)
{
  EXPECT_HOST_PASS(chunk_test_dedicated_dispatch());
}

TEST_P(ChunkVersionTest, OversizedFirstChunkGetsEarlyInvalidRequest)
{
  EXPECT_HOST_PASS(chunk_test_oversized(GetParam()));
}

TEST_P(ChunkVersionTest, LastChunkWithheldForTwoToNineChunks)
{
  for (unsigned chunks = 2; chunks <= 9; chunks++) {
    SCOPED_TRACE(chunks);
    EXPECT_HOST_PASS(chunk_test_missing(chunks, GetParam()));
  }
}

INSTANTIATE_TEST_SUITE_P(Spdm, ChunkVersionTest,
                         ::testing::Values(kSpdm12, kSpdm14));

class ChunkRecoveryTest : public ::testing::TestWithParam<unsigned> {};

TEST_P(ChunkRecoveryTest, AbandonKeepsOutcomeAndPropagatesRecoveryFailure)
{
  EXPECT_HOST_PASS(chunk_test_abandon_and_recovery(GetParam()));
}

/* 0 = full recovery, 1..5 = failing recovery step. */
INSTANTIATE_TEST_SUITE_P(FailingStep, ChunkRecoveryTest, ::testing::Range(0u, 6u));

TEST(CertificateSignatureTest, BoundedDerRejectsMalformedLengths)
{
  EXPECT_HOST_PASS(chunk_test_der_bounds());
}

TEST(CertificateSignatureTest, MisconfiguredMutationFailsClosed)
{
  EXPECT_HOST_PASS(chunk_test_signature_fail_closed());
}

TEST(CertificateSignatureTest, RejectingPositiveControlFailsWithoutInjecting)
{
  EXPECT_HOST_PASS(chunk_test_rejecting_positive_control());
}

}  // namespace
