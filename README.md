# XIAO_OpenAI_complete_voice_and_vision_assistant
This device uses the XIAO ESP32 S3 sense with OpenAI to make a feature rich AI assistant


# XIAO ESP32S3 AI Voice & Vision Assistant

A fully offline-triggered, cloud-powered AI assistant built on the **Seeed Studio XIAO ESP32S3 Sense**. Press a button, ask a question or point the camera at something — the assistant listens, thinks, and speaks back. No app, no phone, no screen required.

---

## What It Does

- **Voice mode** — Hold D2, speak your question, release. The assistant transcribes your speech, sends it to GPT-5.4-mini with live web search enabled, and speaks the answer back through the speaker using Google TTS.
- **Vision mode** — Hold D3, speak your question about what the camera sees, release. The assistant captures an image, transcribes your speech, and sends both to GPT-5.4-mini for analysis.
- **Persistent conversation** — Both voice and vision maintain independent conversation histories stored server-side by OpenAI via `previous_response_id`. History survives device restarts because the thread IDs are saved to flash memory.
- **Web search** — Voice mode has automatic web search built in. Ask about today's weather, current news, sports scores, or anything time-sensitive — the model searches before answering.
- **WiFi config portal** — On first boot (or after factory reset), the device creates a WiFi hotspot. Connect to it from your phone or laptop to configure everything through a browser.

---

## Hardware Required

| Component | Notes |
|---|---|
| Seeed Studio XIAO ESP32S3 Sense | With the camera expansion board |
| MAX98357A I2S amplifier | For audio output |
| Speaker | 4Ω or 8Ω, connected to MAX98357A |
| 6× momentary push buttons | One for each function pin |
| Breadboard + wires | For connections |

### Pin Connections

| Function | XIAO Pin | Notes |
|---|---|---|
| Voice button (hold to record) | D2 | Pull-up, active LOW |
| Vision button (hold to record) | D3 | Pull-up, active LOW |
| Volume cycle button | D0 | Press to step volume up (0→3→...→21→0) |
| Config portal button | D10 | Hold 3 seconds to enter config mode |
| New voice chat button | D8 | Press to discard voice history and start fresh |
| New vision chat button | D9 | Press to discard vision history and start fresh |
| MAX98357A BCLK | D6 (GPIO 6) | I2S clock |
| MAX98357A LRC | D5 (GPIO 5) | I2S word select |
| MAX98357A DIN | D2 (GPIO 2) | I2S data |
| PDM Microphone CLK | GPIO 42 | Built-in on Sense board |
| PDM Microphone DATA | GPIO 41 | Built-in on Sense board |

> **Factory Reset:** Hold D2 and D3 simultaneously while powering on (or while pressing the reset button) to wipe all saved configuration. The device will restart into config portal mode.

---

## Software & Dependencies

### Arduino IDE Setup
- Board: **XIAO ESP32S3** (via Espressif ESP32 board package)
- Partition scheme: **Default with spiffs (3MB APP/1.5MB SPIFFS)**

### Required Libraries
Install all from Arduino Library Manager or GitHub:

| Library | Source |
|---|---|
| `ESP32-audioI2S` | [schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) |
| `ArduinoJson` | Library Manager — Benoit Blanchon |
| `base64` | Library Manager |

The following are included with the ESP32 Arduino core and need no separate installation: `WiFi`, `WiFiClientSecure`, `HTTPClient`, `Preferences`, `WebServer`, `DNSServer`.

### Files Needed
Your sketch folder must contain these files:
```
your_sketch/
├── your_sketch.ino       ← main code
├── config_portal.h       ← WiFi configuration portal UI
└── camera_pins.h         ← XIAO ESP32S3 Sense camera pin definitions
```

`camera_pins.h` is available from Seeed's official Arduino examples for the XIAO ESP32S3 Sense.

---

## API Keys & Accounts

You need one account:

**OpenAI** — [platform.openai.com](https://platform.openai.com)
- Used for: Speech-to-text (Whisper-1), language model (GPT-5.4-mini), and web search
- Get an API key from the API keys section of your account dashboard
- Billing must be enabled — this project uses pay-per-use API calls

You do **not** need a separate Google account or any TTS API key. Google TTS is accessed for free via the ESP32-audioI2S library's `connecttospeech()` method, which uses Google's public TTS endpoint.

---

## First Time Setup

1. Flash the firmware to your XIAO ESP32S3 Sense.
2. On first boot, the device creates a WiFi hotspot named **`XIAO-AI-Setup-XXXXXX`** (where XXXXXX is part of the MAC address).
3. Connect to that network from your phone or laptop. Password: **`12345678`**
4. A configuration page should open automatically (captive portal). If it doesn't, open a browser and go to **`192.168.4.1`**
5. Fill in:
   - Your WiFi network name and password
   - Your OpenAI API key
   - Assistant name (e.g. Jarvis, Friday, Max — whatever you like)
   - AI role/personality (e.g. "You are a friendly assistant who loves science")
   - Voice language for TTS and STT
6. Click **Save Configuration**. The device restarts and connects to your WiFi.

---

## How to Use

### Voice Mode (D2)
1. Hold **D2** and start speaking your question.
2. Release **D2** when done.
3. Wait — the assistant transcribes your speech (Whisper), queries GPT-5.4-mini (with web search if needed), and speaks the answer.

For anything requiring current information — "What's the weather in Mumbai?", "Who won yesterday's match?", "What's the price of Bitcoin?" — web search activates automatically. No need to phrase it differently.

### Vision Mode (D3)
1. Point the camera at what you want to analyze.
2. Hold **D3** and speak your question about it — "What is this?", "Read the text on this label", "How many calories is this food?", "What plant is this?"
3. Release **D3** when done.
4. The assistant captures the image, transcribes your question, and responds.

### Volume (D0)
Press **D0** to step the volume up. It cycles: 0 → 3 → 6 → 9 → 12 → 15 → 18 → 21 → 0. The current level is announced aloud.

### New Voice Chat (D8)
Press **D8** to discard the current voice conversation thread and start a completely fresh one. The vision conversation is unaffected. Useful when you want to start a new topic without the model having context from the previous conversation.

### New Vision Chat (D9)
Press **D9** to discard the current vision conversation thread and start fresh. The voice conversation is unaffected.

### Config Mode (D10)
Hold **D10 for 3 seconds** to re-enter the configuration portal. The device disconnects from WiFi, creates the hotspot again, and you can update any settings.

---

## Conversation Memory

Both voice and vision maintain separate persistent conversation histories:

- History is stored **server-side by OpenAI** using the Responses API `previous_response_id` mechanism — the full conversation lives on OpenAI's servers, not on the device.
- The thread IDs are saved to **flash memory** on the ESP32, so history survives device restarts and power cycles.
- If a thread expires on OpenAI's servers (after a long period of inactivity), the device detects this automatically, clears the stale ID, and seamlessly starts a new thread without throwing an error.
- Press **D8** or **D9** at any time to intentionally start a fresh conversation.

---

## Configuration Portal Details

The portal is accessible at `http://192.168.4.1` while connected to the device's hotspot. It lets you configure:

| Setting | Description |
|---|---|
| WiFi SSID | Your home or office network name |
| WiFi Password | Leave blank to keep existing password |
| OpenAI API Key | Your `sk-...` key — leave blank to keep existing |
| Assistant Name | What the AI calls itself |
| AI Role | System prompt / personality description (up to 1500 characters) |
| Voice Language | Language for both TTS output and STT recognition |

The portal also shows the current configuration status and any recent errors at the top of the page.

---

## Supported Languages

The following languages are available for voice input and output:

English (UK), English (US), English (India), Spanish (Spain), Spanish (Mexico), French, German, Italian, Portuguese (Brazil), Portuguese (Portugal), Russian, Japanese, Korean, Chinese Simplified, Chinese Traditional, Arabic, Hindi, Dutch, Polish, Swedish, Turkish, Bengali, Gujarati, Kannada, Malayalam, Marathi, Punjabi, Tamil, Telugu.

---

## Troubleshooting

**No audio / silent speaker**
- Check MAX98357A wiring — BCLK to D6, LRC to D5, DIN to D2.
- Make sure speaker is connected to MAX98357A output terminals.
- Check volume is not at 0 — press D0 to cycle through levels.

**Whisper returns empty transcription**
- Speak clearly and close to the microphone.
- The PDM microphone is built into the Sense expansion board — make sure it is attached.
- Try holding the button longer to give more audio for Whisper to work with.

**"No configuration found" on every boot**
- The flash was cleared. Go through first time setup again.

**Vision API error 400**
- Usually a payload formatting issue. Make sure `camera_pins.h` defines `CAMERA_MODEL_XIAO_ESP32S3` correctly.

**WiFi keeps dropping**
- Move closer to the router.
- The device will attempt to reconnect automatically before each API call.

**Config portal doesn't open automatically**
- Manually navigate to `http://192.168.4.1` in your browser while connected to the `XIAO-AI-Setup` network.

---

## Project Structure

```
├── main.ino                  Main sketch — all logic, API calls, button handling
├── config_portal.h           WiFi captive portal — HTML UI, web server, save handler
└── camera_pins.h             Camera GPIO definitions for XIAO ESP32S3 Sense
```

---

## Cost Estimate

All AI processing is pay-per-use via the OpenAI API. Approximate costs per interaction (as of 2025):

| Call | Model | Typical cost |
|---|---|---|
| Speech to text | Whisper-1 | ~$0.003 per minute of audio |
| Voice/Vision response | GPT-5.4-mini | ~$0.001–0.003 per exchange |
| Web search | Built into GPT-5.4-mini | Included in response cost |

A typical 10-second question and short answer costs roughly **$0.005 to $0.01** total. Google TTS for the spoken response is free.

---

## License

MIT License — free to use, modify, and distribute. Attribution appreciated but not required.

---

## Credits

- [schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) — excellent audio library enabling both Google TTS playback and I2S microphone recording on the ESP32
- [Seeed Studio](https://www.seeedstudio.com) — XIAO ESP32S3 Sense hardware
- [OpenAI](https://openai.com) — Whisper STT and GPT-5.4-mini via Responses API
