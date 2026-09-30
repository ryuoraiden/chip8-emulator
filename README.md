# Chip-8 Emulator

A Chip-8 emulator built in C++ with SDL2 graphics and audio support.

> **TatHack '26 submission (PS1).** We started from the organizers' partially broken emulator, fixed the planted bugs in opcode handling, memory, timing and rendering, and added speed control, color palettes and save states. See [Bugs fixed](#bugs-fixed) and [AI usage](AI_USAGE.md).

## Features

- All 35 Chip-8 opcodes, with correct VF flag behaviour for the 8XYN family
- 64x32 pixel display with SDL2 rendering, 10x scaled
- Keyboard input, including the blocking wait-for-key instruction (FX0A)
- 440 Hz square-wave beep
- Fixed 60 Hz timestep: timers always tick at 60 Hz, independent of CPU speed
- **Configurable emulation speed**: 60 to 8000 instructions per second with `+` / `-`, plus a `--speed N` multiplier
- **Color palettes**: 7 schemes (Classic Green Screen, Amber CRT, Neon High-Contrast and more) with `[` / `]` or `--palette`
- **Save states**: `F5` saves the whole machine, `F9` restores it
- **CRT mode** (`--crt`): screen curvature, scanlines, bloom, vignette and phosphor afterglow, which also removes the flicker caused by XOR drawing
- Command-line validation with a clear usage message (`--help`)
- Robust against bad input: missing, empty or oversized ROMs are rejected before a window opens; stack overflow/underflow and out-of-range key numbers are caught instead of corrupting memory; falls back to software rendering when no GPU driver is available

## Architecture

Chip-8 is a virtual machine from the 1970s designed to make programming video games easier on early microcomputers.

### System Specifications

- **Memory**: 4 KB (4096 bytes)
  - `0x000-0x1FF`: Reserved for interpreter and fonts
  - `0x200-0xFFF`: Program/ROM space
- **Registers**:
  - 16 8-bit general-purpose registers (V0-VF)
  - VF doubles as a flag register for arithmetic operations
  - 16-bit index register (I)
  - 16-bit program counter (PC)
  - 8-bit stack pointer (SP)
- **Display**: 64x32 pixels, monochrome
- **Timers**:
  - Delay timer (counts down at 60 Hz)
  - Sound timer (beeps when > 0, counts down at 60 Hz)
- **Stack**: 16 levels for subroutine calls
- **Keypad**: 16-key hexadecimal input

### Code layout

| File | Role |
|---|---|
| `src/chip8.h`, `src/chip8.cpp` | The machine: memory, registers, fetch/decode/execute, timers, save states. No SDL. |
| `src/main.cpp` | SDL window, input, audio, command-line options and the main loop |

## Dependencies

- SDL2 library

### Installation

**Arch Linux:**
```bash
sudo pacman -S sdl2
```

**Ubuntu/Debian:**
```bash
sudo apt-get install libsdl2-dev
```

**macOS:**
```bash
brew install sdl2
```

## Building

1. Clone the repo
```bash
git clone https://github.com/ryuoraiden/chip8-emulator.git
cd chip8-emulator
```

2. Make it
```bash
make
```

The Makefile tracks header dependencies, so editing `chip8.h` rebuilds everything that includes it.

## Usage
```bash
./chip8 [--speed N] [--palette name|index] [--crt] <path-to-rom-file>
```

| Option | Meaning |
|---|---|
| `--speed N` | Multiply the emulation speed by N (a whole number from 1 to 100) |
| `--palette P` | Start with a color scheme, by index (`0`-`6`) or by name (quote names with spaces) |
| `--crt` | CRT look: curvature, scanlines, bloom and phosphor afterglow |
| `--help` | Show usage and the list of palettes |

**Examples:**
```bash
./chip8 roms/Pong.ch8
./chip8 --palette "Amber CRT" roms/Tetris.ch8
./chip8 --speed 2 roms/Blinky.ch8
./chip8 --crt roms/Pong.ch8
```

ROM file names are case-sensitive on Linux. Invalid options print an error and the usage text instead of crashing.

## Keyboard Mapping

The original Chip-8 keypad is mapped to keyboard keys:
```
Chip-8 Keypad:          QWERTY Keyboard:
┌─┬─┬─┬─┐               ┌─┬─┬─┬─┐
│1│2│3│C│               │1│2│3│4│
├─┼─┼─┼─┤               ├─┼─┼─┼─┤
│4│5│6│D│               │Q│W│E│R│
├─┼─┼─┼─┤      =        ├─┼─┼─┼─┤
│7│8│9│E│               │A│S│D│F│
├─┼─┼─┼─┤               ├─┼─┼─┼─┤
│A│0│B│F│               │Z│X│C│V│
└─┴─┴─┴─┘               └─┴─┴─┴─┘
```

**Emulator controls:**

| Key | Action |
|---|---|
| `+` / `-` | Faster / slower (steps: 60, 120, 250, 350, 500, 750, 1000, 1500, 2000, 4000, 8000 instructions per second; default 500) |
| `[` / `]` | Previous / next color palette |
| `F5` | Save state to `<rom>.sav` |
| `F9` | Load state from `<rom>.sav` |
| `Esc` | Quit |

The window title shows the current speed and palette.

### Game-Specific Controls

**PONG:**
- Left paddle: `1` (up), `Q` (down)
- Right paddle: `4` (up), `R` (down)

**TETRIS:**
- `Q` - Rotate
- `W` - Drop
- `E` - Move right
- `A` - Move left

Note: this Tetris ROM has no game-over routine. When the stack reaches the top it keeps spawning pieces; that is the ROM's own behaviour, not an emulator bug.

## Save States

`F5` writes the complete machine state (memory, registers, index, program counter, stack, timers and screen) to `<rom file>.sav`, for example `roms/Pong.ch8.sav`. `F9` restores it.

- **Format**: a `CH8S` magic tag, a version byte, then each field in a fixed order (6204 bytes). Fields are written one by one rather than dumping the object, so padding or future class changes can't silently corrupt saves.
- **Safe saving**: the file is written to `<rom>.sav.tmp` first and then renamed over the old save, so a failed save never destroys the previous one.
- **Safe loading**: the file is read into a separate machine and checked (tag, version, exact length, valid stack pointer) before anything is copied into the running game. A missing, truncated or foreign file is rejected and the game keeps running unchanged.
- Held keys are cleared on load, and speed and palette are not part of the save (they are emulator settings, not machine state).
- Multi-byte fields use the host byte order, so saves are portable between little-endian machines (x86, ARM), which is every common PC.

## Implementation Details

### Instruction Set

The emulator implements all 35 Chip-8 instructions, including:
- **Arithmetic**: ADD, SUB, AND, OR, XOR, shift operations (VF is always written after the result, so it wins when X is F)
- **Graphics**: Draw sprites with XOR mode, collision detection
- **Flow control**: Jump, call/return subroutines, conditional skips
- **Memory**: Load/store registers, BCD conversion (all memory accesses are masked to 4 KB)
- **Timers**: Delay and sound timer operations
- **Input**: Key press detection (blocking and non-blocking)

### Timing

The main loop uses a fixed timestep. Real elapsed time is accumulated, and one emulated frame runs for every 1/60 s: it executes `speed / 60` instructions (the fractional remainder carries over to the next frame, so the average speed is exact) and then ticks the timers once. The elapsed time per loop is capped at 0.25 s, so after a stall (window drag, debugger pause) the emulator catches up at most 15 frames instead of jumping ahead.

### Display

Graphics are rendered using SDL2:
- Each Chip-8 pixel is scaled 10× for visibility (640×320 window)
- XOR-based sprite drawing for collision detection
- Sprites wrap at their start position and clip at the screen edges

### Audio

A 440 Hz square wave (musical note A) plays while `sound_timer > 0`. The audio buffer is 512 samples (about 12 ms), so beeps only a few frames long start and stop on time.

## Bugs Fixed

| Area | Bug | Fix |
|---|---|---|
| 00EE (return) | Read the stack before decrementing `sp`, so returns jumped to `0x2` | Decrement first: `stack[--sp]` |
| FX33 (BCD) | Tens digit stored as `value/10` (123 gave 1, 12, 3) | `(value/10) % 10` |
| FX55 / FX65 | Loop stopped before VX | Loop through VX inclusive |
| FX0A (wait for key) | Advanced even with no key held | Only advance `pc` once a key is found |
| Rendering | Screen drawn upside down (`31 - y`) | Row 0 at the top |
| Timing | A 16 ms delay after every instruction (about 60 instructions/s) and timers ticking per instruction | Fixed 60 Hz timestep, timers ticked once per frame |
| Audio | Square wave flipped every 100 samples (about 220 Hz) | Derived from `TONE_HZ = 440` |
| 8XY4-8XYE | VF written before the result; `>` instead of `>=` for borrow | Result first, flag last; `>=` |

## ROMs

`roms/` contains Pong, Tetris and Blinky (provided with the original repository), plus Soccer and Tic-Tac-Toe (by David Winter), all from public Chip-8 ROM collections. All game credit goes to their original authors.

## Resources

- [Chip-8 ROMs Archive](https://github.com/kripod/chip8-roms)
- [Cowgod's Chip-8 Technical Reference](http://devernay.free.fr/hacks/chip8/C8TECH10.HTM)
- [AI usage disclosure](AI_USAGE.md)
