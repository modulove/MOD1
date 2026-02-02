# ADSR/AD Envelope Generator (Beta)

**Community-driven firmware for HAGIWO MOD1**

This firmware extends the original HAGIWO ADSR design with features requested by the Modulove community. 
All additions are backward-compatible - the original functionality remains intact.

![MOD1 ADSR](https://modulove.io/mod1-adsr)

---


## Envelope Modes

### 1. ADSR Mode (Classic)
Traditional gate-based envelope with Attack, Decay, Sustain, and Release phases.

```
Gate ──┐     ┌──────────────┐     ┌──
       │     │              │     │
       └─────┘              └─────┘

       ╱╲
      ╱  ╲___________
     ╱               ╲
    ╱                 ╲____
   A    D      S        R
```

- **Trigger Input (F1):** Gate signal controls envelope
- **POT1:** Attack time (1ms - 5s)
- **POT2:** Decay time (1ms - 5s)
- **POT3:** Release time (1ms - 5s)
- **Button (short):** Cycle through 6 sustain levels
- **Curve:** Set via Serial or WebSerial (Linear/Exponential/Logarithmic)

### 2. Trigger/AD Mode
One-shot envelope perfect for percussive sounds, piano-style plucks, and drums. Ignores gate length - fires complete Attack→Decay cycle on trigger.

```
Trigger ──┐  ┌──┐  ┌──
          │  │  │  │
          └──┘  └──┘

          ╱╲    ╱╲    ╱╲
         ╱  ╲  ╱  ╲  ╱  ╲
        ╱    ╲╱    ╲╱    ╲
       A   D  A  D  A   D
```

- **Trigger Input (F1):** Any trigger fires complete envelope
- **POT1:** Attack time
- **POT2:** Decay time
- **POT3:** Curve morph (Linear ↔ Exponential) - **Real-time control!**
- Sustain level is ignored - always decays to zero

### 3. Dual AD Mode
Two independent Attack-Decay envelopes with shared time controls. Perfect for stereo patches or complex modulation.

```
F1 Trig ──┐        ┌──
          └────────┘
          ╱╲
         ╱  ╲______
        A    D         ENV1 → F3

F2 Trig ────┐    ┌────
            └────┘
            ╱╲
           ╱  ╲____
          A    D       ENV2 → F4
```

- **F1:** ENV1 Trigger Input
- **F2:** ENV2 Trigger Input
- **F3:** ENV1 Output
- **F4:** ENV2 Output
- **POT1:** Attack time (shared)
- **POT2:** Decay time (shared)
- **POT3:** Curve morph (shared)
- **Button:** Manual trigger for ENV1

#### Dual Mode with Normalization
Enable "Normalize Triggers" to have F1 trigger BOTH envelopes simultaneously, freeing F2 for other uses:

| F2 Option | Description |
|-----------|-------------|
| Attack Gate | HIGH during attack phase |
| Decay Gate | HIGH during decay phase |
| EOC Pulse | 10ms trigger at end of cycle |
| Reverse EG | Inverted ENV1 output |
| CV Decay | 0-5V controls decay time |
| CV Time | 0-5V controls all times |

---

### F2/F3 Output Options (Single Modes)

| Mode | Code | Description | Use Case |
|------|------|-------------|----------|
| Attack Gate | 0 | HIGH during attack | Trigger other modules at start |
| Decay Gate | 1 | HIGH during D/S/R | Gate for secondary envelope |
| EOC Pulse | 2 | 10ms pulse at end | Clock/trigger sequencers |
| Reverse EG | 3 | Inverted envelope | Ducking, sidechaining |
| CV Decay | 4 | 0-5V input | Modulate decay time |
| CV Time | 5 | 0-5V input | Modulate all times globally |

### CV Input Response

**CV Time (Global):**
```
0V   → 0.25× speed (4× longer envelopes)
2.5V → 1× speed (normal)
5V   → 4× speed (4× shorter envelopes)
```
*Exponential respons*

**CV Decay:**
```
0V   → 0.1× speed (10× longer decay)
2.5V → 1× speed (normal)
5V   → 2× speed (2× shorter decay)
```
*Linear response*

---

## Button Control

All modes can be selected without computer/WebSerial using the button:

| Press Duration | Action | LED Feedback |
|----------------|--------|--------------|
| Short (<0.8s) | Cycle sustain level (ADSR only) | - |
| Long (1-3s) | Toggle ADSR ↔ Trigger mode | 1 or 2 blinks |
| Extra Long (>3s) | Toggle Dual mode ON/OFF | 3 blinks |

### LED Blink Patterns
- **1 blink:** ADSR mode
- **2 blinks:** Trigger/AD mode  
- **3 blinks:** Dual AD mode

---

## Envelope Curves

### Available Curves (ADSR Mode)

| Curve | Attack Shape | Decay/Release Shape | Best For |
|-------|--------------|---------------------|----------|
| Linear | Constant rate | Constant rate | Modulation, LFO-like |
| Exponential | Slow→Fast | Fast→Slow | Natural sounds, piano |
| Logarithmic | Fast→Slow | Slow→Fast | Pads, swells |

### Real-time Curve Control (Trigger/Dual Modes)

In Trigger and Dual modes, **POT3 becomes a curve morph control:**
- Full CCW: Linear envelope
- Full CW: Exponential envelope
- Middle positions: Blend between curves

---

## Serial Configuration

Connect via USB (115200 baud) and send commands:

```
HELP              Show all commands
STATUS            Show current configuration

MODE=ADSR         Switch to ADSR mode
MODE=TRIGGER      Switch to Trigger/AD mode
MODE=DUAL         Switch to Dual AD mode

CURVE=LIN         Linear envelope curve
CURVE=EXP         Exponential curve (default)
CURVE=LOG         Logarithmic curve

F2=0              F2 = Attack Gate
F2=1              F2 = Decay Gate
F2=2              F2 = EOC Pulse
F2=3              F2 = Reverse Envelope
F2=4              F2 = CV Decay Input
F2=5              F2 = CV Time Input

F3=0-5            Same options as F2

NORM=1            Enable trigger normalization (Dual mode)
NORM=0            Disable trigger normalization
```

---

## WebSerial Configuration

A browser-based configuration page is included (`ADSR_Config_v2.5.html`).

**Requirements:**
- Chrome, Edge, or Opera browser
- USB connection to MOD1

**Features:**
- Visual mode selection
- Curve preview graphics
- Pin configuration dropdowns
- Real-time visualization
- Factory reset option

---

## Use Cases

### Sidechaining / Ducking
1. Set Mode: ADSR or Trigger
2. Set F2 or F3: Reverse Envelope
3. Patch reversed output to VCA controlling pad/bass
4. Trigger with kick drum → instant sidechain compression effect

### Velocity-like Dynamics
1. Set Mode: Trigger/AD
2. Set F2: CV Time Input
3. Patch velocity CV to F2
4. Harder hits = faster envelopes

### Stereo Envelopes
1. Set Mode: Dual AD
2. Patch F1 to left channel trigger
3. Patch F2 to right channel trigger
4. F3 → Left VCA, F4 → Right VCA

### Complex Modulation
1. Set Mode: Dual AD with Normalization ON
2. F1 triggers both envelopes
3. F2 set to Reverse Envelope
4. F3 = Normal envelope, F2 = Inverted
5. Crossfade between two sounds

---

## Version History

| Version | Changes |
|---------|---------|
| Original | Basic ADSR (HAGIWO design) |
| v1.1 | Added CV Time input, Dual AD mode |
| v1.2 | Trigger/AD mode, Button mode switching, EEPROM, WebSerial config |

---

## Credits

- **Original Design:** HAGIWO
- **Extended Firmware:** Modulove Community
- **Feature Requests:** Users who wanted more from their 4HP!

---

## Links

- [Modulove Website](https://modulove.io)
- [Original HAGIWO Project](https://note.com/solder_state)

---

*Made with 💜 in Hamburg*
