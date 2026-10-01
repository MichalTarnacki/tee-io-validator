/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
/* Campaign INIs through the actual C parser/CLI/driver registration and the
 * production exit policy. Each validator run is a forked child (EXPECT_EXIT):
 * validator_entry() is a main() with process-wide state. */
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "gtest/gtest.h"

extern "C" int validator_entry(int argc, char **argv);

namespace {

namespace fs = std::filesystem;
using Section = std::map<std::string, std::string>;
using Ini = std::map<std::string, Section>;

constexpr int kValidatorFailed = 255;
constexpr const char *kReachedRun = "HOST: parsed catalog; PCI execution replaced";

std::string Trim(const std::string &text)
{
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string::npos) return "";
  return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

Ini ReadIni(const fs::path &path)
{
  Ini ini;
  std::ifstream stream(path);
  std::string line, section;
  while (std::getline(stream, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = line.substr(1, line.size() - 2);
      ini[section];
      continue;
    }
    const auto equals = line.find('=');
    if (equals != std::string::npos) {
      ini[section][Trim(line.substr(0, equals))] = Trim(line.substr(equals + 1));
    }
  }
  return ini;
}

std::string Value(const Ini &ini, const std::string &section, const std::string &key)
{
  const auto s = ini.find(section);
  if (s == ini.end()) return "";
  const auto k = s->second.find(key);
  return k == s->second.end() ? "" : k->second;
}

int RunValidator(const fs::path &ini, const std::string &driver,
                 const char *mode, const char *case_id)
{
  if (mode != nullptr) setenv("TEEIO_HOST_MODE", mode, 1);
  else unsetenv("TEEIO_HOST_MODE");
  if (case_id != nullptr) setenv("TEEIO_HOST_CASE", case_id, 1);
  else unsetenv("TEEIO_HOST_CASE");
  std::vector<std::string> args = {"teeio_validator", "-f", ini.string(),
                                   "-t", "1", "-c", "1", "-s", driver,
                                   "-l", "verbose"};
  std::vector<char *> argv;
  for (auto &arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  return validator_entry(static_cast<int>(args.size()), argv.data());
}

#define EXPECT_VALIDATOR_EXIT(code, ini, driver, mode, case_id)              \
  EXPECT_EXIT(std::exit(RunValidator(ini, driver, mode, case_id)),           \
              ::testing::ExitedWithCode(code), kReachedRun)

class CatalogTest : public ::testing::Test {
 protected:
  void SetUp() override
  {
    const char *catalog = std::getenv("TEEIO_FAULT_CATALOG_DIR");
    if (catalog == nullptr) {
      GTEST_SKIP() << "set TEEIO_FAULT_CATALOG_DIR to the campaign INI directory";
    }
    for (const auto &entry : fs::directory_iterator(catalog)) {
      if (entry.path().extension() == ".ini") inis_.push_back(entry.path());
    }
    std::sort(inis_.begin(), inis_.end());
  }

  /* The single catalog INI whose campaign drives Fault.<case_id>. */
  fs::path DriverIni(int case_id) const
  {
    const std::string driver = "Fault." + std::to_string(case_id);
    std::vector<fs::path> found;
    for (const auto &path : inis_) {
      if (Value(ReadIni(path), "Campaign", "driver") == driver) found.push_back(path);
    }
    EXPECT_EQ(found.size(), 1u) << driver;
    if (found.size() != 1) return {};
    EXPECT_EQ(Value(ReadIni(found[0]), "TestSuite_1", "Fault"), std::to_string(case_id));
    return found[0];
  }

  std::vector<fs::path> inis_;
};

TEST_F(CatalogTest, EveryIniReachesExecutionThroughParserAndCli)
{
  ASSERT_EQ(inis_.size(), 34u);
  for (const auto &path : inis_) {
    SCOPED_TRACE(path.filename().string());
    std::string driver = Value(ReadIni(path), "Campaign", "driver");
    EXPECT_VALIDATOR_EXIT(kValidatorFailed, path, driver.empty() ? "Version.1" : driver,
                          nullptr, nullptr);
  }
}

TEST_F(CatalogTest, CertificateSignatureCandidateSlice)
{
  const Ini ini = ReadIni(DriverIni(1));
  EXPECT_EQ(Value(ini, "Campaign", "allowed_actual"), "spdm_error_0x05");
  EXPECT_EQ(Value(ini, "FaultRule_1", "occurrence"), "1");
  EXPECT_EQ(std::stoul(Value(ini, "FaultRule_1", "offset"), nullptr, 0), 12u);
  EXPECT_EQ(Value(ini, "FaultRule_1", "action"), "xor");
  EXPECT_EQ(Value(ini, "FaultRule_1", "pattern"), "01");
}

TEST_F(CatalogTest, StrictChunkTransferConfigurations)
{
  const struct {
    int case_id;
    const char *actual, *action, *code;
  } expected[] = {
    {2, "spdm_error_0x01", "extend", "0x85"},
    {3, "chunk_transfer_abandoned", "abandon", "0x86"},
    {4, "final_chunk_withheld", "drop", "0x85"},
  };
  for (const auto &e : expected) {
    SCOPED_TRACE(e.case_id);
    const Ini ini = ReadIni(DriverIni(e.case_id));
    EXPECT_EQ(Value(ini, "Campaign", "allowed_actual"), e.actual);
    EXPECT_EQ(Value(ini, "FaultRule_1", "doe_type"), "plain_spdm");
    EXPECT_EQ(Value(ini, "FaultRule_1", "occurrence"), "1");
    EXPECT_EQ(Value(ini, "FaultRule_1", "action"), e.action);
    EXPECT_EQ(Value(ini, "FaultRule_1", "spdm_code"), e.code);
    EXPECT_EQ(ini.at("FaultRule_1").count("blocked_reason"), 0u);
    EXPECT_EQ(ini.at("FaultRule_1").count("runnable"), 0u);
    const std::string recovery = Value(ini, "FaultRule_1", "recovery");
    EXPECT_TRUE(recovery.empty() || recovery == "none");
  }
}

TEST_F(CatalogTest, KeyExchangeDuplicateAndPositiveControl)
{
  for (int case_id : {5, 6}) {
    SCOPED_TRACE(case_id);
    const Ini ini = ReadIni(DriverIni(case_id));
    EXPECT_EQ(Value(ini, "FaultRule_1", "doe_type"), "plain_spdm");
    EXPECT_EQ(Value(ini, "FaultRule_1", "action"), "duplicate");
    EXPECT_EQ(Value(ini, "FaultRule_1", "spdm_code"), "0xe4");
    EXPECT_EQ(Value(ini, "FaultRule_1", "occurrence"), "1");
    EXPECT_EQ(ini.at("FaultRule_1").count("runnable"), 0u);
    EXPECT_EQ(ini.at("FaultRule_1").count("blocked_reason"), 0u);
  }
}

TEST_F(CatalogTest, ZeroFireControlPassesOnlyWithStrictRecords)
{
  const fs::path control = DriverIni(6);
  EXPECT_VALIDATOR_EXIT(0, control, "Fault.6", "pass", nullptr);
  for (const char *mode : {"skipped", "no-primary", "failed", "not-tested",
                           "teardown", "wrong-class", "missing-driver",
                           "non-spdm", "two-controls", "extra-rule", "empty"}) {
    SCOPED_TRACE(mode);
    EXPECT_VALIDATOR_EXIT(kValidatorFailed, control, "Fault.6", mode, nullptr);
  }
}

TEST_F(CatalogTest, ZeroFireExemptionDeniedToOtherDrivers)
{
  for (int case_id : {1, 2, 3, 4, 5, 7}) {
    SCOPED_TRACE(case_id);
    EXPECT_VALIDATOR_EXIT(kValidatorFailed, DriverIni(case_id),
                          "Fault." + std::to_string(case_id), "pass", nullptr);
  }
}

TEST_F(CatalogTest, RealFireCannotHideFailedAssertions)
{
  const fs::path finish_signature = DriverIni(7);
  EXPECT_VALIDATOR_EXIT(kValidatorFailed, finish_signature, "Fault.7", "fired-failed", nullptr);
  EXPECT_VALIDATOR_EXIT(0, finish_signature, "Fault.7", "fired-pass", nullptr);
}

TEST_F(CatalogTest, ResultsScopedToSelectedCaseAndDriver)
{
  const fs::path control = DriverIni(6);
  EXPECT_VALIDATOR_EXIT(kValidatorFailed, control, "Fault.6", "pass", "1");
  EXPECT_VALIDATOR_EXIT(kValidatorFailed, control, "Fault.2", "pass", nullptr);
}

}  // namespace
