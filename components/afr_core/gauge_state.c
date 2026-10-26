#include "gauge_state.h"

#include <math.h>
#include <string.h>

#include "conversion.h"

static const char *const k_names[ST_COUNT] = {
  "BOOT", "NO_SPARTAN", "SEQUENCER", "WAIT_EXHAUST", "WARMING",
  "READY", "RICH_RAIL", "LEAN_RAIL", "TOO_HOT", "FAULT"
};

static void enter(gauge_sm_t *sm, gauge_state_t s, uint32_t t_ms)
{
  if (sm->state != s) {
    sm->state = s;
    sm->since_ms = t_ms;
  }
}

void gs_init(gauge_sm_t *sm, uint32_t t_ms)
{
  memset(sm, 0, sizeof(*sm));
  sm->state = ST_BOOT;
  sm->since_ms = t_ms;
}

void gs_init_complete(gauge_sm_t *sm, uint32_t t_ms)
{
  if (sm->state == ST_BOOT) enter(sm, ST_NO_SPARTAN, t_ms);
}

void gs_force_fault(gauge_sm_t *sm, fault_reason_t reason, uint32_t t_ms)
{
  if (sm->state != ST_FAULT) sm->pre_fault = sm->state;
  sm->fault = reason;
  enter(sm, ST_FAULT, t_ms);
}

/* READY or a rail, decided by the output voltage alone. */
static gauge_state_t ready_substate(float v_out)
{
  if (v_out < RAIL_RICH_V) return ST_RICH_RAIL;
  if (v_out > RAIL_LEAN_V) return ST_LEAN_RAIL;
  return ST_READY;
}

static bool supply_fitted(const gs_input_t *in)
{
  return !isnan(in->supply_v);
}

gauge_state_t gs_update(gauge_sm_t *sm, const gs_input_t *in)
{
  const uint32_t t = in->t_ms;

  if (sm->state == ST_BOOT) return sm->state;
  if (sm->state == ST_FAULT && sm->fault != FAULT_I2C) return sm->state;

  if (!in->adc_ok) {
    sm->good_run = false;
    if (sm->fails < 255) sm->fails++;
    if (sm->fails >= GS_FAULT_FAILS && sm->state != ST_FAULT) {
      gs_force_fault(sm, FAULT_I2C, t);
    }
    return sm->state;
  }
  sm->fails = 0;

  if (sm->state == ST_FAULT) {
    if (!sm->good_run) {
      sm->good_run = true;
      sm->good_since_ms = t;
    }
    if (t - sm->good_since_ms < GS_FAULT_CLEAR_MS) return sm->state;
    sm->fault = FAULT_NONE;
    enter(sm, sm->pre_fault, t);
  }

  /* Loss of Spartan power returns to NO_SPARTAN from any state. */
  if (in->v_out < GS_POWER_OFF_OUTPUT_V) {
    if (!sm->low) {
      sm->low = true;
      sm->low_since_ms = t;
    }
  } else {
    sm->low = false;
  }
  bool lost = (supply_fitted(in) && in->supply_v < GS_POWER_OFF_SUPPLY_V) ||
              (in->led == LED_OFF && sm->low && t - sm->low_since_ms >= GS_POWER_OFF_MS);
  if (lost) {
    enter(sm, ST_NO_SPARTAN, t);
    return sm->state;
  }

  switch (sm->state) {
  case ST_NO_SPARTAN:
    if ((supply_fitted(in) && in->supply_v > GS_POWER_ON_SUPPLY_V) ||
        in->v_out > GS_POWER_ON_OUTPUT_V) {
      enter(sm, ST_SEQUENCER, t);
    }
    break;

  case ST_SEQUENCER:
    if (in->cal_complete || t - sm->since_ms >= GS_SEQ_TIMEOUT_MS) {
      enter(sm, ST_WARMING, t);
    }
    break;

  case ST_WARMING:
    if (in->led == LED_VERY_SLOW) enter(sm, ST_WAIT_EXHAUST, t);
    else if (in->led == LED_STEADY) enter(sm, ready_substate(in->v_out), t);
    else if (in->led == LED_FAST) enter(sm, ST_TOO_HOT, t);
    break;

  case ST_WAIT_EXHAUST:
    /* STEADY also leaves, via WARMING, in case a SLOW phase was missed. */
    if (in->led == LED_SLOW || in->led == LED_STEADY) enter(sm, ST_WARMING, t);
    break;

  case ST_READY:
  case ST_RICH_RAIL:
  case ST_LEAN_RAIL:
    if (in->led == LED_SLOW) enter(sm, ST_WARMING, t);
    else if (in->led == LED_FAST) enter(sm, ST_TOO_HOT, t);
    else enter(sm, ready_substate(in->v_out), t);
    break;

  case ST_TOO_HOT:
    if (in->led == LED_STEADY) enter(sm, ready_substate(in->v_out), t);
    else if (in->led == LED_SLOW) enter(sm, ST_WARMING, t);
    break;

  case ST_BOOT:
  case ST_FAULT:
  case ST_COUNT:
    break;
  }
  return sm->state;
}

const char *gs_state_name(gauge_state_t s)
{
  return (unsigned)s < ST_COUNT ? k_names[s] : "?";
}

gauge_state_t gs_state_parse(const char *s)
{
  for (int i = 0; i < ST_COUNT; i++) {
    if (strcmp(s, k_names[i]) == 0) return (gauge_state_t)i;
  }
  return ST_COUNT;
}

bool gs_has_lambda(gauge_state_t s)
{
  return s == ST_READY || s == ST_RICH_RAIL || s == ST_LEAN_RAIL;
}

const char *gs_fault_text(fault_reason_t f)
{
  switch (f) {
  case FAULT_I2C: return "ADC read failed";
  case FAULT_ADC_NOT_FOUND: return "Lambda ADC not found";
  case FAULT_AUX_NOT_FOUND: return "LED/TPS ADC not found";
  default: return "";
  }
}
