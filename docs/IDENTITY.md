# Name memory

The bot remembers up to **34 people** by name. The memory lives in NVS, so it
survives a reset and a power cycle.

---

## Teaching it your name

Just say it in an ordinary sentence:

```
> hi my name is joky pk
Nice to meet you, Joky Pk. I will remember your name.
```

Recognised introductions:

| Pattern | Example |
|---|---|
| `my name is X` | "my name is Arjun" |
| `name is X` | "the name is Ravi" |
| `i am X` | "I am Meera" |
| `i'm X` | "I'm Sam" |
| `i am called X` | "I am called Priya" |
| `call me X` | "call me Joky" |
| `you can call me X` | "you can call me Sam" |
| `this is X` | "this is Arjun" |
| `myself X` | "myself Ravi" |

Names are stored in title case, so `joky pk` becomes `Joky Pk`. A name may be
one, two or three words.

### Fields

If you mention what you do, that is saved too:

```
> my name is arjun. I'm an engineering student
Nice to meet you, Arjun. I have saved that you are an engineering student.
```

Recognised: `i am a/an X`, `i work as X`, `i am studying X`, `i am doing X`.
The field is only accepted when it contains a recognisable occupation word
(student, engineer, doctor, teacher, developer, farmer, nurse and about forty
others), so "I am here" is never stored as a profession.

---

## After a power cycle

The bot does not assume who is talking. The first thing you say triggers a
check against the most recently seen name:

```
> what is esp32
Are you Sam?
```

Your question is **not lost**. It is held and answered as soon as the identity
is settled.

### Saying yes

```
> yes
Good to see you again, Sam. Still a teacher? ESP32 is a family of low-cost,
low-power system-on-chip microcontrollers with integrated Wi-Fi and Bluetooth
capabilities, developed by Espressif Systems, Sam.
```

You can attach a new question to the confirmation and it will be answered
instead of the held one:

```
> yes what is i2c
Good to see you again, Sam. I2C (Inter-Integrated Circuit) is a synchronous
multi-master, multi-slave serial communication bus...
```

Accepted as yes: `yes`, `y`, `yeah`, `yep`, `yup`, `ya`, `correct`, `right`,
`true`, `sure`, `ok`, `indeed`, `exactly`, `that's me`, `i am`, `it's me`,
`you are right`.

### Saying no

Each "no" moves to the next remembered name, most recently seen first:

```
> no
Are you Arjun?
> no
Are you Meera?
> no
Are you Ravi?
> no
How are you,.. What is your name?
```

After **four** rejections it stops guessing and asks the open question. That
limit is `AMELTECH_IDENTITY_MAX_GUESSES`.

If fewer than four names are stored, it asks the open question as soon as the
list runs out.

Accepted as no: `no`, `n`, `nope`, `nah`, `negative`, `wrong`, `incorrect`,
`false`, `never`, `not me`, `that's not me`, `i am not`, `you are wrong`.

### Answering the open question

```
> my name is priya. I am a doctor
Nice to meet you, Priya. I have saved that you are a doctor. ESP32 is a family
of low-cost, low-power system-on-chip microcontrollers...
```

A bare name works too — at this point in the conversation `Priya` on its own is
unambiguous.

---

## State machine

```
                        power on / reset
                               |
                               v
                     +-------------------+
                     |  boot pending     |   any names stored?
                     +-------------------+
                          |          |
                     no   |          | yes
                          v          v
                    +--------+   +-----------+
              +---> |  IDLE  |   |  CONFIRM  | <---+
              |     +--------+   +-----------+     |
              |          ^            |            |
              |          |    yes     |  no        | next name
              |          +------------+  (< 4)     | remains
              |          |            |            |
              |          |            +------------+
              |          |            |
              |          |            | 4th no, or list exhausted
              |          |            v
              |          |     +--------------+
              |          +-----|  AWAIT_NAME  |
              |    name given  +--------------+
              |                       |
              +-----------------------+
                 2 failed attempts, gives up and
                 answers the question normally
```

Two guards stop the conversation ever getting stuck:

- In `CONFIRM`, an answer that is neither yes nor no is re-asked **once**. On
  the second unparseable answer the bot drops to `IDLE` and simply answers the
  question.
- In `AWAIT_NAME`, it asks at most **twice** more before giving up.

Introducing yourself works from any state and immediately settles the identity.

---

## How your name is used

Once known, your name appears in replies — but not in every reply, which would
get grating fast. The rule is: once immediately after confirmation, then every
`AMELTECH_NAME_MENTION_GAP` (default **3**) replies.

```
> what is i2c
Are you Priya?
> yes
Good to see you again, Priya. I2C is ... , Priya.     <- confirmation
> what is spi
SPI is ...                                            <- 1
> what is uart
UART is ...                                           <- 2
> what is pwm
PWM is ...                                            <- 3
> what is adc
ADC ... , Priya.                                      <- mentioned again
```

The name is never appended to a multi-line report, or to a reply that already
contains it, or to anything longer than 220 characters.

---

## Capacity and eviction

| Limit | Value | Constant |
|---|---|---|
| Names stored | 34 | `AMELTECH_MAX_PROFILES` |
| Name length | 31 characters | `AMELTECH_PROFILE_NAME_LEN` |
| Field length | 47 characters | `AMELTECH_PROFILE_FIELD_LEN` |
| Guesses before giving up | 4 | `AMELTECH_IDENTITY_MAX_GUESSES` |
| Replies between name mentions | 3 | `AMELTECH_NAME_MENTION_GAP` |

When all 34 slots are full and a new name arrives, the **least recently seen**
profile is removed — the 34th in recency order. Everyone else keeps their spot.
Simply being greeted refreshes your position, so regular users are never
evicted in favour of a one-off visitor.

The 34 slots are a static array, about 3.0 KB of RAM. They are not heap
allocated, so a full profile store cannot fragment memory or fail at an awkward
moment.

---

## Asking about yourself

```
> what is my name
You are Priya, and you told me you are a doctor.
```

Also recognised: `who am i`, `do you know my name`, `do you remember me`.

If it does not know yet, it starts the identity conversation.

---

## Managing profiles from a sketch

```cpp
bot.rememberUser("Arjun", "engineering student");
bot.forgetUser("Arjun");
bot.forgetAllUsers();

Serial.println(bot.listUsers());
Serial.println(bot.getUserProfileCount());

const char* name = bot.getUserName();     // nullptr when unknown
const char* field = bot.getUserField();   // nullptr when not set
```

From the Serial Monitor:

```
names
Remembered names: 3/34
1. Priya - a doctor (5 chats)
2. Sam (2 chats)
3. Arjun - an engineering student (7 chats)

forget me
Forgotten. I no longer remember Priya.
```

---

## What is not a name

The extractor rejects a candidate when any word is:

- a feeling or state — `fine`, `good`, `tired`, `busy`, `sorry`, `hungry`,
  `ready`, `here`, `back`, `lost`, `late` and about forty more
- an occupation — those belong in the field, not the name
- non-alphabetic, shorter than 2 characters, or longer than 20
- more than three words in total

So "I am fine" and "I am a student" never create a profile called *Fine* or
*Student*.

---

## Storage detail

Profiles live in NVS under the namespace `ameltech_id`, written in recency
order (`n0` is the most recent). Writing in that order means that if a load is
ever truncated, the newest and most useful profiles are the ones that survive.

Writes are deferred and throttled the same way knowledge is: a dirty flag, then
a flush at most once a minute from `tick()`, plus one on `end()`.

---

See also: [API.md](API.md), [TRAINING.md](TRAINING.md).
