#pragma once

#include "../inmp441_audio/audio_source.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace note_sequence_detector {

class NoteSequenceDetector : public binary_sensor::BinarySensor,
                             public Component,
                             public inmp441_audio::AudioListener {
 public:
  void set_parent(inmp441_audio::Inmp441AudioSource *parent) { this->parent_ = parent; }
  void set_frequency_tolerance(float hz) { this->frequency_tolerance_hz_ = hz; }
  void set_min_rms(uint16_t min_rms) { this->min_rms_ = min_rms; }
  void set_trigger_hold(uint32_t milliseconds) { this->trigger_hold_ms_ = milliseconds; }
  void set_note_count(uint8_t count) { this->note_count_ = count; }
  void set_note(uint8_t index, float frequency, uint32_t min_duration_ms) {
    if (index < this->notes_.size())
      this->notes_[index] = {frequency, min_duration_ms};
  }

  void setup() override;
  void loop() override;
  void on_audio(const int16_t *samples, size_t count, uint32_t first_sample) override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  struct NoteTarget {
    float frequency{0};
    uint32_t min_duration_ms{80};
  };

  void analyze_frame_();
  void advance_state_(float frequency, float rms);
  bool matches_(float frequency, float target) const;

  static constexpr size_t FFT_SIZE = 1024;
  static constexpr size_t HOP_SIZE = 512;
  inmp441_audio::Inmp441AudioSource *parent_{nullptr};
  std::array<NoteTarget, 32> notes_{};
  std::array<int16_t, FFT_SIZE> samples_{};
  std::array<float, FFT_SIZE> real_{};
  std::array<float, FFT_SIZE> imag_{};
  uint8_t note_count_{0};
  uint8_t current_note_{0};
  uint16_t min_rms_{500};
  float frequency_tolerance_hz_{20.0f};
  uint32_t trigger_hold_ms_{500};
  uint32_t frame_count_{0};
  uint16_t samples_since_frame_{0};
  uint32_t matching_frames_{0};
  uint16_t release_frames_{0};
  bool has_full_frame_{false};
  bool waiting_for_release_{false};
  bool detected_{false};
  uint32_t detected_at_ms_{0};
};

}  // namespace note_sequence_detector
}  // namespace esphome
