/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
/* Test the production static driver directly. Only recovery's high-level
 * operations are mocked; FI, DOE framing, and CHUNK_SEND responder are real. */
#define libspdm_init_connection host_init_connection
#define libspdm_get_certificate host_get_certificate
#define libspdm_start_session host_start_session
#define libspdm_heartbeat host_heartbeat
#define libspdm_stop_session host_stop_session
#define libspdm_set_certificate host_set_certificate
#include "../../library/spdm_test_lib/test_case/test_case_fault.c"
#include "internal/libspdm_responder_lib.h"
#include "host_chunk_fault.h"
#include "../common/host_check.h"

static unsigned assertions_failed;
static unsigned recovery_step;
static unsigned fail_step;
static void *expected_context;
static IDE_TEST_FAULT_CONFIG config;
static bool sig_control;
static unsigned sig_control_calls;
static uint8_t sig_chain[LIBSPDM_MAX_CERT_CHAIN_SIZE];
static size_t sig_chain_size;
/* Existing requester debuglib references these application-owned settings. */
bool g_libspdm_log = false;
FILE *m_logfile;
int g_debug_level = TEEIO_DEBUG_ERROR;

bool teeio_record_assertion_result(int cls, int id, int assertion,
  ide_common_test_case_assertion_type_t type, teeio_test_result_t result,
  const char *format, ...)
{
  assertions_failed += result == TEEIO_TEST_RESULT_FAILED;
  return true;
}

enum { DISPATCH_KEY_EXCHANGE = 1, DISPATCH_FINISH_SIGNATURE };
static unsigned dispatch_kind, dispatch_setup, dispatch_run, dispatch_teardown;
static unsigned common_teardown;
static bool dispatch_setup_result;
void spdm_test_case_common_teardown(void *context) { common_teardown++; }
bool teeio_spdm_apply_version_override(void *context)
{
  assert(!"dedicated dispatch fell through to chunk setup"); return false;
}
void teeio_spdm_log_negotiated_version(void *context)
{
  assert(!"dedicated dispatch fell through to chunk setup");
}

/* Separate drivers are tested with real libspdm in their own harnesses.
 * These routing probes fail loudly if reached by an ordinary chunk case. */
bool spdm_test_case_fault_key_exchange_setup(void *context)
{
  assert(dispatch_kind == DISPATCH_KEY_EXCHANGE); dispatch_setup++; return dispatch_setup_result;
}
void spdm_test_case_fault_key_exchange_run(void *context)
{
  assert(dispatch_kind == DISPATCH_KEY_EXCHANGE); dispatch_run++;
}
void spdm_test_case_fault_key_exchange_teardown(void *context)
{
  assert(dispatch_kind == DISPATCH_KEY_EXCHANGE); dispatch_teardown++;
}
bool spdm_test_case_fault_mut_auth_setup(void *context)
{
  assert(dispatch_kind == DISPATCH_FINISH_SIGNATURE); dispatch_setup++; return dispatch_setup_result;
}
void spdm_test_case_fault_mut_auth_run(void *context)
{
  assert(dispatch_kind == DISPATCH_FINISH_SIGNATURE); dispatch_run++;
}

void chunk_test_dedicated_dispatch(void)
{
  const teeio_fault_driver_t drivers[] = {TEEIO_FAULT_DRIVER_KEY_EXCHANGE_DUPLICATE,
    TEEIO_FAULT_DRIVER_KEY_EXCHANGE_CACHED_CERT, TEEIO_FAULT_DRIVER_FINISH_SIGNATURE};
  unsigned i, success;
  for (i = 0; i < 3; i++) {
    for (success = 0; success < 2; success++) {
      teeio_spdm_test_context_t test = {0};
      memset(&config, 0, sizeof(config)); config.rule_count = 1;
      strcpy(config.rules[0].scenario, "host_test"); teeio_fault_init(&config);
      assert(teeio_fault_select_driver(drivers[i]));
      dispatch_kind = i == 2 ? DISPATCH_FINISH_SIGNATURE : DISPATCH_KEY_EXCHANGE;
      dispatch_setup = dispatch_run = dispatch_teardown = common_teardown = 0;
      dispatch_setup_result = success != 0;
      /* Deliberately looks like a populated chunk scratch. Mut-auth teardown
       * must not read its bogus installed/callback/session fields. */
      memset(test.test_scratch_buffer, 0xa5, sizeof(test.test_scratch_buffer));
      test.test_scratch_buffer_size = sizeof(teeio_fault_test_buffer_t);
      assert(spdm_test_case_fault_setup(&test) == dispatch_setup_result);
      if (success) spdm_test_case_fault_run(&test);
      spdm_test_case_fault_teardown(&test);
      assert(dispatch_setup == 1 && dispatch_run == success);
      assert(dispatch_teardown == (i != 2) && common_teardown == (i == 2));
      assert(test.test_scratch_buffer[0] == 0xa5);
    }
  }
  teeio_fault_init(&config); assert(!is_key_exchange_scenario());
  assert(!teeio_fault_select_driver(TEEIO_FAULT_DRIVER_NONE));
  assert(!teeio_fault_select_driver(TEEIO_FAULT_DRIVER_MAX + 1));
  assert(!is_key_exchange_scenario());
  dispatch_kind = 0;
}

/* Unexpected dispatch is a test failure: an oversized first chunk must never
 * reach SET_CERTIFICATE or its update callback. */
libspdm_get_spdm_response_func libspdm_get_response_func_via_request_code(uint8_t code)
{
  assert(!"oversized first chunk reached terminal dispatch");
  return NULL;
}

libspdm_return_t libspdm_responder_handle_response_state(libspdm_context_t *spdm,
  uint8_t code, size_t *size, void *response)
{
  assert(!"unexpected responder state");
  return LIBSPDM_STATUS_INVALID_STATE_LOCAL;
}

/* Stand-in for the common observer, using the REAL transport decoder. */
libspdm_return_t teeio_fault_transport_decode_message(void *spdm,
  uint32_t **session, bool *app, bool request, size_t transport_size,
  void *transport, size_t *size, void **message)
{
  libspdm_return_t status = libspdm_transport_pci_doe_decode_message(
    spdm, session, app, request, transport_size, transport, size, message);
  if (!LIBSPDM_STATUS_IS_ERROR(status) && *size >= 4) {
    const uint8_t *bytes = *message;
    size_t offset = bytes[0] >= SPDM_MESSAGE_VERSION_14 ? 8 : 6;
    if (bytes[1] == SPDM_CHUNK_SEND_ACK && *size >= offset + 4 &&
        bytes[offset + 1] == SPDM_ERROR) {
      char actual[32];
      snprintf(actual, sizeof(actual), "spdm_error_0x%02x", bytes[offset + 2]);
      teeio_fault_record_actual(actual);
    }
  }
  return status;
}

libspdm_return_t teeio_fault_transport_encode_message(void *spdm,
  const uint32_t *session, bool app, bool request, size_t size, void *message,
  size_t *transport_size, void **transport)
{
  assert(!"unscoped plaintext injector called");
  return LIBSPDM_STATUS_SEND_FAIL;
}

static libspdm_return_t recovery_call(void *spdm, unsigned step)
{
  teeio_fault_test_buffer_t *state = ((libspdm_context_t *)spdm)->app_context_data_ptr;
  assert(spdm == expected_context);
  assert(!state->armed);
  assert(++recovery_step == step);
  return fail_step == step ? LIBSPDM_STATUS_SEND_FAIL : LIBSPDM_STATUS_SUCCESS;
}

libspdm_return_t host_init_connection(void *spdm, bool version_only)
{
  libspdm_return_t status = recovery_call(spdm, 1);
  unsigned i;
  for (i = 0; i < LIBSPDM_MAX_SESSION_COUNT; i++) {
    ((libspdm_context_t *)spdm)->session_info[i].session_id = INVALID_SESSION_ID;
  }
  return status;
}

libspdm_return_t host_get_certificate(void *spdm, const uint32_t *session,
  uint8_t slot, size_t *size, void *chain)
{
  assert(session == NULL);
  if (sig_control) {
    memset(chain, 0, 1024);
    *size = 1024;
    return LIBSPDM_STATUS_SUCCESS;
  }
  return recovery_call(spdm, 2);
}

libspdm_return_t host_set_certificate(void *spdm, const uint32_t *session,
  uint8_t slot, void *chain, size_t size)
{
  assert(sig_control);
  sig_control = false;
  sig_control_calls++;
  /* Same ambiguous callback rejection as the real unresolved signature baseline. */
  return LIBSPDM_STATUS_ERROR_PEER;
}

libspdm_return_t host_start_session(void *spdm, bool psk, const void *hint,
  uint16_t hint_size, uint8_t hash, uint8_t slot, uint8_t policy,
  uint32_t *session, uint8_t *period, void *measurement)
{
  *session = 0x22223333;
  return recovery_call(spdm, 3);
}

libspdm_return_t host_heartbeat(void *spdm, uint32_t session)
{
  assert(session == 0x22223333);
  return recovery_call(spdm, 4);
}

libspdm_return_t host_stop_session(void *spdm, uint32_t session, uint8_t attributes)
{
  assert(session == 0x22223333);
  return recovery_call(spdm, 5);
}

static teeio_fault_test_buffer_t *fixture(teeio_spdm_test_context_t *test,
  teeio_fault_driver_t driver, uint8_t code, teeio_fault_action_t action)
{
  libspdm_context_t *spdm = calloc(1, sizeof(*spdm));
  assert(spdm != NULL);
  memset(test, 0, sizeof(*test));
  test->spdm_context = spdm;
  spdm->version = LIBSPDM_CONTEXT_STRUCT_VERSION;
  spdm->transport_encode_message = teeio_fault_transport_encode_message;
  spdm->transport_decode_message = teeio_fault_transport_decode_message;
  spdm->app_context_data_ptr = test; /* restoration sentinel */
  memset(&config, 0, sizeof(config));
  config.rule_count = 1;
  config.rules[0].id = 1;
  strcpy(config.rules[0].scenario, "host_test");
  config.rules[0].doe_type = TEEIO_FAULT_DOE_TYPE_PLAIN_SPDM;
  config.rules[0].spdm_code = code;
  config.rules[0].occurrence = 1;
  config.rules[0].action = action;
  config.rules[0].size = 4;
  config.rules[0].pattern_size = 4;
  teeio_fault_init(&config);
  assert(teeio_fault_select_driver(driver));
  assert(chunk_install(test));
  ((teeio_fault_test_buffer_t *)(void *)test->test_scratch_buffer)->version =
    SPDM_MESSAGE_VERSION_14;
  return (void *)test->test_scratch_buffer;
}

static void cleanup(teeio_spdm_test_context_t *test)
{
  libspdm_context_t *spdm = test->spdm_context;
  teeio_fault_test_buffer_t *state = (void *)test->test_scratch_buffer;
  state->session_id = 0;
  spdm_test_case_fault_teardown(test);
  assert(spdm->transport_encode_message == teeio_fault_transport_encode_message);
  assert(spdm->transport_decode_message == teeio_fault_transport_decode_message);
  assert(spdm->app_context_data_ptr == test);
  free(spdm);
}

static void make_send(uint8_t *message, uint8_t version, uint32_t sequence,
  bool last, size_t size)
{
  memset(message, 0, size);
  message[0] = version;
  message[1] = SPDM_CHUNK_SEND;
  message[2] = last ? SPDM_CHUNK_SEND_REQUEST_ATTRIBUTE_LAST_CHUNK : 0;
  message[3] = 7;
  libspdm_write_uint32(message + 4, sequence);
  libspdm_write_uint32(message + 8, (uint32_t)(size - (sequence == 0 ? 16 : 12)));
  if (sequence == 0) {
    libspdm_write_uint32(message + 12, 4096);
    message[16] = version;
    message[17] = SPDM_SET_CERTIFICATE;
  }
}

static void observe(teeio_spdm_test_context_t *test, const uint8_t *message, size_t size)
{
  uint8_t storage[512];
  void *transport = storage;
  void *decoded;
  size_t transport_size = sizeof(storage), decoded_size = sizeof(storage);
  uint32_t *session = NULL;
  bool app = false;
  memcpy(storage + 8, message, size);
  assert(!LIBSPDM_STATUS_IS_ERROR(libspdm_transport_pci_doe_encode_message(
    test->spdm_context, NULL, false, false, size, storage + 8,
    &transport_size, &transport)));
  assert(!LIBSPDM_STATUS_IS_ERROR(chunk_decode(test->spdm_context, &session,
    &app, false, transport_size, transport, &decoded_size, &decoded)));
}

void chunk_test_oversized(uint8_t version)
{
  teeio_spdm_test_context_t test;
  teeio_fault_test_buffer_t *state = fixture(&test,
    TEEIO_FAULT_DRIVER_CHUNK_OVERSIZED, SPDM_CHUNK_SEND, TEEIO_FAULT_ACTION_EXTEND);
  uint8_t storage[640], response[64];
  void *transport = storage, *plain;
  size_t transport_size = sizeof(storage), plain_size = sizeof(storage);
  size_t response_size = sizeof(response);
  uint32_t *session = NULL;
  bool app = false;
  libspdm_context_t *responder = calloc(1, sizeof(*responder));
  state->version = version;
  assert(responder != NULL);
  responder->version = LIBSPDM_CONTEXT_STRUCT_VERSION;
  responder->connection_info.version = (spdm_version_number_t)version << SPDM_VERSION_NUMBER_SHIFT_BIT;
  responder->connection_info.connection_state = LIBSPDM_CONNECTION_STATE_NEGOTIATED;
  responder->local_context.capability.flags = SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHUNK_CAP;
  responder->connection_info.capability.flags = SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CHUNK_CAP;
  responder->local_context.capability.data_transfer_size = 256;
  responder->local_context.capability.max_spdm_msg_size = 8192;
  responder->scratch_buffer = calloc(1, 65536);
  responder->scratch_buffer_size = 65536;

  make_send(storage + 8, version, 0, false, 256);
  /* Positive control against real responder: same first chunk is valid. */
  assert(!LIBSPDM_STATUS_IS_ERROR(libspdm_get_response_chunk_send(responder,
    256, storage + 8, &response_size, response)));
  assert(response[1] == SPDM_CHUNK_SEND_ACK && response[2] == 0);
  assert(responder->chunk_context.send.chunk_in_use);
  memset(&responder->chunk_context, 0, sizeof(responder->chunk_context));

  /* Setup/control traffic never consumes FI occurrence. */
  assert(!LIBSPDM_STATUS_IS_ERROR(chunk_encode(test.spdm_context, NULL, false,
    true, 256, storage + 8, &transport_size, &transport)));
  assert(teeio_fault_fire_count() == 0 && config.rules[0].match_count == 0);
  state->armed = true;
  state->data_transfer_size = 256;
  transport_size = sizeof(storage);
  assert(!LIBSPDM_STATUS_IS_ERROR(chunk_encode(test.spdm_context, NULL, false,
    true, 256, storage + 8, &transport_size, &transport)));
  assert(transport_size == 268);
  assert(libspdm_read_uint32((uint8_t *)transport + 4) == 67);
  assert(!LIBSPDM_STATUS_IS_ERROR(libspdm_transport_pci_doe_decode_message(
    test.spdm_context, &session, &app, true, transport_size, transport, &plain_size, &plain)));
  assert(plain_size == 260); /* appended bytes are actually visible to SPDM */
  response_size = sizeof(response);
  assert(!LIBSPDM_STATUS_IS_ERROR(libspdm_get_response_chunk_send(responder,
    plain_size, plain, &response_size, response)));
  observe(&test, response, response_size);
  assert(state->first_early_invalid_request && !state->invalid);
  assert(teeio_fault_fire_count() == 1 && !responder->chunk_context.send.chunk_in_use);
  assert(strstr(teeio_fault_result_record(), "spdm_error_0x01"));

  /* Regression: terminal 0x05, late 0x01, missing EARLY_ERROR, wrong sequence,
   * and truncated embedded errors must not satisfy the first-ACK predicate. */
  state->first_early_invalid_request = false;
  state->ack_count = 0;
  response[2] = 0;
  response[(version >= SPDM_MESSAGE_VERSION_14 ? 8 : 6) + 2] = 5;
  observe(&test, response, response_size);
  assert(!state->first_early_invalid_request);
  response[2] = 1;
  response[(version >= SPDM_MESSAGE_VERSION_14 ? 8 : 6) + 2] = 1;
  observe(&test, response, response_size);
  assert(!state->first_early_invalid_request);
  state->ack_count = 0;
  observe(&test, response, version >= SPDM_MESSAGE_VERSION_14 ? 8 : 6);
  assert(!state->first_early_invalid_request);
  state->ack_count = 0;
  response[4] = 1;
  observe(&test, response, response_size);
  assert(!state->first_early_invalid_request && state->invalid);
  free(responder->scratch_buffer);
  free(responder);
  cleanup(&test);
}

void chunk_test_missing(unsigned chunks, uint8_t version)
{
  teeio_spdm_test_context_t test;
  teeio_fault_test_buffer_t *state = fixture(&test,
    TEEIO_FAULT_DRIVER_CHUNK_LAST_WITHHELD, SPDM_CHUNK_SEND, TEEIO_FAULT_ACTION_DROP);
  unsigned i;
  uint8_t storage[512], ack[8] = {0};
  state->version = version;
  state->armed = true;
  for (i = 0; i < chunks; i++) {
    void *transport = storage;
    size_t transport_size = sizeof(storage);
    libspdm_return_t status;
    make_send(storage + 8, version, i, i + 1 == chunks, 256);
    status = chunk_encode(test.spdm_context, NULL, false, true, 256,
                           storage + 8, &transport_size, &transport);
    if (i + 1 == chunks) {
      assert(status == LIBSPDM_STATUS_SEND_FAIL);
      assert(state->selected && teeio_fault_fire_count() == 1);
    } else {
      assert(!LIBSPDM_STATUS_IS_ERROR(status));
      assert(config.rules[0].match_count == 0);
      ack[0] = version;
      ack[1] = SPDM_CHUNK_SEND_ACK;
      ack[3] = 7;
      libspdm_write_uint32(ack + 4, i);
      observe(&test, ack, version >= SPDM_MESSAGE_VERSION_14 ? 8 : 6);
    }
  }
  assert(!state->invalid && state->ack_count == chunks - 1);
  assert(strstr(teeio_fault_result_record(), "final_chunk_withheld"));
  cleanup(&test);
}

void chunk_test_abandon_and_recovery(unsigned failure)
{
  teeio_spdm_test_context_t test;
  teeio_fault_test_buffer_t *state = fixture(&test,
    TEEIO_FAULT_DRIVER_CHUNK_ABANDON, SPDM_CHUNK_GET, TEEIO_FAULT_ACTION_ABANDON);
  uint8_t storage[512] = {0}, response[32] = {0};
  void *transport = storage;
  size_t transport_size = sizeof(storage);
  char result[768];
  state->armed = true;
  storage[8] = SPDM_MESSAGE_VERSION_14;
  storage[9] = SPDM_CHUNK_GET;
  assert(!LIBSPDM_STATUS_IS_ERROR(chunk_encode(test.spdm_context, NULL, false,
    true, 8, storage + 8, &transport_size, &transport)));
  assert(teeio_fault_fire_count() == 0);
  response[0] = SPDM_MESSAGE_VERSION_14;
  response[1] = SPDM_CHUNK_RESPONSE;
  libspdm_write_uint32(response + 8, 16);
  libspdm_write_uint32(response + 12, 2048);
  response[16] = SPDM_MESSAGE_VERSION_14;
  response[17] = SPDM_DIGESTS;
  observe(&test, response, sizeof(response));
  assert(!state->partial_response);
  response[17] = SPDM_CERTIFICATE;
  observe(&test, response, sizeof(response));
  assert(state->partial_response);
  libspdm_write_uint32(storage + 12, state->next_get_sequence);
  transport_size = sizeof(storage);
  assert(chunk_encode(test.spdm_context, NULL, false, true, 8, storage + 8,
    &transport_size, &transport) == LIBSPDM_STATUS_SEND_FAIL);
  assert(teeio_fault_fire_count() == 1 && !state->invalid);
  snprintf(result, sizeof(result), "%s", teeio_fault_result_record());
  state->session_id = 0x11112222;
  ((libspdm_context_t *)test.spdm_context)->session_info[0].session_id = state->session_id;
  expected_context = test.spdm_context;
  recovery_step = 0;
  fail_step = failure;
  assertions_failed = 0;
  assert(chunk_recover(&test) == (failure == 0));
  assert(recovery_step == (failure == 0 ? 5 : failure));
  assert(assertions_failed == (failure == 0 ? 0 : 1));
  assert(strcmp(result, teeio_fault_result_record()) == 0);
  cleanup(&test);
}

void chunk_test_der_bounds(void)
{
  uint8_t inputs[][12] = {{0x30, 0x80}, {0x30, 0x81, 0x01, 0},
    {0x30, 0x82, 0, 0x80}, {0x30, 0x89}, {0x30, 0x88, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, {0x31, 0}};
  unsigned i;
  size_t n, length;
  const uint8_t *p, *value;
  for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
    for (n = 0; n <= sizeof(inputs[0]); n++) {
      p = inputs[i];
      assert(!certificate_der(&p, p + n, 0x30, &value, &length));
    }
  }
}

static void sig_config(void)
{
  config.rules[0].offset = 12;
  config.rules[0].pattern_size = 1;
  config.rules[0].pattern[0] = 1;
}

/* SPDM cert chain (header, root hash, DER bundle) from the libspdm samples. */
static void load_sample_chain(void)
{
  const uint32_t hash = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384;
  size_t header = sizeof(spdm_cert_chain_t) + libspdm_get_hash_size(hash);
  const uint8_t *root;
  size_t root_size, der_size;
  FILE *f = fopen(HOST_SAMPLE_KEY_DIR "/ecp384/bundle_responder.certchain.der", "rb");
  assert(f != NULL);
  der_size = fread(sig_chain + header, 1, sizeof(sig_chain) - header, f);
  assert(der_size > 0 && feof(f) && fclose(f) == 0);
  sig_chain_size = header + der_size;
  libspdm_write_uint32(sig_chain, (uint32_t)sig_chain_size);
  assert(libspdm_x509_get_cert_from_cert_chain(sig_chain + header, der_size, 0,
    &root, &root_size));
  assert(libspdm_hash_all(hash, root, root_size, sig_chain + sizeof(spdm_cert_chain_t)));
}

void chunk_test_signature_fail_closed(void)
{
  teeio_fault_test_buffer_t parsed = {0};
  const uint32_t hash = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384;
  load_sample_chain();
  assert(certificate_signature_target(&parsed, sig_chain, sig_chain_size, hash));
  size_t target = parsed.certificate_target;
  for (size_t n = 0; n <= target - sizeof(spdm_set_certificate_request_t); n++)
    assert(!certificate_signature_target(&parsed, sig_chain, n, hash));
  for (unsigned failure = 0; failure < 7; failure++) {
    teeio_spdm_test_context_t test;
    teeio_fault_test_buffer_t *state = fixture(&test,
      TEEIO_FAULT_DRIVER_CERT_SIGNATURE_CHUNK, SPDM_CHUNK_SEND, TEEIO_FAULT_ACTION_XOR);
    uint8_t storage[4096] = {0}, saved[4096];
    sig_config();
    assert(certificate_signature_target(state, sig_chain, sig_chain_size, hash));
    /* Put the computed target in the first chunk; still leave data pending. */
    size_t size = 16 + state->certificate_target + 1;
    assert(size < sizeof(storage) - 8 && size - 16 < sig_chain_size + 4);
    make_send(storage + 8, state->version, 0, false, size);
    libspdm_write_uint32(storage + 20, (uint32_t)(sig_chain_size + 4));
    memcpy(storage + 28, sig_chain, size - 20);
    state->armed = true;
    switch (failure) {
      case 0: config.rules[0].pattern[0] = 3; break;
      case 1: config.rules[0].offset = 24; break;
      case 2: config.rules[0].occurrence = 2; break;
      case 3: state->certificate_target_valid = false; break;
      case 4: storage[8 + 16 + state->certificate_target] ^= 2; break;
      case 5: storage[12] = 1; break;
      case 6: libspdm_write_uint32(storage + 16, (uint32_t)size); break;
    }
    memcpy(saved, storage, sizeof(storage));
    void *transport = storage;
    size_t transport_size = sizeof(storage);
    assert(chunk_encode(test.spdm_context, NULL, false, true, size, storage + 8,
      &transport_size, &transport) == LIBSPDM_STATUS_SEND_FAIL);
    assert(state->invalid && !state->selected && memcmp(saved, storage, sizeof(storage)) == 0);
    cleanup(&test);
  }
}

void chunk_test_rejecting_positive_control(void)
{
  teeio_spdm_test_context_t test;
  teeio_fault_test_buffer_t *state = fixture(&test,
    TEEIO_FAULT_DRIVER_CERT_SIGNATURE_CHUNK, SPDM_CHUNK_SEND, TEEIO_FAULT_ACTION_XOR);
  libspdm_context_t *spdm = test.spdm_context;
  spdm->connection_info.algorithm.base_hash_algo = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384;
  spdm->connection_info.capability.flags = SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_SET_CERT_CAP;
  spdm->connection_info.capability.data_transfer_size = 256;
  state->session_id = 0x11112222;
  sig_control = true;
  sig_control_calls = 0;
  recovery_step = fail_step = assertions_failed = 0;
  expected_context = spdm;
  spdm_test_case_fault_run(&test);
  assert(!sig_control && sig_control_calls == 1);
  assert(teeio_fault_fire_count() == 0 && !state->selected);
  assert(assertions_failed == 1 && recovery_step == 5);
  cleanup(&test);
}
