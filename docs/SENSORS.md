# Sensors

The library includes a self-contained DHT11 / DHT21 / DHT22 driver. There is no
dependency on an external DHT library, so the code behaves identically on every
ESP32 core version.

---

## Wiring

```
        DHT22 / AM2302                    ESP32
        +-------------+
        |  1  VCC     |------------------ 3V3
        |  2  DATA    |------+----------- GPIO 4  (any usable GPIO)
        |  3  NC      |      |
        |  4  GND     |---+  |
        +-------------+   |  |
                          |  +--[ 4.7k - 10k ]--- 3V3
                          |
                         GND
```

- Most breakout boards already include the pull-up resistor. Bare three-pin
  sensors usually do too; bare four-pin sensors usually do not.
- Use a **3V3** supply. Powering a DHT from 5V and feeding its data line into
  an ESP32 GPIO will damage the pin over time.
- Keep the cable under about 20 cm. Longer runs need a lower pull-up value
  and a 100 nF capacitor across the sensor's supply pins.
- Avoid strapping pins (GPIO 0, 2, 5, 12, 15) and the input-only pins
  (GPIO 34–39, which cannot drive the line low).

---

## Setup

```cpp
bot.beginDHT(4, DHT_TYPE_22);   // DHT22 / AM2302
bot.beginDHT(4, DHT_TYPE_11);   // DHT11
bot.beginDHT(4, DHT_TYPE_21);   // DHT21 / AM2301
```

Then call `bot.tick()` from `loop()`. It reads the sensor at the correct
interval automatically — you do not need to schedule anything.

| Sensor | Minimum interval | Range | Resolution |
|---|---|---|---|
| DHT11 | 1000 ms | 0–50 °C, 20–90 % RH | 1 °C, 1 % |
| DHT21 | 2000 ms | −40–80 °C, 0–100 % RH | 0.1 °C, 0.1 % |
| DHT22 | 2000 ms | −40–80 °C, 0–100 % RH | 0.1 °C, 0.1 % |

Reading faster than the datasheet allows returns `DHT_TOO_SOON` and the last
good value, which is labelled as cached. The sensor is never actually polled
faster than it can respond.

---

## Asking about the room

```
> what is the temperature
It is 24.3 C in the room, which feels like 25.1 C at 58% humidity.
Comfortable working conditions. Nothing to worry about.

> what is the humidity
Humidity is 58.2% at 24.3 C, dew point 15.6 C.

> analyse the situation
Warm and humid.
Temperature 29.4 C, humidity 74%, feels like 32.8 C, dew point 24.3 C.
Comfort: warm. Temperature is rising, humidity is steady.
High humidity with warm air. Watch for condensation on cold surfaces.
It is 74% humidity. My circuits are basically doing yoga in a steam room.
```

The bot distinguishes a *definition* question from a *measurement* question.
"What is temperature" gets the physics answer from the knowledge base; "what is
the temperature in here" reads the sensor. Questions mentioning the CPU, chip,
core or board are routed to die-temperature reporting instead.

---

## When the sensor fails

The library never fabricates a reading.

```
> what is the temperature
The DHT22 did not answer (checksum failed). I would rather say nothing than
make a reading up. Check the data pin, the 3V3 supply and the pull-up resistor.
```

| Status | Meaning |
|---|---|
| `DHT_OK` | fresh, valid, checksum verified |
| `DHT_NOT_CONFIGURED` | `beginDHT()` was never called |
| `DHT_TOO_SOON` | rate limited; the previous value is still valid |
| `DHT_NO_RESPONSE` | the sensor never pulled the line low — check wiring and power |
| `DHT_TIMEOUT` | a data pulse never arrived — usually noise or a long cable |
| `DHT_CHECKSUM` | 40 bits arrived but the checksum failed — electrical noise |
| `DHT_IMPLAUSIBLE` | decoded values fall outside the sensor's rated range |

A failed read never overwrites the last good value. The reading is downgraded
from `MEAS_LIVE` to `MEAS_CACHED`, and then to `MEAS_STALE` once it is too old
to trust. The age is always reported alongside a cached value.

Reads are retried automatically (three attempts by default). Occasional
failures are normal for DHT parts and are not treated as a fault until they
become consecutive.

---

## Situation analysis

`bot.getSituationReport()` and `sensors().analyze()` return derived values
computed from the live reading.

| Value | Method |
|---|---|
| Dew point | Magnus formula, accurate to ±0.4 °C from −40 to 50 °C |
| Heat index | Rothfusz regression with the low-humidity adjustment |
| Absolute humidity | g/m³, from saturation vapour pressure |
| Comfort band | cold / cool / ideal / warm / hot / dangerous |
| Trend | direction over the last 8 samples |

Risk flags:

| Flag | Condition | Why it matters |
|---|---|---|
| `condensationRisk` | ambient within 2 °C of dew point | water forms on cold surfaces and on the board |
| `moldRisk` | sustained humidity above 70 % | mould grows on walls and in enclosures |
| `dryAirRisk` | sustained humidity below 25 % | static discharge damages ICs |
| `electronicsRisk` | conditions the board itself will not enjoy | combined heat and humidity |

```cpp
SituationReport s = bot.sensors().analyze();
if (s.valid) {
    Serial.println(s.headline);
    Serial.println(s.advice);
    if (s.condensationRisk) Serial.println("Condensation likely!");
}
```

The physics helpers are usable on their own:

```cpp
float dp = SensorHub::dewPoint(24.3f, 58.0f);
float hi = SensorHub::heatIndex(29.4f, 74.0f);
float ah = SensorHub::absoluteHumidity(24.3f, 58.0f);
ComfortLevel c = SensorHub::classifyComfort(24.3f, 58.0f);
```

---

## The humour

Trolling is on by default and can be switched off:

```cpp
bot.enableTrolling(false);
```

There are eight comment pools, one per condition band, three lines each, and
they rotate deterministically so you do not hear the same joke twice in a row.

Two rules are enforced in code:

1. Humour is **appended to a real measurement**, never substituted for one.
2. If the sensor did not answer, there is no joke — just the error and what to
   check.

A comment on a reading that does not exist would be a lie dressed as a joke,
which is worse than no joke.

---

## Using the sensor directly

```cpp
DhtReadStatus rc = bot.sensors().read();
if (rc == DHT_OK) {
    const DhtReading& r = bot.sensors().reading();
    Serial.println(r.temperatureC);
    Serial.println(r.humidityPercent);
    Serial.println(bot.sensors().ageMs());
}
```

`DhtReading` also carries `successCount`, `failureCount` and
`consecutiveFailures`, which is what the Sensors component of the health score
is built from.

---

## Interrupts

Decoding the single-wire protocol is timing critical and takes about 4 ms. By
default the driver takes a `portMUX` critical section for that window so a
Wi-Fi or timer interrupt cannot corrupt the bit stream.

If your application cannot tolerate a 4 ms interrupt block:

```cpp
bot.sensors().setInterruptSafe(false);
```

You will see more `DHT_CHECKSUM` and `DHT_TIMEOUT` results when Wi-Fi is
active. The retry logic absorbs most of them, and the health score will tell
you if the error rate becomes significant.

---

## Ambient as a thermal fallback

On chips with no usable die temperature sensor — including the classic ESP32,
whose `temperatureRead()` returns a constant 53.33 °C placeholder — the thermal
guard falls back to the DHT reading plus a 15 °C offset.

That estimate is always labelled as an estimate. In diagnostics it appears as
`(ambient estimate)` and its component confidence drops from 90 to 55, so it
informs the health score without dominating it.

---

See also: [TELEMETRY.md](TELEMETRY.md), [STATUS.md](STATUS.md).
