#pragma once

#include "../inmp441_audio/audio_source.h"
#include "esphome/core/component.h"
#include <lwip/sockets.h>
#include <array>
#include <cstdint>
#include <string>

namespace esphome {
namespace audio_udp_stream {

class AudioUDPStream : public Component, public inmp441_audio::AudioListener {
 public:
  explicit AudioUDPStream(inmp441_audio::Inmp441AudioSource *parent) : parent_(parent) {}
  void set_destination_ip(const std::string &ip) { this->destination_ip_ = ip; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_packet_samples(uint16_t count) { this->packet_samples_ = count; }
  void setup() override;
  void on_audio(const int16_t *samples, size_t count, uint32_t first_sample) override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  bool open_socket_();
  inmp441_audio::Inmp441AudioSource *parent_;
  std::string destination_ip_;
  uint16_t port_{5005};
  uint16_t packet_samples_{320};
  int socket_{-1};
  sockaddr_in destination_{};
  std::array<int16_t, 1200> packet_samples_buffer_{};
  size_t buffered_samples_{0};
  uint32_t packet_first_sample_{0};
  uint32_t dropped_packets_{0};
};

}  // namespace audio_udp_stream
}  // namespace esphome
