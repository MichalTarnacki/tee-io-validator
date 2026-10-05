/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
/* In-memory device IO with real FI, DOE, requester, responder, crypto and
 * FINISH; deliberate peer/IO defects are limited to the target exchange. */
#include <unistd.h>

#include <string>
#include <tuple>

#include "host_gtest.h"

extern "C" void key_exchange_test_case(uint8_t spdm_version, const char *mode);

namespace {

using KeyExchangeParam = std::tuple<uint8_t, const char *>;

class KeyExchangeTest : public ::testing::TestWithParam<KeyExchangeParam> {
 protected:
  /* The sample secret library opens key files relative to this directory. */
  static void SetUpTestSuite() { ASSERT_EQ(chdir(HOST_SAMPLE_KEY_DIR), 0); }
};

TEST_P(KeyExchangeTest, DriverOutcomeWireSequenceAndCleanup)
{
  EXPECT_HOST_PASS(key_exchange_test_case(std::get<0>(GetParam()),
                                          std::get<1>(GetParam())));
}

std::string ParamName(const ::testing::TestParamInfo<KeyExchangeParam> &info)
{
  std::string name = "Spdm" + std::to_string(std::get<0>(info.param) >> 4) +
                     std::to_string(std::get<0>(info.param) & 0xf) + "_" +
                     std::get<1>(info.param);
  for (char &c : name) {
    if (c == '-') c = '_';
  }
  return name;
}

INSTANTIATE_TEST_SUITE_P(
  Mode, KeyExchangeTest,
  ::testing::Combine(
    ::testing::Values<uint8_t>(0x12, 0x14),
    ::testing::Values("cache", "duplicate", "missing-trust", "bad-signature",
                      "same-id", "duplicate-bad-signature", "truncated-first",
                      "wrong-version", "session-limit", "no-fire",
                      "wrong-action", "second-timeout", "recovery-fail",
                      "setup-fail")),
  ParamName);

}  // namespace
