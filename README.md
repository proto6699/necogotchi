# necogotchi

**Why only talk to Neco on your PC when she can live in your pocket?**

necogotchi puts [Neco](https://github.com/proto6699/neco-ai) on a ~$15 ESP32 "Cheap Yellow Display": a 2.8-inch touch screen with a speaker, small enough to ride in a laptop bag. Part Tamagotchi, part terminal. She sits on a CRT-styled home screen, meows when you poke her, falls asleep when ignored, plays the music on your SD card, has idle thoughts, and talks back through the DeepSeek API. She keeps her own reflections on the board, in the same format as neco-ai.

It's an experiment, not a product: a continuity system squeezed onto a microcontroller with no PSRAM, to see what a persistent character feels like when she lives in a physical object instead of a browser tab.

<p align="center">
  <img src="docs/photos/home.jpg" width="49%" alt="necogotchi home screen: Neco, her line, the dock">
  <img src="docs/photos/chat.jpg" width="49%" alt="a real reply from DeepSeek on the chat screen">
</p>
<p align="center">
  <img src="docs/photos/boot.jpg" width="32%" alt="boot screen: ECH0 BIOS">
  <img src="docs/photos/settings.jpg" width="32%" alt="settings screen">
  <img src="docs/photos/back.jpg" width="32%" alt="the back: ESP32, speaker on the SPEAK connector, acrylic case">
</p>

*Photos of the real device. The chat screen shows an actual reply: she noticed she had just powered on, had no messages yet, and that music was already playing. The blue tint is the phone camera on an LCD.*

---|---|
| ![necogotchi on the desk](docs/photos/front.jpg) | ![in the hand](docs/photos/hand.jpg) |
| ![the back: board, speaker, case](docs/photos/back.jpg) | ![in the laptop bag](docs/photos/bag.jpg) |

---

## Status (honest)

| | |
|---|---|
| Runs on hardware | yes: one dual-USB ESP32-2432S028 in an acrylic case with a small speaker. display, touch, setup portal, SD music and DeepSeek chat seen working on it (photos above) |
| Builds | zero warnings in project code, Arduino core 2.0.17, 1.50 MB of the 3 MB app partition |
| Not tested | the micro-USB-only CYD and other variants; long-term reflection quality; battery use |

---

## What it works on

**Board:** ESP32-2432S028 "Cheap Yellow Display" (CYD), 2.8 inch, 320x240.

| Part | Used for |
|---|---|
| ESP32 (240 MHz dual core, ~320 KB RAM, no PSRAM, 4 MB flash) | everything |
| ILI9341 panel | the screen. The **dual-USB** version needs colour inversion and a gamma fix, both on by default |
| XPT2046 resistive touch | taps, keyboard. Use a fingernail or stylus |
| Speaker on the 2-pin **SPEAK** connector (GPIO26) | meows, purrs, music |
| microSD (FAT32) | your music, any folders |
| RGB LED on the back | breathes with her state |
| Light sensor (LDR) | auto-brightness, and she knows when she's in the dark bag |

You also need Wi-Fi (a phone hotspot is fine) and a **DeepSeek API key** (platform.deepseek.com) for her to talk. Without them she still works as an offline pet and music player.

**Single-USB (micro-USB only) CYD:** probably works. Turn off *invert color* in SET > MORE > MORE if the colours look like a photo negative.

---

## Install (browser, nothing to download but the .bin)

1. Download [`firmware/necogotchi-v0.6.1.bin`](firmware/necogotchi-v0.6.1.bin).
2. Open **https://web.esphome.io** in Chrome, Edge, Brave or Chromium (Firefox can't talk to USB).
3. Plug in the CYD with a **USB-A** cable (the CYD's USB-C port doesn't work with C-to-C cables).
4. **Connect** → pick the port (CH340 / USB Serial) → **Install** → choose the `.bin`.
   - First install: erasing is fine.
   - **Updates: don't erase**, or you lose Wi-Fi, key, calibration and her reflections.
5. Stuck on *Connecting...*? Hold the **BOOT** button until the progress bar moves.

### Linux notes

```bash
sudo usermod -aG uucp $USER        # Arch/CachyOS (Debian/Ubuntu: dialout), then log out and back in
ls /dev/ttyUSB*                     # the board should show up
```
"Failed to open serial port": close the Arduino IDE / other tabs, and if `sudo fuser -v /dev/ttyUSB0` shows `brltty`, disable it. Flatpak Chrome can't see USB; use the distro package.

Terminal alternative:
```bash
esptool --chip esp32 --port /dev/ttyUSB0 write_flash 0x0 necogotchi-v0.6.1.bin
```

### First boot

1. CRT power-on, BIOS text, a meow (volume starts low).
2. **Touch calibration:** tap the four corner crosses. The pressure readout at the bottom shows whether touch is being read. Draw to test, then DONE.
3. **Setup hotspot:** the screen shows a QR code, `NECO-SETUP-xxxx` and an 8-digit PIN. Scan it with your phone; the setup page opens by itself (or go to `192.168.4.1`).
4. Pick your Wi-Fi, type the password, **paste your DeepSeek key**, choose what she calls you, the cat, your UTC offset and how often she has idle thoughts. **SAVE**. She restarts and joins.
5. Put a FAT32 microSD with MP3s in the slot. She finds them in any folder and starts playing.

---

## The UI

*Screens rendered on a PC from the firmware's layout, font and sprite (`tools/render_screens.py`). Text inside them is example content, not real replies.*

### Home
![home](docs/screens/home.png)

- **Neco** with her mood sprite (idle, pleased, asleep). **Tap** her: meow and a happy face. **Hold**: purr.
- **Her words** on the right. Real idle thoughts from DeepSeek carry a `thought 00:38` label; **tap the words** to ask for one now. If she couldn't think, the reason shows in red.
- Ignore her for 90 seconds and she falls asleep. Tap to wake her.
- **Hold the top bar** to switch the screen off with a CRT power-off. Music keeps playing; tap to wake.
- Top bar: clock, SD card, Wi-Fi strength. Dock: CHAT, MUSIC, WIFI, SET.

### Chat
![chat](docs/screens/chat.png) ![keyboard](docs/screens/keyboard.png)

- **TYPE** opens the on-screen keyboard (`123` for numbers, hold `DEL` to clear). The chips send quick messages.
- Each message goes to DeepSeek with her neco-ai persona, an *about this body* section, live readings (time, Wi-Fi, light, song playing, minutes since you last wrote) and the reflections that match the topic.
- After you can read the reply she *reflects*: a second small call decides if anything is worth remembering. A toast tells you when she kept something.

### Music
![music](docs/screens/music.png)

- Every `.mp3` / `.wav` on the card, any folder, sorted by path, so albums play in order.
- `|<` `>||` `>|` · **ALL / ONE** = loop the whole card or one song (hold it to turn autoplay on boot on/off) · `-` `+` volume · hold `>||` to stop.
- Keeps playing on every screen. Keep MP3s at 128 kbps or lower.

### Setup hotspot
![setup](docs/screens/setup.png)

Shown on first boot, or from SET > MORE > *setup portal*. **SKIP** gives you the offline pet.

### Settings
![settings](docs/screens/settings.png)

Three pages, cycled with MORE:
- **BODY:** brightness (or auto), volume, cat (Neco / Coneco / Sakamoto), CRT lines and flicker, sounds.
- **LINK:** setup portal, web admin address and PIN, idle thought interval, reflection count.
- **SCREEN:** invert colour, touch sensitivity, recalibrate, motion effects (*full* or *band*, see below), UTC offset.

### CRT effects
![crt](docs/preview-crt.gif)

Scanlines, rounded tube corners, backlight flicker, power-on and power-off. Moving effects depend on the panel: if it can be read back you get the rolling band, static specks and glitch tears (*full*); if not (the dual-USB board) the rolling band still runs by redrawing the screen under it (*band*).

---

## Web admin

Once she's on your Wi-Fi: **http://neco-cyd.local** (or the IP on the LINK page). User `neco`, password = the PIN.

- change any setting, including the key (it's never shown back, only its last 4 characters)
- **about this body:** true facts she reads on every message. Add anything you want her to know. Facts work better than personality instructions.
- **reflections:** read them, forget single ones, export, forget all
- **import from neco-ai:** `python3 scripts/neco-memory.py export neco-memory.json` in your neco-ai checkout, then upload. Her Den memories come with her.

---

## How she works

```
touch / keyboard ─▶ ESP32 ─▶ HTTPS ─▶ api.deepseek.com
                     │  prompt = neco-ai persona + this body + readings + matching reflections + last 3 exchanges
                     │◀─ reply + hidden <mood:...> tag ─▶ sprite, chirp
                     └─▶ second call: consolidation JSON ─▶ /reflections.json on the board's flash
```

- **Persona:** `identity.md`, shared `values.md` and `voice.md` from neco-ai, baked in at build time.
- **Reflections:** up to 160, retrieved by word overlap plus importance and recency, like neco-ai's `store.retrieve()`. Same export format both ways.
- **Idle thoughts:** about a third get a cue from an open question or a real memory, like neco-ai's `wandering.py`. They aren't stored as memories.

### Costs and limits

- Each chat turn is up to two API calls; the prompt is roughly 3k tokens. Cheap on a Flash model, not free. Idle thoughts every 15 minutes add up; set 60 or off if you care.
- Key, Wi-Fi passwords and reflections are stored in plain text on the board's flash. Lose it, rotate the key.
- TLS to DeepSeek is encrypted but the certificate isn't verified yet.
- No voice output yet: she chirps and meows.

---

## Hardware lessons (so you don't repeat them)

These cost an evening each:

- **The touch clock (GPIO25) is also a DAC pin.** Installing the ESP32's I2S in built-in-DAC mode claims both DAC pads. Release GPIO25 explicitly (`dac_output_disable` + `rtc_gpio_deinit`) or touch reads one frozen point forever. Also, `I2S_DAC_CHANNEL_LEFT_EN` is GPIO26 and `RIGHT_EN` is GPIO25: the names are crossed.
- **DAC silence is mid-scale, not zero**, and a starved DMA replays its last buffer forever. Fill every buffer with `0x8000` when idle, or you get a whistle.
- **The amp is loud.** Volume tops out at about 35% of the DAC swing here.
- **Dual-USB panels** need `invertDisplay(true)` and a gamma re-select, or blacks look grey. They also can't be read back, so pixel-readback effects need a fallback.
- **TLS on a no-PSRAM ESP32** needs a big contiguous block of RAM. Free the prompt copies and pause the MP3 decoder before connecting.

---

## Build from source

PlatformIO (works from any editor's terminal):
```bash
pio run -t upload && pio device monitor
```
Arduino IDE: core **esp32 2.0.17** (3.x won't build), libraries TFT_eSPI 2.5.43, ESP8266Audio 1.9.7, ArduinoJson 7. TFT_eSPI needs the pins from `platformio.ini`'s build flags in its `User_Setup.h`. Board: ESP32 Dev Module, partition *Huge APP*.

Regenerate the sprites, font, sounds and persona prompts from a neco-ai checkout:
```bash
python3 tools/make_assets.py ../neco-ai
```

```
src/main.cpp      screens: boot, home, chat, music, wifi, settings, calibration
src/brain.*       persona prompt, readings, DeepSeek client, reflections
src/portal.*      setup hotspot, captive page, web admin, QR
src/fx.*  ui.*    CRT effects, drawing, keyboard, touch events
src/hw.*  net.*   display, touch, audio driver, settings, Wi-Fi
src/assets/       generated: VT323, sprites, sounds, persona text
firmware/         flashable .bin files
```

`firmware/cyd-reference-touchtest.bin` is the CYD reference repo's own touch test (plus colour inversion). Flash it to check your touch hardware independently of this firmware.

---

## Credits

- [neco-ai](https://github.com/proto6699/neco-ai): the persona, cats, memory design and Den palette this is built from.
- [ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display): pin map, touch approach, dual-USB gamma fix.
- [Gume/Braino](https://github.com/iamankushpandit/Gume): documented the DAC/touch-clock conflict and DAC idle behaviour on the dual-USB board. Code here is written independently.
- VT323 by Peter Hull (SIL OFL 1.1) · QR generator by Richard Moore (MIT) · ESP8266Audio by Earle Philhower (GPL-3.0).

The cats are fan-inspired; this project isn't affiliated with their creators.

## License

GPL-3.0 (the firmware links ESP8266Audio, which is GPL-3.0). Font and QR code keep their own licences in `LICENSES/`.
