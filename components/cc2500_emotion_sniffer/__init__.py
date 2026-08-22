import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID

from ..cc2500_emotion import CC2500Emotion

CODEOWNERS = ["@sergeyzh"]
DEPENDENCIES = ["cc2500_emotion"]
# ESPHome compiles every .h in the component, so the text_sensor headers must be
# present even when no text_sensor is configured.
AUTO_LOAD = ["text_sensor"]

CONF_CC2500_EMOTION_ID = "cc2500_emotion_id"
CONF_DUMP_RAW = "dump_raw"
CONF_ON_FRAME = "on_frame"

cc2500_emotion_sniffer_ns = cg.esphome_ns.namespace("cc2500_emotion_sniffer")
CC2500Sniffer = cc2500_emotion_sniffer_ns.class_("CC2500Sniffer", cg.Component)
SnifferFrameTrigger = cc2500_emotion_sniffer_ns.class_(
    "SnifferFrameTrigger",
    automation.Trigger.template(
        cg.std_string, cg.uint8, cg.std_string, cg.uint8, cg.int_, cg.uint8
    ),
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CC2500Sniffer),
        cv.GenerateID(CONF_CC2500_EMOTION_ID): cv.use_id(CC2500Emotion),
        # Log the full 8- or 14-byte frame for every command, not just unknown ones.
        cv.Optional(CONF_DUMP_RAW, default=False): cv.boolean,
        # Fires once per press, for any remote in range.
        cv.Optional(CONF_ON_FRAME): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(SnifferFrameTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_CC2500_EMOTION_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_dump_raw(config[CONF_DUMP_RAW]))

    for conf in config.get(CONF_ON_FRAME, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [
                (cg.std_string, "remote_id"),
                (cg.uint8, "command"),
                (cg.std_string, "command_name"),
                (cg.uint8, "value"),
                (cg.int_, "rssi"),
                (cg.uint8, "lqi"),
            ],
            conf,
        )
