import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import (
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_OUTPUT_ID,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from . import CC2500Emotion, cc2500_emotion_ns

CONF_CC2500_EMOTION_ID = "cc2500_emotion_id"
CONF_BRIGHTNESS_STEPS = "brightness_steps"

EmotionControlLight = cc2500_emotion_ns.class_("EmotionControlLight", light.LightOutput)

CONFIG_SCHEMA = cv.All(
    light.RGB_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(EmotionControlLight),
            cv.GenerateID(CONF_CC2500_EMOTION_ID): cv.use_id(CC2500Emotion),
            cv.Optional(
                CONF_COLD_WHITE_COLOR_TEMPERATURE, default="6500 K"
            ): cv.color_temperature,
            cv.Optional(
                CONF_WARM_WHITE_COLOR_TEMPERATURE, default="2700 K"
            ): cv.color_temperature,
            # Number of relative steps that span the brightness slider (0..100%).
            cv.Optional(CONF_BRIGHTNESS_STEPS, default=10): cv.int_range(min=1, max=64),
            # A stepped device has no smooth fade; default transitions off.
            cv.Optional(
                CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
            ): cv.positive_time_period_milliseconds,
        }
    ),
    light.validate_color_temperature_channels,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)

    parent = await cg.get_variable(config[CONF_CC2500_EMOTION_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_cold_white_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_warm_white_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_brightness_steps(config[CONF_BRIGHTNESS_STEPS]))
