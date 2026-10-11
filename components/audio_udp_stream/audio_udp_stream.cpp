#include "audio_udp_stream.h"
#include "esphome/core/log.h"
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <cstring>

namespace esphome {
namespace audio_udp_stream {

static const char *const TAG = "audio_udp_stream";

struct __attribute__((packed)) PacketHeader {
  char magic[4];
  uint32_t first_sample;
};

void AudioUDPStream::setup() {
  if (this->parent_->sample_rate() != 16000) {
    ESP_LOGE(TAG, "UDP format is fixed at 16000 Hz; configure the audio source accordingly");
    this->mark_failed();
    return;
  }
  if (!this->open_socket_()) {
    this->mark_failed();
    return;
  }
  this->parent_->add_listener(this);
  ESP_LOGI(TAG, "Streaming PCM to %s:%u", this->destination_ip_.c_str(), this->port_);
}

bool AudioUDPStream::open_socket_() {
  this->socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (this->socket_ < 0) {
    ESP_LOGE(TAG, "Unable to create UDP socket");
    return false;
  }
  this->destination_.sin_family = AF_INET;
  this->destination_.sin_port = htons(this->port_);
  if (inet_aton(this->destination_ip_.c_str(), &this->destination_.sin_addr) == 0) {
    ESP_LOGE(TAG, "Invalid destination IPv4 address: %s", this->destination_ip_.c_str());
    close(this->socket_);
    this->socket_ = -1;
    return false;
  }
  return true;
}

void AudioUDPStream::on_audio(const int16_t *samples, size_t count, uint32_t first_sample) {
  if (this->is_failed())
    return;

  for (size_t i = 0; i < count; i++) {
    if (this->buffered_samples_ == 0)
      this->packet_first_sample_ = first_sample + i;
    this->packet_samples_buffer_[this->buffered_samples_++] = samples[i];
    if (this->buffered_samples_ != this->packet_samples_)
      continue;

    uint8_t packet[sizeof(PacketHeader) + sizeof(int16_t) * 1200];
    PacketHeader header{};
    memcpy(header.magic, "FFT1", sizeof(header.magic));
    header.first_sample = htonl(this->packet_first_sample_);
    memcpy(packet, &header, sizeof(header));
    memcpy(packet + sizeof(header), this->packet_samples_buffer_.data(),
           this->buffered_samples_ * sizeof(int16_t));
    const size_t packet_length = sizeof(header) + this->buffered_samples_ * sizeof(int16_t);
    const int sent = sendto(this->socket_, packet, packet_length, 0,
                            reinterpret_cast<const sockaddr *>(&this->destination_),
                            sizeof(this->destination_));
    if (sent != static_cast<int>(packet_length)) {
      this->dropped_packets_++;
      if ((this->dropped_packets_ & 0x3F) == 1)
        ESP_LOGW(TAG, "UDP send failed; dropped packets: %u", this->dropped_packets_);
    }
    this->buffered_samples_ = 0;
  }
}

}  // namespace audio_udp_stream
}  // namespace esphome
