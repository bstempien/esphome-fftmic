import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.inmp441_audio import Inmp441AudioSource
from esphome.const import CONF_ID

DEPENDENCIES = ["inmp441_audio"]

audio_udp_stream_ns = cg.esphome_ns.namespace("audio_udp_stream")
AudioUDPStream = audio_udp_stream_ns.class_("AudioUDPStream", cg.Component)

CONF_SOURCE_ID = "source_id"
CONF_DESTINATION_IP = "destination_ip"
CONF_PORT = "port"
CONF_PACKET_SAMPLES = "packet_samples"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AudioUDPStream),
        cv.Required(CONF_SOURCE_ID): cv.use_id(Inmp441AudioSource),
        cv.Required(CONF_DESTINATION_IP): cv.ipv4address,
        cv.Optional(CONF_PORT, default=5005): cv.port,
        cv.Optional(CONF_PACKET_SAMPLES, default=320): cv.int_range(min=64, max=500),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], await cg.get_variable(config[CONF_SOURCE_ID]))
    await cg.register_component(var, config)
    cg.add(var.set_destination_ip(str(config[CONF_DESTINATION_IP])))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_packet_samples(config[CONF_PACKET_SAMPLES]))
