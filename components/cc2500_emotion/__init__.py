import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_ID

CODEOWNERS = ["@sergeyzh"]
DEPENDENCIES = ["spi"]
AUTO_LOAD = ["light"]
MULTI_CONF = True

CONF_GDO0_PIN = "gdo0_pin"
CONF_REMOTE_ID = "remote_id"

cc2500_emotion_ns = cg.esphome_ns.namespace("cc2500_emotion")
CC2500Emotion = cc2500_emotion_ns.class_(
    "CC2500Emotion", cg.Component, spi.SPIDevice
)


def _validate_remote_id(value):
    """Accept a 4-byte remote ID as a hex string, e.g. '68049F25' or '68:04:9F:25'."""
    value = cv.string_strict(value)
    cleaned = value.replace(" ", "").replace(":", "")
    if len(cleaned) != 8:
        raise cv.Invalid(
            "remote_id must be 4 bytes (8 hex digits), e.g. '68049F25'"
        )
    try:
        data = bytes.fromhex(cleaned)
    except ValueError as err:
        raise cv.Invalid("remote_id must be hexadecimal") from err
    return list(data)


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CC2500Emotion),
            cv.Optional(CONF_GDO0_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_REMOTE_ID, default="68049F25"): _validate_remote_id,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    if CONF_GDO0_PIN in config:
        gdo0 = await cg.gpio_pin_expression(config[CONF_GDO0_PIN])
        cg.add(var.set_gdo0_pin(gdo0))

    rid = config[CONF_REMOTE_ID]
    cg.add(var.set_remote_id(rid[0], rid[1], rid[2], rid[3]))
