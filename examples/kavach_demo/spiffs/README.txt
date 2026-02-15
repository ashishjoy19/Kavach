Voice feedback WAV files for Kavach
===================================

Put your WAV files HERE in this folder (spiffs/). They are flashed to the
device and appear at /spiffs/ when the app runs.

Required (for audio feedback):
  - beep.wav       Short beep when you say "Alexa" (wake word)
  - echo_en_ok.wav Default "OK" after any command (e.g. "Send alert", "Turn on light")

Optional (per-command responses):
  - echo_en_alerted.wav  When you say "Send alert"
  - echo_en_calling.wav  When you say "Call family"
  - echo_en_help.wav     When you say "Help"
  (For Chinese: echo_cn_ok.wav, echo_cn_alerted.wav, etc.)

  - gas_alarm.wav       Played when gas leak is detected (with the on-screen warning).

Format: MUST be 16 kHz, 16-bit WAV (mono or stereo). Other sample rates (e.g. 44.1 kHz) will be skipped to avoid crashing the voice pipeline.

After adding or changing files, build and flash again:
  idf.py build flash

---
If your board has an SD card slot (and the BSP supports it), you can
instead put the same files on the PHYSICAL SD CARD (root of the card,
not inside a folder named "sd card"). The app will look at /sdcard/ first,
then /spiffs/, then /storage/.
