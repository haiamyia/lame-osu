# LAME

Help, questions, or suggestions → Discord: **haiamyia**

This was done in 3 hours do NOT expect anything good from it. The purpose of this was to provide a simple base.

I might keep this updated if anyone will use this.

**LAME** — external cheetos for [osu!](https://osu.ppy.sh/) stable and osu!lazer.

```
osu! (stable / lazer)
        │
        ▼  memory read
┌───────────────────┐
│  cache threads    │  process attach · game state · beatmap load
└─────────┬─────────┘
          ▼
┌───────────────────┐
│  gameplay modules │  aim assist · relax · replay bot
└─────────┬─────────┘
          ▼
┌───────────────────┐
│  overlay (ImGui)  │  simple menu · Insert to toggle
└───────────────────┘
```

## Features

### Aim assist
Cursor assist that pulls toward upcoming hit objects while preserving your own hand movement.

- **Hand + offset model**
- **Tablet mode**
- **Require motion**
- **Angle gating**
- **On-note hold**
- Tunable pull strength, close/far return rates, timing window, motion curve, and more

### Relax
Timing-based tap assist that schedules clicks from beatmap object times.

- Gaussian timing and hold length
- Alternation logic for fast sections
- Uses in-game or custom Z/X (configurable in System tab)

### Replay bot
Plays back `.osr` replay files.

- Frame-accurate sync to game time with speed and offset controls
- HR mismatch correction and optional replay flip
- Optional disable aim / disable clicking
- Speed multiplier

### System
- Auto-detects **osu!stable** or **osu!lazer** and attaches when a match is found
- **Stream proof** — overlay excluded from screen capture
- Custom left/right key bindings

## Requirements

| Requirement | Notes |
|-------------|--------|
| Windows 10/11 | x64 only |
| Visual Studio 2022 | v143 toolset, Desktop development with C++ |
| osu! stable or osu!lazer | Running on the same machine |
| OpenTabletDriver | Required for tablet users |

## Build

1. Clone the repository.
2. Open `lame.vcxproj` in Visual Studio 2022 (or build from Developer Command Prompt).
3. Select **Release | x64**.
4. Build. Output: `builds/x64/Release/lame.exe`.

```powershell
msbuild lame.vcxproj /p:Configuration=Release /p:Platform=x64
```

Run `lame.exe` while osu is open. The menu appears over the osu client. Press **Insert** to show or hide the overlay.

## Usage overview

| Tab | Purpose |
|-----|---------|
| **Aimbot** | Enable aim assist, tablet mode, tuning sliders |
| **Relax** | Enable relax, timing / alternation settings |
| **Replay** | Load `.osr`, speed, offset, flip options |
| **System** | Keys, stream proof, attach / beatmap diagnostics |

**Tips**
- Use **tablet mode** with OTD absolute pen; turn **tablet mode** off for mouse play.
- Disable aim assist and relax when using the replay bot.
- Lazer offsets may need updating after major osu!lazer updates (`impl/defs/offsets_lazer.hxx`).

## Architecture

| Layer | Role |
|-------|------|
| `core/threads/cache.hxx` | Background threads: process discovery, game snapshot, beatmap reload |
| `core/game/` | `osu!stable` and `osu!lazer` clients behind `i_osu_client` |
| `core/beatmap/` | Stable `.osu` parser, lazer memory reader, Songs resolver |
| `core/aim_assist/` | Aim assist module |
| `core/relax/` | Relax tap module |
| `core/replay/` | `.osr` LZMA decode + replay playback |
| `impl/memory/` | Clean `ntdll` syscall stubs, `NtUserSendInput`, process I/O |
| `impl/input/` | `WH_MOUSE_LL` hook thread |
| `impl/ui/` | ImGui overlay |

Memory reads use a disk-loaded clean `ntdll.dll` to resolve syscall numbers, then call through the live module via indirect syscalls.

## Project structure

```
lame/
├── core.cxx
├── core/
│   ├── aim_assist/
│   ├── beatmap/
│   ├── game/
│   ├── relax/
│   ├── replay/
│   └── threads/
├── impl/
│   ├── defs/
│   ├── deps/
│   ├── input/
│   ├── memory/
│   ├── struct/
│   └── ui/
└── lame.vcxproj
```

## Configuration reference (aim assist)

| Setting | Description |
|---------|-------------|
| **Pull strength** | How hard assist pulls toward the note (moderate ≤ ~9, stronger above) |
| **Close return** | How fast assist offset returns to your hand when near a note |
| **Far return** | Return speed when far from the target |
| **Timing window** | How early (ms) before a hit time assist can engage |
| **Min hand speed** | Minimum movement speed required when “Require motion” is on |
| **Max aim angle** | Maximum angle between your movement and the note before assist cuts off |
| **Blend angle** | Angle below which assist is fully allowed |
| **Motion curve** | Smooths the angle-based assist ramp |
| **Tablet mode** | For OTD absolute tablets |
| **Require motion** | No assist unless you are actively moving |

## Legal & fair use

This project is provided for **education and research**.

- Using this kind of software in osu! may violate [osu!’s Terms of Service](https://osu.ppy.sh/legal/terms) and result in account restrictions.
- I do not encourage cheating on ranked multiplayer or leaderboards.
- You are responsible for how you use this software.

## Contributing

Issues, suggestions, and help requests are welcome → Discord: **haiamyia**

Pull requests are also welcome (especially lazer offsets updates after game patches).

## License

This project is licensed under the **MIT License** — see [LICENSE](LICENSE).

You are free to use, copy, modify, merge, publish, and distribute the code, including commercially. The only requirement is to keep the copyright notice and license text when you share it.

---

*Not affiliated with ppy or osu!.*
