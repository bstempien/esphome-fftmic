#include "audio_source.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome
{
  namespace inmp441_audio
  {

    static const char *const TAG = "inmp441_audio";

    // Claude change 2 for hum.
    // static bool IRAM_ATTR on_ovf(i2s_chan_handle_t, i2s_event_data_t *, void *u) {
    //   (*static_cast<volatile uint32_t *>(u))++; return false;
    // }

    void Inmp441AudioSource::setup()
    {
      i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
      channel_config.dma_desc_num = 4;
      channel_config.dma_frame_num = this->raw_samples_.size();

      esp_err_t err = i2s_new_channel(&channel_config, nullptr, &this->rx_handle_);
      if (err != ESP_OK)
      {
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

      // Claude code to show if we drop audio.
      // i2s_event_callbacks_t cbs = {}; cbs.on_recv_q_ovf = on_ovf;
      // i2s_channel_register_event_callback(this->rx_handle_, &cbs, &this->ovf_count_);

      err = i2s_channel_init_std_mode(this->rx_handle_, &standard_config);
      if (err == ESP_OK)
        err = i2s_channel_enable(this->rx_handle_);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to initialize I2S microphone: %s", esp_err_to_name(err));
        this->mark_failed();
        return;
      }

      ESP_LOGI(TAG, "INMP441 capture started at %u Hz", this->sample_rate_);
      if (xTaskCreate(&Inmp441AudioSource::capture_task_, "inmp441_capture", 4096, this, 5,
                      &this->capture_task_handle_) != pdPASS)
      {
        ESP_LOGE(TAG, "Failed to create audio capture task");
        this->mark_failed();
        err = i2s_channel_disable(this->rx_handle_);
        if (err != ESP_OK)
          ESP_LOGE(TAG, "Failed to disable I2S receive channel: %s", esp_err_to_name(err));
        err = i2s_del_channel(this->rx_handle_);
        if (err != ESP_OK)
          ESP_LOGE(TAG, "Failed to delete I2S receive channel: %s", esp_err_to_name(err));
        else
          this->rx_handle_ = nullptr;
      }
    }

    void Inmp441AudioSource::capture_task_(void *arg)
    {
      auto *source = static_cast<Inmp441AudioSource *>(arg);
      while (true)
        source->capture_loop_();
    }

    void Inmp441AudioSource::capture_loop_()
    {

      // Constants (file scope or class static)
      static constexpr float kDcR = 0.995f;                       // DC blocker pole (~12 Hz @ 16 kHz)
      static constexpr float kHighPassHz = 1800.0f;
      static constexpr float kPi = 3.14159265358979323846f;
      static constexpr float kGainDb = 30.0f;                     // +30 dB
      static const float kGainLin = powf(10.0f, kGainDb / 20.0f); // ~15.85x
      static const float kScale = kGainLin / 256.0f;              // gain + 24-bit -> 16-bit (divide by 2^8)
      const float high_pass_tan =
          tanf(kPi * kHighPassHz / static_cast<float>(this->sample_rate_));
      const float high_pass_gain = 1.0f / (1.0f + high_pass_tan);
      const float high_pass_feedback = (1.0f - high_pass_tan) / (1.0f + high_pass_tan);

      size_t bytes_read = 0;
      esp_err_t err = i2s_channel_read(this->rx_handle_, this->raw_samples_.data(),
                                       this->raw_samples_.size() * sizeof(int32_t), &bytes_read,
                                       1000);
      if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
      {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
        this->status_set_warning();
        vTaskDelay(pdMS_TO_TICKS(20));
        return;
      }

      if (bytes_read == 0) return;
      this->status_clear_warning();
      // const uint32_t overflow_count = *static_cast<volatile uint32_t *>(&this->ovf_count_);
      // if (overflow_count != this->reported_ovf_count_)
      // {
      //   this->reported_ovf_count_ = overflow_count;
      //   ESP_LOGW(TAG, "I2S receive queue overflow count %u", overflow_count);
      // }

      const size_t count = bytes_read / sizeof(int32_t);
      for (size_t i = 0; i < count; i++)
      {
        // INMP441 audio is 24-bit, MSB-aligned in the 32-bit I2S slot.
        // Arithmetic shift gives a signed 24-bit value.
        const float x = static_cast<float>(this->raw_samples_[i] >> 8);

        // Taking out the high pass filter and gain until we see how the 
        // audio sounds after the hardware adds. 
        
        // const float high_passed =
        //     high_pass_gain * (x - this->high_pass_x1_) + high_pass_feedback * this->high_pass_y1_;
        // this->high_pass_x1_ = x;
        // this->high_pass_y1_ = high_passed;

        // DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
        // const float y = high_passed - this->dc_x1_ + kDcR * this->dc_y1_;
        // this->dc_x1_ = high_passed;
        // this->dc_y1_ = y;

        // Apply gain and scale 24-bit -> 16-bit range, then clamp.
        // If we uncomment the DC blocker above, change high_passed to y in the below float s assignment.
        // float s = high_passed * kScale;
        // if (s > 32767.0f)
        //   s = 32767.0f;
        // else if (s < -32768.0f)
        //   s = -32768.0f;

        //this->pcm_samples_[i] = static_cast<int16_t>(lrintf(s));
        this->pcm_samples_[i] = static_cast<int16_t>(lrintf(x));
      }
      this->publish_audio_(this->pcm_samples_.data(), count);
    }

    void Inmp441AudioSource::publish_audio_(const int16_t *samples, size_t count)
    {
      portENTER_CRITICAL(&this->ring_mux_);
      for (size_t i = 0; i < count; i++)
      {
        this->audio_ring_[this->write_sequence_ % RING_BUFFER_SIZE] = samples[i];
        this->write_sequence_++;
      }
      portEXIT_CRITICAL(&this->ring_mux_);
    }

    bool Inmp441AudioSource::add_listener(AudioListener *listener)
    {
      if (listener == nullptr)
        return false;

      bool added = false;
      portENTER_CRITICAL(&this->ring_mux_);
      for (auto &state : this->listeners_)
      {
        if (state.listener == listener)
        {
          added = true;
          break;
        }
        if (state.listener == nullptr)
        {
          state.listener = listener;
          state.read_sequence = this->write_sequence_;
          state.overrun_count = 0;
          added = true;
          break;
        }
      }
      portEXIT_CRITICAL(&this->ring_mux_);
      if (!added)
        ESP_LOGE(TAG, "Too many audio consumers; maximum is %u", this->listeners_.size());
      return added;
    }

    size_t Inmp441AudioSource::read_audio(AudioListener *listener, int16_t *samples, size_t max_count,
                                          uint32_t *first_sample)
    {
      if (listener == nullptr || samples == nullptr || first_sample == nullptr || max_count == 0)
        return 0;

      size_t count = 0;
      uint64_t dropped_samples = 0;
      uint32_t overrun_count = 0;
      bool found = false;
      portENTER_CRITICAL(&this->ring_mux_);
      for (auto &state : this->listeners_)
      {
        if (state.listener != listener)
          continue;

        found = true;
        const uint64_t oldest_sequence =
            this->write_sequence_ > RING_BUFFER_SIZE ? this->write_sequence_ - RING_BUFFER_SIZE : 0;
        if (state.read_sequence < oldest_sequence)
        {
          dropped_samples = oldest_sequence - state.read_sequence;
          state.read_sequence = oldest_sequence;
          overrun_count = ++state.overrun_count;
        }

        count = std::min(max_count, static_cast<size_t>(this->write_sequence_ - state.read_sequence));
        *first_sample = static_cast<uint32_t>(state.read_sequence);
        for (size_t i = 0; i < count; i++)
          samples[i] = this->audio_ring_[(state.read_sequence + i) % RING_BUFFER_SIZE];
        state.read_sequence += count;
        break;
      }
      portEXIT_CRITICAL(&this->ring_mux_);

      if (!found)
        ESP_LOGE(TAG, "Audio listener attempted to read without registration");
      else if (dropped_samples > 0 && ((overrun_count & 0x3F) == 1))
        ESP_LOGW(TAG, "Audio ring buffer overrun; listener dropped %llu samples (%u overruns)",
                 static_cast<unsigned long long>(dropped_samples), overrun_count);
      return count;
    }

  } // namespace inmp441_audio
} // namespace esphome
