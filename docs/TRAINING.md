# Training

The bot learns from you over the Serial Monitor. No recompilation, no
re-flashing, and what you teach it survives a power cycle.

---

## Getting started

Open the Serial Monitor at **115200 baud** with line ending set to **Newline**
or **Both NL & CR**, then type:

```
train | who made you | AmelTech labs made me.
```

```
train successfully and save data number code 0001
```

That four digit number is the **data number code**. It identifies the lesson so
you can delete it later. Codes are assigned in order and are not reused within
a session, so `0001`, `0002`, `0003` and so on.

Now ask the question back:

```
> who made you
AmelTech labs made me.
```

---

## Command reference

The separator is a vertical bar. Spaces around it are optional and are trimmed.

| Command | Effect |
|---|---|
| `train \| <question> \| <answer>` | teach a new question and answer |
| `train \| delete \| 0001` | delete one entry by its data number code |
| `train \| delete \| full data` | delete every entry you taught |
| `train \| delete \| <question>` | delete by question text |
| `train \| list` | list taught entries with their codes |
| `train \| status` | capacity, memory and whether training is open |
| `train \| save` | force an immediate write to flash |
| `train \| help` | command reminder |
| `train` | same as `train \| help` |

`delete`, `remove` and `del` are interchangeable. `full data`, `fulldata`,
`full`, `all` and `everything` all mean the same thing.

### Examples

```
train | what is my favourite colour | Blue, you told me last week.
train successfully and save data number code 0002

train | list
Taught entries (2/48):
0001  who made you  ->  AmelTech labs made me.
0002  what is my favourite colour  ->  Blue, you told me last week.

train | delete | 0001
train delete successfully data number code 0001

train | delete | full data
train delete successfully, 1 taught entry removed. Built-in knowledge is untouched.
```

Deleting never touches the 2037 built-in entries. Only what you taught is
removed.

---

## The memory reserve

Training is blocked while free heap is at or below **200 KB**.

That reserve is not arbitrary. Chat logging, the query scratch buffers and the
matcher's candidate pool all allocate from the heap while a conversation is
running. If lessons were allowed to consume everything, the chatbot would start
failing exactly when it was busiest. The gate makes that impossible.

When the gate is closed you get a clear explanation, not a silent failure:

```
Training is paused. Free heap is 198 KB and 200 KB is reserved so chat
logging never runs out of memory.
Delete an entry with: train | delete | <code>   then try again.
```

Check the current state at any time:

```
train | status
Training status
  Taught entries : 12/48
  Next data code : 0013
  Free heap      : 236 KB
  Reserved heap  : 200 KB kept free for chat logging
  Training open  : yes
  Accepted / rejected lessons: 12 / 1
```

### Changing the reserve

The default is set by `AMELTECH_TRAIN_MIN_FREE_HEAP` in `AmelTechConfig.h`.
To change it at runtime:

```cpp
bot.training().setMinFreeHeap(150UL * 1024UL);   // 150 KB
bot.training().setMinFreeHeap(0);                // disable the gate entirely
```

Disabling the gate is supported but not recommended. If you do, watch
`Diagnostics` — the Memory component will start warning long before anything
breaks.

---

## Capacity

| Limit | Value | Constant |
|---|---|---|
| Taught entries | 48 | `AMELTECH_MAX_USER_ENTRIES` |
| Question length | 127 characters | `AMELTECH_MAX_QUESTION_LEN` |
| Answer length | 319 characters | `AMELTECH_MAX_ANSWER_LEN` |
| Data number codes | 0001–9999 | `AMELTECH_TRAIN_MAX_CODE` |

Each entry occupies roughly 440 bytes of heap, allocated only when the entry
exists. Version 1.1.0 reserved 32 entries as a static array whether they were
used or not, costing about 21.5 KB of RAM permanently.

---

## Error messages

Every rejection explains itself.

| Situation | Message |
|---|---|
| Missing answer | `The answer is missing. Use: train \| question \| answer` |
| Blank part | `Use: train \| question \| answer  (both parts are required)` |
| Already taught, same answer | `That question is already taught with the same answer, so nothing changed.` |
| Already taught, new answer | Updated, with `(the previous answer for that question was replaced)` |
| Too long | Names the exact character limits |
| 48 entries used | `Training memory is full (48 entries). Delete one with: train \| delete \| <code>` |
| Heap reserve reached | Explains the reserve and how much is free |
| Unknown code | `No taught entry has data number code 0042. Use: train \| list  to see the codes in use.` |

---

## Training from a sketch

The console is a convenience wrapper. The same thing works from code:

```cpp
AmelTechError err = bot.train("who made you", "AmelTech labs made me.");
if (err == AMELTECH_OK) {
    Serial.print("stored as code ");
    Serial.println(bot.getLastTrainCode());
} else {
    Serial.println(AmelTechBot::errorToString(err));
}
```

`addQA()` is an alias for `train()`. Both accept an optional category.

To remove:

```cpp
bot.removeQA("who made you");
bot.removeQAByCode(1);
bot.clearKnowledge();       // taught entries only
```

---

## Persistence

Taught entries live in the NVS partition under the namespace `ameltech_kb`.

Writes are **deferred and throttled**. Nothing is written on every change;
instead a dirty flag is set and the data is flushed at most once per minute
from `tick()`, plus once on `end()` and once when you say goodbye. Flash
endurance is finite and version 1.1.0 wrote on every single call.

Force a write when you need one:

```cpp
bot.saveKnowledge();
```

Loading happens automatically inside `begin()`. The heap guard is bypassed
during load, because refusing to restore what is already stored would be
worse than useless.

`saveToNvs()` also removes keys left over from entries that no longer exist.
Version 1.1.0 left stale keys behind whenever the entry count shrank, so a
deleted entry could reappear after a reset.

---

## Matching what you taught

Taught entries are indexed exactly like built-in ones: same normalizer, same
token signature, same trigram sketch, same scoring. They are searched first,
so a lesson always beats a built-in entry of equal confidence.

This means you do not have to phrase your question the same way to get your
answer back:

```
train | who built this robot | My team at the workshop did.
train successfully and save data number code 0003

> who built this robot
My team at the workshop did.

> tell me about who built this robot
My team at the workshop did.

> who biult this robot
My team at the workshop did.
```

---

See also: [API.md](API.md), [ARCHITECTURE.md](ARCHITECTURE.md).
