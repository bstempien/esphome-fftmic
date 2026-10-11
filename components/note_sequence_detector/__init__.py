import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.inmp441_audio import Inmp441AudioSource
from esphome.components import binary_sensor

DEPENDENCIES = ["inmp441_audio"]

note_sequence_detector_ns = cg.esphome_ns.namespace("note_sequence_detector")
NoteSequenceDetector = note_sequence_detector_ns.class_(
    "NoteSequenceDetector", binary_sensor.BinarySensor, cg.Component
)
CONF_SOURCE_ID = "source_id"
CONF_NOTES = "notes"
CONF_FREQUENCY = "frequency"
CONF_MIN_DURATION = "min_duration"
CONF_FREQUENCY_TOLERANCE = "frequency_tolerance"
CONF_MIN_RMS = "min_rms"
CONF_TRIGGER_HOLD = "trigger_hold"

NOTE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_FREQUENCY): cv.float_range(min=40, max=4000),
        cv.Optional(CONF_MIN_DURATION, default="80ms"): cv.positive_time_period_milliseconds,
    }
)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(NoteSequenceDetector).extend(
    {
        cv.Required(CONF_SOURCE_ID): cv.use_id(Inmp441AudioSource),
        cv.Required(CONF_NOTES): cv.All(cv.ensure_list(NOTE_SCHEMA), cv.Length(min=1, max=32)),
        cv.Optional(CONF_FREQUENCY_TOLERANCE, default=20.0): cv.float_range(min=1, max=200),
        cv.Optional(CONF_MIN_RMS, default=500): cv.int_range(min=1, max=32767),
        cv.Optional(CONF_TRIGGER_HOLD, default="500ms"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)
    cg.add(var.set_parent(await cg.get_variable(config[CONF_SOURCE_ID])))
    cg.add(var.set_frequency_tolerance(config[CONF_FREQUENCY_TOLERANCE]))
    cg.add(var.set_min_rms(config[CONF_MIN_RMS]))
    cg.add(var.set_trigger_hold(config[CONF_TRIGGER_HOLD].total_milliseconds))
    cg.add(var.set_note_count(len(config[CONF_NOTES])))
    for index, note in enumerate(config[CONF_NOTES]):
        cg.add(var.set_note(index, note[CONF_FREQUENCY], note[CONF_MIN_DURATION].total_milliseconds))
