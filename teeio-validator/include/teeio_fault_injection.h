/**
 *  Copyright Notice:
 *  Copyright 2026 Intel. All rights reserved.
 *  License: BSD 3-Clause License.
 **/

#ifndef __TEEIO_FAULT_INJECTION_H__
#define __TEEIO_FAULT_INJECTION_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEEIO_FAULT_MAX_RULES 64
#define TEEIO_FAULT_MAX_NAME_SIZE 64
#define TEEIO_FAULT_MAX_EXPECTED_SIZE 128
#define TEEIO_FAULT_MAX_PATTERN_SIZE 64
#define TEEIO_FAULT_MAX_MESSAGE_SIZE 0x9000
#define TEEIO_FAULT_MATCH_ANY UINT32_MAX
#define TEEIO_FAULT_OCCURRENCE_ALL 0
#define TEEIO_FAULT_DOE_TYPE_PLAIN_SPDM 0xfd
#define TEEIO_FAULT_DOE_TYPE_PLAIN_SECURED_SPDM 0xfe

typedef enum {
    TEEIO_FAULT_DIRECTION_SEND = 0,
    TEEIO_FAULT_DIRECTION_RECEIVE,
    TEEIO_FAULT_DIRECTION_MAX
} teeio_fault_direction_t;

typedef enum {
    TEEIO_FAULT_ACTION_SET = 0,
    TEEIO_FAULT_ACTION_XOR,
    TEEIO_FAULT_ACTION_TRUNCATE,
    TEEIO_FAULT_ACTION_TRUNCATE_TO,
    TEEIO_FAULT_ACTION_EXTEND,
    TEEIO_FAULT_ACTION_SET_DECLARED_LENGTH,
    TEEIO_FAULT_ACTION_DROP,
    TEEIO_FAULT_ACTION_DUPLICATE,
    TEEIO_FAULT_ACTION_REPLAY,
    TEEIO_FAULT_ACTION_REORDER,
    TEEIO_FAULT_ACTION_ABANDON,
    TEEIO_FAULT_ACTION_MAX
} teeio_fault_action_t;

typedef enum {
    TEEIO_FAULT_DISPOSITION_PASS = 0,
    TEEIO_FAULT_DISPOSITION_MUTATE,
    TEEIO_FAULT_DISPOSITION_DROP,
    TEEIO_FAULT_DISPOSITION_DUPLICATE,
    TEEIO_FAULT_DISPOSITION_REPLAY,
    TEEIO_FAULT_DISPOSITION_HOLD,
    TEEIO_FAULT_DISPOSITION_REORDER,
    TEEIO_FAULT_DISPOSITION_ABANDON,
    TEEIO_FAULT_DISPOSITION_ERROR
} teeio_fault_disposition_t;

typedef struct {
    int id;
    bool enabled;
    char scenario[TEEIO_FAULT_MAX_NAME_SIZE];
    char expected[TEEIO_FAULT_MAX_EXPECTED_SIZE];
    teeio_fault_direction_t direction;
    uint32_t doe_type;
    uint32_t spdm_code;
    uint32_t occurrence;
    teeio_fault_action_t action;
    uint32_t offset;
    bool offset_from_end;
    uint32_t size;
    uint32_t declared_length;
    uint8_t pattern[TEEIO_FAULT_MAX_PATTERN_SIZE];
    uint32_t pattern_size;
    uint32_t match_count;
    uint32_t fire_count;
} teeio_fault_rule_t;

typedef struct {
    bool enabled;
    char selected_scenario[TEEIO_FAULT_MAX_NAME_SIZE];
    uint32_t rule_count;
    teeio_fault_rule_t rules[TEEIO_FAULT_MAX_RULES];
} teeio_fault_config_t;

typedef struct {
    teeio_fault_disposition_t disposition;
    /* Message pointers remain valid until the next teeio_fault_apply call. */
    const uint8_t *message;
    size_t message_size;
    const uint8_t *secondary_message;
    size_t secondary_message_size;
    int rule_id;
} teeio_fault_result_t;

const char *teeio_fault_direction_name(teeio_fault_direction_t direction);
bool teeio_fault_parse_direction(const char *name,
                                 teeio_fault_direction_t *direction);

const char *teeio_fault_action_name(teeio_fault_action_t action);
bool teeio_fault_parse_action(const char *name, teeio_fault_action_t *action);
const char *teeio_fault_disposition_name(teeio_fault_disposition_t disposition);

int teeio_fault_parse_pattern(const char *value, uint8_t *pattern,
                              size_t pattern_capacity);

bool teeio_fault_validate_rule(const teeio_fault_rule_t *rule, char *error,
                               size_t error_size);

void teeio_fault_init(teeio_fault_config_t *config);
void teeio_fault_reset(void);

teeio_fault_result_t teeio_fault_apply(teeio_fault_direction_t direction,
                                       const void *message,
                                       size_t message_size,
                                       size_t message_capacity);

bool teeio_fault_scenario_fired(void);
uint32_t teeio_fault_fire_count(void);
const char *teeio_fault_audit_record(void);
void teeio_fault_record_actual(const char *actual);
const char *teeio_fault_result_record(void);

#endif