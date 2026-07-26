import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..cc2500_emotion import CC2500Emotion

CODEOWNERS = ["@sergeyzh"]
DEPENDENCIES = ["cc2500_emotion"]
# ESPHome compiles every .h in the component, so the text_sensor headers must be
# present even when no text_sensor is configured.
AUTO_LOAD = ["text_sensor"]

CONF_CC2500_EMOTION_ID = "cc2500_emotion_id"
CONF_DUMP_RAW = "dump_raw"

cc2500_emotion_sniffer_ns = cg.esphome_ns.namespace("cc2500_emotion_sniffer")
CC2500Sniffer = cc2500_emotion_sniffer_ns.class_("CC2500Sniffer", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CC2500Sniffer),
        cv.GenerateID(CONF_CC2500_EMOTION_ID): cv.use_id(CC2500Emotion),
        # Log the full 14-byte frame for every command, not just unknown ones.
        cv.Optional(CONF_DUMP_RAW, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_CC2500_EMOTION_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_dump_raw(config[CONF_DUMP_RAW]))
