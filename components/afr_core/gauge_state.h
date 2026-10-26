/* Gauge state machine.
 *
 * The LED alone moves the gauge into and out of READY; the output voltage
 * alone decides the two rail states. A number is shown only in ST_READY. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GS_SEQ_TIMEOUT_MS     15000u /* SEQUENCER -> WARMING regardless */
#define GS_POWER_ON_SUPPLY_V  9.0f   /* NO_SPARTAN -> SEQUENCER */
#define GS_POWER_ON_OUTPUT_V  1.0f
#define GS_POWER_OFF_SUPPLY_V 8.0f   /* any -> NO_SPARTAN */
#define GS_POWER_OFF_OUTPUT_V 0.05f
#define GS_POWER_OFF_MS       10000u
#define GS_FAULT_FAILS        3u     /* consecutive I2C failures */
#define GS_FAULT_CLEAR_MS     1000u  /* of good reads */

typedef struct {
  uint32_t t_ms;
  bool     adc_ok;      /* this sample's lambda read succeeded */
  float    v_out;       /* calibrated, spike-filtered */
  led_pattern_t led;
  float    supply_v;    /* NAN if not fitted */
  bool     cal_complete; /* both sequencer plateaus captured */
} gs_input_t;

typedef struct {
  gauge_state_t state;
  gauge_state_t pre_fault;
  fault_reason_t fault;
  uint32_t since_ms;
  uint8_t  fails;
  bool     good_run;
  uint32_t good_since_ms;
  bool     low;
  uint32_t low_since_ms;
} gauge_sm_t;

void gs_init(gauge_sm_t *sm, uint32_t t_ms);
/* BOOT -> NO_SPARTAN once initialisation is complete. */
void gs_init_complete(gauge_sm_t *sm, uint32_t t_ms);
/* Enter ST_FAULT for a reason that never clears (ADC not found). */
void gs_force_fault(gauge_sm_t *sm, fault_reason_t reason, uint32_t t_ms);
gauge_state_t gs_update(gauge_sm_t *sm, const gs_input_t *in);

/* "READY", "LEAN_RAIL", ... as written in the log. */
const char *gs_state_name(gauge_state_t s);
/* Parse a name; ST_COUNT if not recognised. */
gauge_state_t gs_state_parse(const char *s);
/* True for READY and the two rails, where lambda is defined. */
bool gs_has_lambda(gauge_state_t s);
const char *gs_fault_text(fault_reason_t f);

#ifdef __cplusplus
}
#endif
