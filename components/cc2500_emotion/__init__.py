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
CONF_RECEIVE = "receive"
CONF_STEP_INTERVAL = "step_interval"

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
            # No default on purpose: an ID belongs to a specific remote, and a
            # shipped default would make every device transmit as someone else's.
            # Without it the hub only listens (see cc2500_emotion_sniffer).
            cv.Optional(CONF_REMOTE_ID): _validate_remote_id,
            cv.Optional(CONF_RECEIVE, default=True): cv.boolean,
            # Pause between consecutive brightness step transmissions, so the lamp
            # registers each as a distinct step instead of collapsing them.
            cv.Optional(
                CONF_STEP_INTERVAL, default="200ms"
            ): cv.positive_time_period_milliseconds,
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

    if CONF_REMOTE_ID in config:
        rid = config[CONF_REMOTE_ID]
        cg.add(var.set_remote_id(rid[0], rid[1], rid[2], rid[3]))

    cg.add(var.set_receive(config[CONF_RECEIVE]))
    cg.add(var.set_step_interval(config[CONF_STEP_INTERVAL]))
