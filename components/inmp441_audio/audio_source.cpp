#include "audio_source.h"
#include "esphome/core/log.h"

namespace esphome {
namespace inmp441_audio {

static const char *const TAG = "inmp441_audio";

void Inmp441AudioSource::setup() {
  i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel_config.dma_desc_num = 4;
  channel_config.dma_frame_num = this->raw_samples_.size();

  esp_err_t err = i2s_new_channel(&channel_config, nullptr, &this->rx_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to allocate I2S receive channel: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  i2s_std_config_t standard_config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(this->sample_rate_),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = static_cast<gpio_num_t>(this->bclk_pin_),
              .ws = static_cast<gpio_num_t>(this->ws_pin_),
              .dout = I2S_GPIO_UNUSED,
              .din = static_cast<gpio_num_t>(this->din_pin_),
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };
  // The ESP32-S3 default slot macro selects both slots even in mono mode.
  // INMP441 L/R low selects the left slot; explicitly exclude the unused slot.
  standard_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  err = i2s_channel_init_std_mode(this->rx_handle_, &standard_config);
  if (err == ESP_OK)
    err = i2s_channel_enable(this->rx_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize I2S microphone: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "INMP441 capture started at %u Hz", this->sample_rate_);
}

void Inmp441AudioSource::loop() {
  if (this->is_failed() || this->rx_handle_ == nullptr)
    return;

  size_t bytes_read = 0;
  esp_err_t err = i2s_channel_read(this->rx_handle_, this->raw_samples_.data(),
                                   this->raw_samples_.size() * sizeof(int32_t), &bytes_read,
                                   20);
  if (err == ESP_ERR_TIMEOUT)
    return;
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();

  const size_t count = bytes_read / sizeof(int32_t);
  for (size_t i = 0; i < count; i++) {
    // INMP441 audio is 24-bit, MSB-aligned in the 32-bit I2S slot.
    this->pcm_samples_[i] = static_cast<int16_t>(__builtin_bswap32(this->raw_samples_[i]) >> 16);
  }
  const uint32_t first_sample = this->sample_index_;
  this->sample_index_ += count;
  for (auto *listener : this->listeners_) {
    if (listener != nullptr)
      listener->on_audio(this->pcm_samples_.data(), count, first_sample);
  }
}

void Inmp441AudioSource::add_listener(AudioListener *listener) {
  for (auto &registered : this->listeners_) {
    if (registered == nullptr) {
      registered = listener;
      return;
    }
  }
  ESP_LOGE(TAG, "Too many audio consumers; maximum is %u", this->listeners_.size());
}

}  // namespace inmp441_audio
}  // namespace esphome
