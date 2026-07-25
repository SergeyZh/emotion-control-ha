import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import (
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_GAMMA_CORRECT,
    CONF_OUTPUT_ID,
    CONF_RESTORE_MODE,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from . import CC2500Emotion, cc2500_emotion_ns

CONF_CC2500_EMOTION_ID = "cc2500_emotion_id"
CONF_BRIGHTNESS_LEVELS = "brightness_levels"
CONF_RAMP_DURATION = "ramp_duration"

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
            # Number of discrete brightness levels the lamp has (L&S = 5).
            cv.Optional(CONF_BRIGHTNESS_LEVELS, default=5): cv.int_range(min=2, max=32),
            # Time for a full min<->max brightness ramp (remote hold), used to estimate
            # how far a received 0x10/0x20 ramp moved before 0x40 stopped it.
            cv.Optional(
                CONF_RAMP_DURATION, default="1000ms"
            ): cv.positive_time_period_milliseconds,
            # A stepped device has no smooth fade; default transitions off.
            cv.Optional(
                CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
            ): cv.positive_time_period_milliseconds,
            # We map brightness to discrete levels ourselves; gamma must stay linear
            # so the HA<->level round-trip is exact.
            cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
            # Restore on/off, brightness and color temperature across reboots (the
            # base light default is ALWAYS_OFF, which loses them).
            cv.Optional(CONF_RESTORE_MODE, default="RESTORE_DEFAULT_OFF"): cv.enum(
                light.RESTORE_MODES, upper=True, space="_"
            ),
        }
    ),
    light.validate_color_temperature_channels,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)

    parent = await cg.get_variable(config[CONF_CC2500_EMOTION_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_light(var))
    cg.add(var.set_cold_white_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_warm_white_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_brightness_levels(config[CONF_BRIGHTNESS_LEVELS]))
    cg.add(var.set_ramp_duration(config[CONF_RAMP_DURATION]))
