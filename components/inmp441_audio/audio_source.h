#pragma once

#include "esphome/core/component.h"
#include <driver/i2s_std.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace inmp441_audio {

class AudioListener {
 public:
  virtual void on_audio(const int16_t *samples, size_t count, uint32_t first_sample) = 0;
};

class Inmp441AudioSource : public Component {
 public:
  void set_bclk_pin(uint8_t pin) { this->bclk_pin_ = pin; }
  void set_ws_pin(uint8_t pin) { this->ws_pin_ = pin; }
  void set_din_pin(uint8_t pin) { this->din_pin_ = pin; }
  void set_sample_rate(uint32_t sample_rate) { this->sample_rate_ = sample_rate; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void add_listener(AudioListener *listener);
  uint32_t sample_rate() const { return this->sample_rate_; }

 protected:
  struct BiquadFilter {
    float b0{1.0f};
    float b1{0.0f};
    float b2{0.0f};
    float a1{0.0f};
    float a2{0.0f};
    float x1{0.0f};
    float x2{0.0f};
    float y1{0.0f};
    float y2{0.0f};

    void configure(float cutoff_hz, float sample_rate, bool high_pass);
    float process(float sample);
  };

  i2s_chan_handle_t rx_handle_{nullptr};
  uint8_t bclk_pin_{0};
  uint8_t ws_pin_{0};
  uint8_t din_pin_{0};
  uint32_t sample_rate_{16000};
  uint32_t sample_index_{0};
  BiquadFilter high_pass_filter_;
  BiquadFilter low_pass_filter_;
  std::array<AudioListener *, 4> listeners_{};
  std::array<int32_t, 256> raw_samples_{};
  std::array<int16_t, 256> pcm_samples_{};
};

}  // namespace inmp441_audio
}  // namespace esphome
