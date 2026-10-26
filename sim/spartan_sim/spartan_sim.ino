/* Simulated Spartan 3 Lite v2 for bench-testing the gauge (spec: Testing,
 * build step 4).
 *
 * Board: any 5 V Arduino (Uno, Nano). 5 V logic matches the real Spartan,
 * so the gauge's dividers see the same levels as in the car.
 *
 *   MCP4725 DAC (VDD 5 V, SDA A4, SCL A5) VOUT -> gauge green input
 *   D7 -> 1 k -> gauge blue input        (heater-status LED line)
 *   D9 -> 10 k -> 10 uF to GND -> gauge TPS input   (throttle, PWM filtered)
 *   TX (D1) -> gauge orange input         (Spartan TX, 5 V, 9600 8N1)
 *   RX (D0) <- gauge yellow (GPIO16)      (Spartan RX)
 *   GND -> gauge ground (the Spartan's black wire) and the ground-sense input
 *
 * Unplug the gauge's UART wires while uploading: D0/D1 are the USB serial.
 *
 * At power-up (reset) it plays the start-up sequencer, 1.666 V for 5 s then
 * 3.333 V for 5 s, then a warm-up with the LED blinking slow, then loops a
 * drive with two passes, a lean-out at full throttle and the lift-off fuel
 * cuts, plus the throttle signal. The LED steps through every pattern. It
 * answers the GET commands the gauge sends at boot and the SET commands
 * from its settings and service screens.
 */
#include <Wire.h>

#define DAC_ADDR 0x60   /* MCP4725 with A0 low; 0x62 on some boards */
#define LED_PIN  7
#define TPS_PIN  9      /* PWM; the RC filter makes it a voltage */
#define TPS_CLOSED_V 0.6f
#define TPS_WOT_V    4.4f

/* Answers to the boot queries. */
static const char *FW = "1.03";
static const char *HW = "2";
static float lam0 = 0.680f, lam5 = 1.360f;
static int perf = 0, slowheat = 0;

static bool forced = false;     /* SETLINOUT holds the output until reset */
static float forced_v = 0;

/* LED patterns: period and on time in ms; 0/1 steady on, 0/0 off. */
struct pattern { unsigned period, on; };
static const pattern P_STEADY = { 0, 1 }, P_SLOW = { 2000, 250 }, P_FAST = { 500, 250 },
                     P_VSLOW = { 8000, 250 }, P_OFF = { 0, 0 };

/* A drive: duration, lambda (0 = hold volts), volts, LED pattern, throttle %. */
struct seg { unsigned long ms; float lambda; float volts; pattern led; float tps; };
static const seg drive[] = {
  { 20000, 0,    1.20f, P_SLOW,   0 },   /* warm-up, output undocumented */
  { 20000, 1.00f, 0,    P_STEADY, 0 },   /* warm idle, closed loop */
  { 1500,  0.70f, 0,    P_STEADY, 12 },  /* staging, rich: rich alarm */
  { 9000,  0.82f, 0,    P_STEADY, 100 }, /* clean pass */
  { 6000,  0,    5.00f, P_STEADY, 0 },   /* lift: fuel cut, no alarm */
  { 20000, 1.00f, 0,    P_STEADY, 0 },
  { 5000,  0.82f, 0,    P_STEADY, 100 }, /* second pass... */
  { 400,   0.96f, 0,    P_STEADY, 100 }, /* ...leans out: LEAN AT LOAD */
  { 2000,  0.83f, 0,    P_STEADY, 100 },
  { 5000,  0,    5.00f, P_STEADY, 0 },
  { 3000,  0,    0.00f, P_STEADY, 5 },   /* rich rail: rich alarm */
  { 5000,  1.00f, 0,    P_FAST,   0 },   /* sensor too hot */
  { 5000,  1.00f, 0,    P_STEADY, 20 },
  { 3000,  1.15f, 0,    P_STEADY, 20 },  /* lean at part throttle: LEAN */
  { 12000, 1.00f, 0,    P_VSLOW,  0 },   /* waiting for exhaust heat */
  { 5000,  1.00f, 0,    P_SLOW,   0 },   /* heating */
};
#define N_SEGS (sizeof drive / sizeof drive[0])

static void dac_write(float v)
{
  if (v < 0) v = 0;
  if (v > 5) v = 5;
  unsigned code = (unsigned)(v / 5.0f * 4095.0f + 0.5f);
  Wire.beginTransmission(DAC_ADDR);
  Wire.write((uint8_t)((code >> 8) & 0x0F)); /* fast mode, normal power */
  Wire.write((uint8_t)(code & 0xFF));
  Wire.endTransmission();
}

static float volts_for(float lambda)
{
  return (lambda - lam0) / (lam5 - lam0) * 5.0f;
}

static void led(const pattern &p, unsigned long t)
{
  bool on = p.on && (p.period == 0 || (t % p.period) < p.on);
  digitalWrite(LED_PIN, on ? HIGH : LOW);
}

static void reply(const char *s)
{
  Serial.print(s);
  Serial.print("\r\n");
}

static void reply_f(float v)
{
  Serial.print(v, 3);
  Serial.print("\r\n");
}

static void command(char *c)
{
  for (char *p = c; *p; p++) *p = toupper(*p);
  if (!strcmp(c, "GETFW")) reply(FW);
  else if (!strcmp(c, "GETHW")) reply(HW);
  else if (!strcmp(c, "GETTYPE")) reply("0");
  else if (!strcmp(c, "GETLAMZEROV")) reply_f(lam0);
  else if (!strcmp(c, "GETLAMFIVEV")) reply_f(lam5);
  else if (!strcmp(c, "GETPERF")) { Serial.print(perf); Serial.print("\r\n"); }
  else if (!strcmp(c, "GETSLOWHEAT")) { Serial.print(slowheat); Serial.print("\r\n"); }
  else if (!strncmp(c, "SETPERF", 7)) { perf = atoi(c + 7); reply("OK"); }
  else if (!strncmp(c, "SETSLOWHEAT", 11)) { slowheat = atoi(c + 11); reply("OK"); }
  else if (!strncmp(c, "SETLINOUT", 9)) { forced = true; forced_v = atof(c + 9); reply("OK"); }
  else if (c[0]) reply("?");
}

static void poll_uart()
{
  static char buf[32];
  static uint8_t n;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r' || ch == '\n') {
      buf[n] = '\0';
      command(buf);
      n = 0;
    } else if (n + 1 < sizeof buf) {
      buf[n++] = ch;
    }
  }
}

static void tps(float pct)
{
  float v = TPS_CLOSED_V + (TPS_WOT_V - TPS_CLOSED_V) * pct / 100.0f;
  analogWrite(TPS_PIN, (int)(v / 5.0f * 255.0f + 0.5f));
}

static void hold(unsigned long ms, float v, const pattern &p)
{
  unsigned long start = millis();
  while (millis() - start < ms) {
    unsigned long t = millis() - start;
    dac_write(forced ? forced_v : v);
    led(p, t);
    poll_uart();
    delay(2);
  }
}

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  pinMode(TPS_PIN, OUTPUT);
  tps(0);
  Serial.begin(9600);
  Wire.begin();
  Wire.setClock(400000);

  /* Start-up output sequencer: the gauge's 2-point calibration. */
  hold(5000, 1.666f, P_SLOW);
  hold(5000, 3.333f, P_SLOW);
}

void loop()
{
  for (unsigned i = 0; i < N_SEGS; i++) {
    const seg &s = drive[i];
    float v = s.lambda > 0 ? volts_for(s.lambda) : s.volts;
    tps(s.tps);
    hold(s.ms, v, s.led);
  }
}
