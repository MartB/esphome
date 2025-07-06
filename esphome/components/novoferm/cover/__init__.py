import esphome.codegen as cg
from esphome.components import cover
import esphome.config_validation as cv
from esphome.const import CONF_CLOSE_DURATION, CONF_OPEN_DURATION

from .. import CONF_NOVOFERM_ID, Novoferm, novoferm_ns

DEPENDENCIES = ["novoferm"]

NovofermCover = novoferm_ns.class_("NovofermCover", cover.Cover, cg.Component)


CONFIG_SCHEMA = cv.All(
    cover.cover_schema(NovofermCover)
    .extend(
        {
            cv.GenerateID(CONF_NOVOFERM_ID): cv.use_id(Novoferm),
            cv.Optional(
                CONF_OPEN_DURATION, default="15s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_CLOSE_DURATION, default="22s"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await cover.new_cover(config)
    await cg.register_component(var, config)

    paren = await cg.get_variable(config[CONF_NOVOFERM_ID])
    cg.add(var.set_novoferm_parent(paren))
    cg.add(var.set_close_duration(config[CONF_CLOSE_DURATION]))
    cg.add(var.set_open_duration(config[CONF_OPEN_DURATION]))
