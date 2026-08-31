# API reference

Everything a sketch needs is reachable through one header:

```cpp
#include <AmelTechBot.h>
```

`AmelTechBot` owns every subsystem. The subsystems are also exposed directly
for advanced use, but you rarely need them.

---

## Lifecycle

### `bool begin()`

Starts the knowledge base, telemetry, thermal guard, diagnostics, the profile
store and the training console, then loads taught entries and remembered names
from NVS.

Returns `false` only if the knowledge base could not start, which in practice
means the heap was exhausted before `setup()` ran.

```cpp
if (!bot.begin()) {
    Serial.println("Not enough memory to start.");
}
```

### `void end()`

Flushes anything dirty to flash, releases the DHT pin and stops the bot.
Called automatically by the destructor.

### `void tick()`

Call this from `loop()`. It refreshes telemetry, updates the thermal state,
polls the sensor and flushes deferred saves — each on its own interval, so
calling it thousands of times per second is cheap.

Without `tick()` the bot still answers questions, but telemetry goes stale,
thermal protection never engages and changes are only written to flash when
you ask.

---

## Conversation

### `String ask(const String& question)`
### `String ask(const char* question)`

The main entry point. Runs the full pipeline: identity, small talk, maths,
sensors, hardware, follow-up, knowledge base, fallback.

Always returns a non-empty reply. Check `getLastError()` and
`getConfidence()` if you need to know how the answer was produced.

```cpp
String answer = bot.ask("what is 15% of 200");
if (bot.getLastError() == AMELTECH_NOT_FOUND) {
    // the bot said so honestly rather than inventing something
}
```

### `String handleSerialLine(const String& line)`

Routes one line of serial input. Training commands go to the console,
`help`, `names` and `forget me` are handled directly, and everything else goes
to `ask()`.

### `bool pollSerial(Stream& stream, String& out)`

Non-blocking line reader. Returns `true` and fills `out` when a complete line
has been read and handled.

```cpp
void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) Serial.println(reply);
    bot.tick();
}
```

---

## Knowledge

### `AmelTechError train(question, answer, category = "custom")`
### `AmelTechError addQA(question, answer, category = "custom")`

Identical; `train` reads better in sketches. Teaches one entry and assigns it a
four-digit data number.

Returns `AMELTECH_OK` on success, or `AMELTECH_DUPLICATE`, `AMELTECH_OVERFLOW`
or `AMELTECH_MEMORY_ERROR`. `getLastStatus()` gives the assigned code.

Taught entries take priority over built-in ones for the same question.

### `AmelTechError removeQA(const String& question)`
### `AmelTechError removeQAByCode(uint16_t code)`

Removes one taught entry. Built-in knowledge cannot be removed.

### `void clearKnowledge()`

Removes every taught entry. Built-in knowledge is untouched.

### `size_t getKnowledgeCount()` / `getBuiltinCount()` / `getUserCount()`
### `uint16_t getLastTrainCode()`

Counts, and the data number of the most recent successful lesson.

### `AmelTechError saveKnowledge()` / `loadKnowledge()`

Explicit NVS write and read. `tick()` saves automatically at most once a
minute, so you rarely need these.

---

## Calculator

### `String calculate(const String& expression)`
### `String calculate(const char* expression)`

Evaluates an expression and returns the formatted result, or an error message.

```cpp
Serial.println(bot.calculate("sqrt(144) + 7!"));   // 5052
```

For the numeric value and detailed errors, use the calculator directly:

```cpp
double v;
if (bot.calculator().evaluateTo("2^10", v)) {
    Serial.println(v);                                 // 1024
} else {
    Serial.println(bot.calculator().lastErrorString());
}

bot.calculator().setAngleMode(CALC_DEGREES);
bot.calculator().setPrecision(6);
```

**Supported**

| Category | Operators and names |
|---|---|
| Arithmetic | `+ - * / %` |
| Power | `^` (right associative, binds tighter than unary minus) |
| Postfix | `!` factorial, `%` percent |
| Grouping | `( )`, `\|x\|` absolute value |
| Roots | `sqrt cbrt` |
| Logs | `ln log log2 log10 exp` |
| Rounding | `floor ceil round trunc abs sign sq` |
| Trigonometry | `sin cos tan asin acos atan atan2 sinh cosh tanh deg rad` |
| Two argument | `pow min max mod hypot gcd lcm` |
| Constants | `pi e tau phi` |

Implicit multiplication (`3(4+5)`, `2pi`), scientific notation (`1.5e3`) and
natural phrasing (`15 percent of 200`, `what is 7 squared`, `12 x 4`) all work.

---

## Sensors

### `bool beginDHT(uint8_t pin, DhtType type = DHT_TYPE_22)`

Configures a DHT sensor. `type` is `DHT_TYPE_11`, `DHT_TYPE_21` or
`DHT_TYPE_22`. Returns `false` for an invalid pin or type.

### `void endDHT()`
### `bool readSensors(bool force = false)`

Reads the sensor, respecting the datasheet interval unless `force` is set.
Returns `false` when no valid reading is available — in which case the bot will
say so rather than reporting an old value as current.

### `const DhtReading& getSensorReading() const`
### `bool hasSensor() const`
### `String getSensorReport()`
### `String getSituationReport()`

`getSituationReport()` is the full analysis: temperature, humidity, feels-like,
dew point, comfort, trends, advice, and a joke when trolling is on.

See [SENSORS.md](SENSORS.md) for the analysis details.

---

## Identity

### `const char* getUserName() const`
### `const char* getUserField() const`

The current user's name and profession, or `nullptr` if unknown.

### `bool rememberUser(const String& name, const String& field = "")`
### `bool forgetUser(const String& name)`
### `void forgetAllUsers()`
### `String listUsers() const`
### `size_t getUserProfileCount() const`

Manual control over the profile store. Names are normally captured
automatically from conversation.

See [IDENTITY.md](IDENTITY.md).

---

## Context

### `void resetContext()`
### `void setContextSize(uint8_t size)`
### `uint8_t getContextSize() const`

The bot remembers the last few turns so follow-ups like "tell me more" work.
`setContextSize(0)` disables it.

---

## Personality

### `void enableTrolling(bool enable)`
### `bool isTrollingEnabled() const`

Humour is only ever appended to real sensor readings, never to facts. On by
default.

### `void setName(const String& botName)`
### `const char* getName() const`

Changes how the bot introduces itself.

---

## Status

### `AmelTechError getLastError() const`
### `const char* getLastStatus() const`
### `float getConfidence() const`
### `MeasurementStatus getMeasurementStatus() const`
### `uint32_t getLastScanMicros() const`

`getConfidence()` returns 0.0–1.0. Compare against `AMELTECH_CONF_STRONG`
(0.90), `AMELTECH_CONF_MODERATE` (0.74) and `AMELTECH_CONF_WEAK` (0.52).

`getMeasurementStatus()` applies to the last hardware or sensor answer and tells
you whether the number was live, cached or unavailable.

**Error codes**

| Code | Meaning |
|---|---|
| `AMELTECH_OK` | success |
| `AMELTECH_INVALID_INPUT` | empty or unusable input |
| `AMELTECH_NOT_FOUND` | no confident match; the bot said so |
| `AMELTECH_LOW_CONFIDENCE` | answered, but hedged |
| `AMELTECH_UNSUPPORTED` | not available on this chip |
| `AMELTECH_UNAVAILABLE` | not configured, e.g. no sensor |
| `AMELTECH_MEASUREMENT_ERROR` | the sensor failed to answer |
| `AMELTECH_MEMORY_ERROR` | out of heap, or the training reserve was hit |
| `AMELTECH_STORAGE_ERROR` | NVS unavailable |
| `AMELTECH_OVERFLOW` | input too long, or storage full |
| `AMELTECH_DUPLICATE` | that entry already exists |
| `AMELTECH_CONFLICT` | the question existed and was updated |

`AmelTechBot::errorToString(err)` gives the name as text.

---

## Hardware

### `const ESP32Telemetry& getTelemetry(bool full = false)`
### `String runDiagnostics(bool full = false)`
### `String getHealthReport()`
### `int getHealthScore()`
### `String getThermalReport()`

Every telemetry field carries a status. Always check it:

```cpp
const ESP32Telemetry& t = bot.getTelemetry(true);
if (Telemetry::statusIsUsable(t.freeHeap.status)) {
    Serial.println(t.freeHeap.value / 1024);
} else {
    Serial.println(Telemetry::statusToString(t.freeHeap.status));
}
```

See [TELEMETRY.md](TELEMETRY.md) for every field and
[STATUS.md](STATUS.md) for the status semantics and health scoring.

---

## Subsystem access

For direct control:

```cpp
bot.knowledge()    // KnowledgeBase
bot.calculator()   // Calculator
bot.sensors()      // SensorHub
bot.telemetry()    // Telemetry
bot.thermal()      // ThermalGuard
bot.diagnostics()  // Diagnostics
bot.profiles()     // UserProfileStore
bot.identity()     // IdentityManager
bot.training()     // TrainingConsole
```

Useful ones:

```cpp
bot.knowledge().rank(query, results, 3);         // top matches with scores
bot.knowledge().lastScanMicros();                // matcher timing
bot.training().setMinFreeHeap(150UL * 1024UL);   // adjust the reserve
bot.thermal().dutyPercent();                     // loop busy percentage
bot.sensors().analyze();                         // structured situation data
```

---

## Statics

```cpp
AmelTechBot::version();                  // "2.0.0"
AmelTechBot::errorToString(err);
UserProfileStore::capacity();            // 34
TrainingConsole::helpText();
TrainingConsole::isTrainingCommand(s);
Telemetry::statusToString(status);
Telemetry::statusIsUsable(status);
SensorHub::dewPoint(tempC, humidity);
SensorHub::heatIndex(tempC, humidity);
AmelTechText::normalize(in, out, size);
AmelTechText::editSimilarity(a, b);
Calculator::formatNumber(value, digits);
```
