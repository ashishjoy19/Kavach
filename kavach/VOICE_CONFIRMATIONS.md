# Voice confirmation messages (Kavach)

**Currently disabled** to save SPIFFS (set `KAVACH_VOICE_CONFIRM` to `1` in `main/app/app_sr_handler.c` when you add SD card or have space).

The device can speak different messages depending on which command was recognised. You provide WAV files in the **spiffs** partition; the code plays the right file per command and falls back to **OK** if a specific file is missing.

## WAV files (place in `spiffs/`)

| Command           | English file            | Chinese file            | Fallback if missing |
|-------------------|-------------------------|--------------------------|---------------------|
| Any command       | `echo_en_ok.wav`        | `echo_cn_ok.wav`         | –                   |
| **Help / Alert**  | `echo_en_alerted.wav`   | `echo_cn_alerted.wav`    | `echo_*_ok.wav`     |
| **Call family**   | `echo_en_calling.wav`   | `echo_cn_calling.wav`    | `echo_*_ok.wav`     |
| **Help** (what can you do) | `echo_en_help.wav` | `echo_cn_help.wav`       | `echo_*_ok.wav`     |

Suggested phrases to record:

- **alerted**: e.g. *"Alert sent"* or *"Help alerted"*
- **calling**: e.g. *"Calling"* or *"Calling your son"*
- **help**: e.g. *"You can say: I need help, call family, turn on the light..."* (or a short *"Say: help, call family, or turn on the light"*)

## WAV format

- **Format**: WAV (PCM), 16‑bit recommended.
- **Sample rate**: 16000 Hz works well (same as SR); 22050 or 44100 are also supported.
- **Channels**: Mono or stereo; the code reads the WAV header and configures the codec.

## Adding or changing messages

1. Record or generate WAVs with the names above and put them in `kavach_demo/spiffs/`.
2. Do a **full flash** so the storage partition is updated:
   ```bash
   idf.py -p <PORT> flash
   ```
3. To add a **new** confirmation type:
   - In `app_sr_handler.c`, add a value to the `confirm_type_t` enum.
   - Add its suffix to `confirm_suffix_en` and `confirm_suffix_cn`.
   - In `sr_handler_task()`, for the command you want, call `play_confirmation_wav(CONFIRM_xxx)`.

## Help command

Saying **"Help"** or **"What can you do?"** (and the Chinese phrases) is handled as `SR_CMD_HELP`. The UI shows *"Help"* and the device plays `echo_en_help.wav` or `echo_cn_help.wav`. Use that file to speak a short list of commands so the user knows what they can say.
