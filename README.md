# esphome-fftmic

ESPHome external components for an ESP32-S3 and INMP441 I²S microphone:

- `audio_udp_stream` sends raw 16-bit mono PCM to a configurable UDP receiver for recording.
- `note_sequence_detector` performs a 1024-point Hann-windowed FFT every 512 samples (32 ms at 16 kHz) and publishes a binary sensor when the configured note sequence is heard.
- `inmp441_audio` is the shared I²S capture source used by both features. It initializes the microphone once and fans the samples out to its consumers.

The detector searches the 60–4000 Hz range, uses parabolic peak interpolation, and requires each configured note to remain within the selected frequency tolerance for its minimum duration. Between notes, it waits for the previous pitch to release. Detection remains on for `trigger_hold` and then automatically resets.

## Wiring

Connect the INMP441 to 3.3 V and GND. Connect `SCK` to `bclk_pin`, `WS` to `ws_pin`, and `SD` to `din_pin`. Tie `L/R` to GND to select the left I²S channel. Use GPIOs available on your particular ESP32-S3 board; configure those exact pins in YAML.

## ESPHome configuration

Replace the example pins and destination address with values for your installation. The host running `audio_tool.py record` must be reachable from the ESP32 and allow incoming UDP on the configured port.

```yaml
esp32:
  board: seeed_xiao_esp32s3
  framework:
    type: esp-idf

external_components:
  - source:
      type: git
      url: https://github.com/bstempien/esphome-fftmic
      ref: main
    components: [inmp441_audio, audio_udp_stream, note_sequence_detector]

inmp441_audio:
  id: microphone
  bclk_pin: GPIO5
  ws_pin: GPIO6
  din_pin: GPIO7
  sample_rate: 16000

audio_udp_stream:
  source_id: microphone
  destination_ip: 192.168.1.20
  port: 5005
  packet_samples: 320

binary_sensor:
  - platform: note_sequence_detector
    name: "Target audio detected"
    source_id: microphone
    frequency_tolerance: 20
    min_rms: 500
    trigger_hold: 500ms
    notes:
      - frequency: 440
        min_duration: 100ms
      - frequency: 523.3
        min_duration: 100ms
      - frequency: 659.3
        min_duration: 100ms
```

Configure normal Wi-Fi/network settings in the device YAML as usual. `frequency_tolerance` is in Hz.

## Record and analyze

Run the receiver on the computer at the destination address:

```sh
python tools/audio_tool.py record --port 5005 --output recording.wav
```

Stop recording with Ctrl-C. To produce a starting `notes:` list from the recorded audio, run:

```sh
python tools/audio_tool.py analyze recording.wav --output target_notes.yaml
```

Review the generated note boundaries and durations, then copy its `notes:` list into `note_sequence_detector`. The analyzer uses only the Python standard library and expects a 16-bit mono, 16 kHz WAV produced by the receiver. Audio frames below `--min-rms` are ignored; `--min-duration-ms` controls the minimum duration of a note event.

## UDP packet format

Each UDP datagram contains an 8-byte header followed by little-endian signed 16-bit PCM samples. The header is `FFT1` followed by the first sample index as an unsigned 32-bit network-byte-order integer. The receiver uses sample indices to restore packet order and fill detected packet gaps with silence.
