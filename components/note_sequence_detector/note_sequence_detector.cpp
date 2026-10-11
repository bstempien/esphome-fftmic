#include "note_sequence_detector.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>

namespace esphome {
namespace note_sequence_detector {

static const char *const TAG = "note_sequence";
static constexpr float PI = 3.14159265358979323846f;

void NoteSequenceDetector::setup() {
  if (this->parent_ == nullptr || this->parent_->sample_rate() != 16000 || this->note_count_ == 0) {
    ESP_LOGE(TAG, "A 16000 Hz audio source and at least one target note are required");
    this->mark_failed();
    return;
  }
  this->parent_->add_listener(this);
  this->publish_state(false);
}

void NoteSequenceDetector::loop() {
  if (this->detected_ && millis() - this->detected_at_ms_ >= this->trigger_hold_ms_) {
    this->detected_ = false;
    this->current_note_ = 0;
    this->matching_frames_ = 0;
    this->waiting_for_release_ = false;
    this->publish_state(false);
  }
}

void NoteSequenceDetector::on_audio(const int16_t *samples, size_t count, uint32_t) {
  if (this->is_failed() || this->detected_)
    return;
  for (size_t i = 0; i < count; i++) {
    this->samples_[this->frame_count_ % FFT_SIZE] = samples[i];
    this->frame_count_++;
    if (this->frame_count_ >= FFT_SIZE)
      this->has_full_frame_ = true;
    if (!this->has_full_frame_)
      continue;
    if (++this->samples_since_frame_ >= HOP_SIZE) {
      this->samples_since_frame_ = 0;
      this->analyze_frame_();
    }
  }
}

void NoteSequenceDetector::analyze_frame_() {
  float square_sum = 0;
  const size_t oldest = this->frame_count_ % FFT_SIZE;
  for (size_t i = 0; i < FFT_SIZE; i++) {
    const float sample = static_cast<float>(this->samples_[(oldest + i) % FFT_SIZE]);
    square_sum += sample * sample;
    const float window = 0.5f - 0.5f * cosf(2.0f * PI * i / (FFT_SIZE - 1));
    this->real_[i] = sample * window;
    this->imag_[i] = 0;
  }
  const float rms = sqrtf(square_sum / FFT_SIZE);

  for (size_t i = 1, j = 0; i < FFT_SIZE; i++) {
    size_t bit = FFT_SIZE >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(this->real_[i], this->real_[j]);
      std::swap(this->imag_[i], this->imag_[j]);
    }
  }
  for (size_t length = 2; length <= FFT_SIZE; length <<= 1) {
    const float angle = -2.0f * PI / length;
    const float step_real = cosf(angle);
    const float step_imag = sinf(angle);
    for (size_t start = 0; start < FFT_SIZE; start += length) {
      float twiddle_real = 1;
      float twiddle_imag = 0;
      for (size_t offset = 0; offset < length / 2; offset++) {
        const size_t even = start + offset;
        const size_t odd = even + length / 2;
        const float odd_real = this->real_[odd] * twiddle_real - this->imag_[odd] * twiddle_imag;
        const float odd_imag = this->real_[odd] * twiddle_imag + this->imag_[odd] * twiddle_real;
        this->real_[odd] = this->real_[even] - odd_real;
        this->imag_[odd] = this->imag_[even] - odd_imag;
        this->real_[even] += odd_real;
        this->imag_[even] += odd_imag;
        const float next_real = twiddle_real * step_real - twiddle_imag * step_imag;
        twiddle_imag = twiddle_real * step_imag + twiddle_imag * step_real;
        twiddle_real = next_real;
      }
    }
  }

  const size_t min_bin = 60 * FFT_SIZE / 16000;
  const size_t max_bin = 4000 * FFT_SIZE / 16000;
  size_t peak_bin = min_bin;
  float peak_magnitude = 0;
  for (size_t bin = min_bin; bin <= max_bin; bin++) {
    const float magnitude = this->real_[bin] * this->real_[bin] + this->imag_[bin] * this->imag_[bin];
    if (magnitude > peak_magnitude) {
      peak_magnitude = magnitude;
      peak_bin = bin;
    }
  }

  float peak_offset = 0;
  if (peak_bin > min_bin && peak_bin < max_bin) {
    const auto magnitude_at = [this](size_t bin) {
      return logf(this->real_[bin] * this->real_[bin] + this->imag_[bin] * this->imag_[bin] + 1.0e-12f);
    };
    const float left = magnitude_at(peak_bin - 1);
    const float center = magnitude_at(peak_bin);
    const float right = magnitude_at(peak_bin + 1);
    const float denominator = left - 2.0f * center + right;
    if (fabsf(denominator) > 1.0e-6f)
      peak_offset = 0.5f * (left - right) / denominator;
  }
  const float frequency = (peak_bin + peak_offset) * 16000.0f / FFT_SIZE;
  this->advance_state_(frequency, rms);
}

bool NoteSequenceDetector::matches_(float frequency, float target) const {
  return fabsf(frequency - target) <= this->frequency_tolerance_hz_;
}

void NoteSequenceDetector::advance_state_(float frequency, float rms) {
  if (this->note_count_ == 0)
    return;

  if (this->waiting_for_release_) {
    if (rms < this->min_rms_ ||
        !this->matches_(frequency, this->notes_[this->current_note_ - 1].frequency)) {
      if (++this->release_frames_ >= 1) {
        this->waiting_for_release_ = false;
        this->release_frames_ = 0;
      }
    } else {
      this->release_frames_ = 0;
    }
    return;
  }

  const auto &target = this->notes_[this->current_note_];
  if (rms >= this->min_rms_ && this->matches_(frequency, target.frequency)) {
    if (this->matching_frames_ < UINT32_MAX)
      this->matching_frames_++;
    const uint32_t required_frames =
        std::max<uint32_t>(1, target.min_duration_ms / 32 + (target.min_duration_ms % 32 != 0));
    if (this->matching_frames_ < required_frames)
      return;

    this->matching_frames_ = 0;
    if (++this->current_note_ >= this->note_count_) {
      this->detected_ = true;
      this->detected_at_ms_ = millis();
      this->publish_state(true);
      ESP_LOGI(TAG, "Target note sequence detected");
      return;
    }
    this->waiting_for_release_ = true;
    return;
  }
  this->matching_frames_ = 0;
}

}  // namespace note_sequence_detector
}  // namespace esphome
