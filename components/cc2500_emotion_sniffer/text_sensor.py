import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CC2500Sniffer

CONF_CC2500_EMOTION_SNIFFER_ID = "cc2500_emotion_sniffer_id"
CONF_DISCOVERED_REMOTES = "discovered_remotes"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_CC2500_EMOTION_SNIFFER_ID): cv.use_id(CC2500Sniffer),
        cv.Optional(CONF_DISCOVERED_REMOTES): text_sensor.text_sensor_schema(
            icon="mdi:remote",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_CC2500_EMOTION_SNIFFER_ID])

    if CONF_DISCOVERED_REMOTES in config:
        sens = await text_sensor.new_text_sensor(config[CONF_DISCOVERED_REMOTES])
        cg.add(parent.set_remotes_text_sensor(sens))
