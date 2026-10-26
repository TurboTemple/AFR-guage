/* Board power: AXP2101 VBUS watch and the shutdown path.
 *
 * Without a LiPo the board dies the moment the ignition goes off; the
 * logger's 1 s sync bounds the loss. With a LiPo, VBUS loss flushes the log
 * at once, and if VBUS stays away for POWER_OFF_CONFIRM_MS (longer than a
 * cranking dip) the gauge closes the log and powers off through the AXP2101,
 * all within 2 s. */
#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_POLL_MS          50
#define POWER_OFF_CONFIRM_MS   1500
#define POWER_CHARGE_MA        200

typedef struct {
  bool vbus;         /* USB-C supply present (ignition on) */
  bool battery;      /* LiPo connected */
  bool pwr_pressed;  /* PWR button (read only; no action in v1) */
} power_status_t;

typedef struct {
  /* Reads a TCA9554 pin level (P4 PWR button, P5 AXP IRQ); may be NULL. */
  bool (*read_expander_pin)(int pin);
  /* VBUS just went away: flush the log now. */
  void (*on_vbus_lost)(void);
  /* VBUS has stayed away: close the log. Power-off follows. */
  void (*on_shutdown)(void);
} power_hooks_t;

/* bus: the onboard I2C bus (from the BSP). */
esp_err_t power_init(i2c_master_bus_handle_t bus);
esp_err_t power_task_start(const power_hooks_t *hooks, int core, int prio);
power_status_t power_status(void);
/* Turn every rail off through the AXP2101 (battery operation only). */
void power_off(void);

#ifdef __cplusplus
}
#endif
