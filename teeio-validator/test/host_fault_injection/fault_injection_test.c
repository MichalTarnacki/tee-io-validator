/**
 *  Copyright Notice:
 *  Copyright 2026 Intel. All rights reserved.
 *  License: BSD 3-Clause License.
 **/

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "teeio_fault_injection.h"

#define TEST_MESSAGE_SIZE 16
#define TEST_DOE_TYPE_SPDM 1
#define TEST_SPDM_CODE_GET_VERSION 0x84

static int m_failures;
static int m_tests;

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #expression);                                               \
            m_failures++;                                                       \
        }                                                                       \
    } while (false)

static void make_message(uint8_t *message, uint8_t marker)
{
    memset(message, 0, TEST_MESSAGE_SIZE);
    message[0] = 0x01;
    message[2] = TEST_DOE_TYPE_SPDM;
    message[4] = TEST_MESSAGE_SIZE / 4;
    message[8] = 0x14;
    message[9] = TEST_SPDM_CODE_GET_VERSION;
    message[12] = marker;
}

static teeio_fault_rule_t *init_rule(teeio_fault_config_t *config,
                                     teeio_fault_action_t action)
{
    teeio_fault_rule_t *rule;

    memset(config, 0, sizeof(*config));
    config->enabled = true;
    strcpy(config->selected_scenario, "test_case");
    config->rule_count = 1;
    rule = &config->rules[0];
    rule->id = 1;
    rule->enabled = true;
    strcpy(rule->scenario, "test_case");
    strcpy(rule->expected, "controlled_result");
    rule->direction = TEEIO_FAULT_DIRECTION_SEND;
    rule->doe_type = TEST_DOE_TYPE_SPDM;
    rule->spdm_code = TEST_SPDM_CODE_GET_VERSION;
    rule->occurrence = 1;
    rule->action = action;
    return rule;
}

static void test_parse_helpers(void)
{
    teeio_fault_action_t action;
    teeio_fault_direction_t direction;
    uint8_t pattern[4];

    m_tests++;
    CHECK(teeio_fault_parse_action("reorder", &action));
    CHECK(action == TEEIO_FAULT_ACTION_REORDER);
    CHECK(!teeio_fault_parse_action("invalid", &action));
    CHECK(teeio_fault_parse_direction("receive", &direction));
    CHECK(direction == TEEIO_FAULT_DIRECTION_RECEIVE);
    CHECK(!teeio_fault_parse_direction("both", &direction));
    CHECK(teeio_fault_parse_pattern("0xde:ad be:ef", pattern,
                                    sizeof(pattern)) == 4);
    CHECK(pattern[0] == 0xde && pattern[1] == 0xad &&
          pattern[2] == 0xbe && pattern[3] == 0xef);
    CHECK(teeio_fault_parse_pattern("abc", pattern, sizeof(pattern)) == -1);
}

static void test_rule_validation(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    char error[128];

    m_tests++;
    rule = init_rule(&config, TEEIO_FAULT_ACTION_SET);
    CHECK(!teeio_fault_validate_rule(rule, error, sizeof(error)));
    CHECK(strstr(error, "requires a pattern") != NULL);
    rule->pattern[0] = 0xaa;
    rule->pattern_size = 1;
    CHECK(teeio_fault_validate_rule(rule, error, sizeof(error)));
}

static void test_set_selector_and_occurrence(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    teeio_fault_result_t result;
    uint8_t message[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(message, 0x10);
    rule = init_rule(&config, TEEIO_FAULT_ACTION_SET);
    rule->occurrence = 2;
    rule->offset = 10;
    rule->pattern[0] = 0x5a;
    rule->pattern_size = 1;
    teeio_fault_init(&config);

    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_RECEIVE, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_PASS);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_PASS);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_MUTATE);
    CHECK(result.message[10] == 0x5a);
    CHECK(message[10] == 0);
    CHECK(teeio_fault_fire_count() == 1);
    CHECK(strstr(teeio_fault_audit_record(), "\"status\":\"fired\"") != NULL);
    CHECK(strstr(teeio_fault_audit_record(),
                 "\"expected\":\"controlled_result\"") != NULL);
    teeio_fault_record_actual("spdm_error_0x01");
    CHECK(strstr(teeio_fault_result_record(),
                 "\"actual\":\"spdm_error_0x01\"") != NULL);
    CHECK(strstr(teeio_fault_result_record(), "\"rule_id\":1") != NULL);
    CHECK(strstr(teeio_fault_result_record(),
                 "\"disposition\":\"mutate\"") != NULL);
    CHECK(strstr(teeio_fault_result_record(), "\"match_count\":2") != NULL);
}

static void test_xor(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    teeio_fault_result_t result;
    uint8_t message[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(message, 0x11);
    rule = init_rule(&config, TEEIO_FAULT_ACTION_XOR);
    rule->offset = 12;
    rule->pattern[0] = 0xff;
    rule->pattern_size = 1;
    teeio_fault_init(&config);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_MUTATE);
    CHECK(teeio_fault_should_record_response());
    CHECK(result.message[12] == (uint8_t)(0x11 ^ 0xff));

    rule->offset = 1;
    rule->offset_from_end = true;
    teeio_fault_reset();
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_MUTATE);
    CHECK(result.message[15] == 0xff);

    rule->direction = TEEIO_FAULT_DIRECTION_RECEIVE;
    teeio_fault_reset();
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_RECEIVE, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_MUTATE);
    CHECK(!teeio_fault_should_record_response());
}

static void test_resize_and_atomic_rejection(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    teeio_fault_result_t result;
    uint8_t message[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(message, 0x12);
    rule = init_rule(&config, TEEIO_FAULT_ACTION_TRUNCATE);
    rule->size = 4;
    teeio_fault_init(&config);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_MUTATE);
    CHECK(result.message_size == 12);
    CHECK(result.message[4] == 3);

    rule->action = TEEIO_FAULT_ACTION_TRUNCATE_TO;
    rule->size = 12;
    teeio_fault_reset();
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_MUTATE);
    CHECK(result.message_size == 12);
    CHECK(result.message[4] == 3);

    rule->action = TEEIO_FAULT_ACTION_TRUNCATE;
    rule->size = 1;
    teeio_fault_reset();
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_ERROR);
    CHECK(result.message == message);
    CHECK(result.message_size == sizeof(message));

    rule->action = TEEIO_FAULT_ACTION_EXTEND;
    rule->size = 0;
    rule->offset = 8;
    rule->declared_length = 0x12345678;
    rule->pattern[0] = 0xde;
    rule->pattern[1] = 0xad;
    rule->pattern[2] = 0xbe;
    rule->pattern[3] = 0xef;
    rule->pattern_size = 4;
    teeio_fault_reset();
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message) + 4);
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_MUTATE);
    CHECK(result.message_size == 20);
    CHECK(result.message[4] == 5);
        CHECK(result.message[8] == 0x78 && result.message[9] == 0x56 &&
            result.message[10] == 0x34 && result.message[11] == 0x12);
    CHECK(result.message[16] == 0xde && result.message[19] == 0xef);
}

static void test_declared_length(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    teeio_fault_result_t result;
    uint8_t message[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(message, 0x13);
    rule = init_rule(&config, TEEIO_FAULT_ACTION_SET_DECLARED_LENGTH);
    rule->declared_length = 0x12345678;
    teeio_fault_init(&config);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_MUTATE);
    CHECK(result.message[4] == 0x78 && result.message[5] == 0x56 &&
          result.message[6] == 0x34 && result.message[7] == 0x12);
    CHECK(strstr(teeio_fault_audit_record(),
                 "\"doe_length\":305419896") != NULL);
}

static void test_drop_and_abandon(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    teeio_fault_result_t result;
    uint8_t message[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(message, 0x14);
    rule = init_rule(&config, TEEIO_FAULT_ACTION_DROP);
    teeio_fault_init(&config);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_DROP);
    CHECK(result.message == NULL && result.message_size == 0);

    rule->action = TEEIO_FAULT_ACTION_ABANDON;
    teeio_fault_reset();
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_ABANDON);
}

static void test_duplicate(void)
{
    teeio_fault_config_t config;
    teeio_fault_result_t result;
    uint8_t message[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(message, 0x15);
    init_rule(&config, TEEIO_FAULT_ACTION_DUPLICATE);
    teeio_fault_init(&config);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_DUPLICATE);
    CHECK(result.secondary_message_size == sizeof(message));
    CHECK(memcmp(result.message, result.secondary_message,
                 sizeof(message)) == 0);
}

static void test_replay(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    teeio_fault_result_t result;
    uint8_t first[TEST_MESSAGE_SIZE];
    uint8_t second[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(first, 0x16);
    make_message(second, 0x26);
    rule = init_rule(&config, TEEIO_FAULT_ACTION_REPLAY);
    rule->occurrence = 2;
    rule->offset = 8;
    rule->pattern[0] = 0x13;
    rule->pattern_size = 1;
    teeio_fault_init(&config);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, first,
                               sizeof(first), sizeof(first));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_PASS);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, second,
                               sizeof(second), sizeof(second));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_REPLAY);
    CHECK(result.message[8] == 0x13);
    CHECK(result.message[12] == 0x16);
}

static void test_plain_secured_selector(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    teeio_fault_result_t result;
    uint8_t message[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(message, 0x19);
    message[2] = TEEIO_FAULT_DOE_TYPE_PLAIN_SECURED_SPDM;
    message[9] = 0xe5;
    rule = init_rule(&config, TEEIO_FAULT_ACTION_XOR);
    rule->doe_type = TEEIO_FAULT_DOE_TYPE_PLAIN_SECURED_SPDM;
    rule->spdm_code = 0xe5;
    rule->offset = 1;
    rule->offset_from_end = true;
    rule->pattern[0] = 1;
    rule->pattern_size = 1;
    teeio_fault_init(&config);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_MUTATE);
    CHECK(result.message[15] == 1);
}

static void test_reorder(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    teeio_fault_result_t result;
    uint8_t first[TEST_MESSAGE_SIZE];
    uint8_t second[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(first, 0x17);
    make_message(second, 0x27);
    rule = init_rule(&config, TEEIO_FAULT_ACTION_REORDER);
    rule->occurrence = TEEIO_FAULT_OCCURRENCE_ALL;
    teeio_fault_init(&config);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, first,
                               sizeof(first), sizeof(first));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_HOLD);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, second,
                               sizeof(second), sizeof(second));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_REORDER);
    CHECK(result.message[12] == 0x27);
    CHECK(result.secondary_message[12] == 0x17);
}

static void test_reset(void)
{
    teeio_fault_config_t config;
    teeio_fault_rule_t *rule;
    teeio_fault_result_t result;
    uint8_t message[TEST_MESSAGE_SIZE];

    m_tests++;
    make_message(message, 0x18);
    rule = init_rule(&config, TEEIO_FAULT_ACTION_DROP);
    rule->occurrence = 2;
    teeio_fault_init(&config);
    teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                      sizeof(message), sizeof(message));
    teeio_fault_reset();
    CHECK(!teeio_fault_scenario_fired());
    CHECK(teeio_fault_fire_count() == 0);
    result = teeio_fault_apply(TEEIO_FAULT_DIRECTION_SEND, message,
                               sizeof(message), sizeof(message));
    CHECK(result.disposition == TEEIO_FAULT_DISPOSITION_PASS);
}

int main(void)
{
    test_parse_helpers();
    test_rule_validation();
    test_set_selector_and_occurrence();
    test_xor();
    test_resize_and_atomic_rejection();
    test_declared_length();
    test_drop_and_abandon();
    test_duplicate();
    test_replay();
    test_plain_secured_selector();
    test_reorder();
    test_reset();

    if (m_failures != 0) {
        fprintf(stderr, "%d of %d fault-injection tests failed\n",
                m_failures, m_tests);
        return 1;
    }
    printf("All %d fault-injection tests passed\n", m_tests);
    return 0;
}