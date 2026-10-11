import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]
MULTI_CONF = False

inmp441_audio_ns = cg.esphome_ns.namespace("inmp441_audio")
Inmp441AudioSource = inmp441_audio_ns.class_("Inmp441AudioSource", cg.Component)

CONF_BCLK_PIN = "bclk_pin"
CONF_WS_PIN = "ws_pin"
CONF_DIN_PIN = "din_pin"
CONF_SAMPLE_RATE = "sample_rate"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Inmp441AudioSource),
        cv.Required(CONF_BCLK_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_WS_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_DIN_PIN): pins.internal_gpio_input_pin_number,
        cv.Optional(CONF_SAMPLE_RATE, default=16000): cv.int_range(min=8000, max=48000),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    include_builtin_idf_component("esp_driver_i2s")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_bclk_pin(config[CONF_BCLK_PIN]))
    cg.add(var.set_ws_pin(config[CONF_WS_PIN]))
    cg.add(var.set_din_pin(config[CONF_DIN_PIN]))
    cg.add(var.set_sample_rate(config[CONF_SAMPLE_RATE]))
