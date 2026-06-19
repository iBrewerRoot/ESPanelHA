/* GENERATED, do not edit by hand — see scripts/mdi-font. */
#include "mdi_icons.h"

#include <string.h>

namespace {
struct MdiEntry { const char *name; const char *glyph; };
const MdiEntry kIcons[] = {
    {"lightbulb", MDI_LIGHTBULB},
    {"lightbulb-outline", MDI_LIGHTBULB_OUTLINE},
    {"lightbulb-group", MDI_LIGHTBULB_GROUP},
    {"ceiling-light", MDI_CEILING_LIGHT},
    {"floor-lamp", MDI_FLOOR_LAMP},
    {"lamp", MDI_LAMP},
    {"led-strip-variant", MDI_LED_STRIP_VARIANT},
    {"string-lights", MDI_STRING_LIGHTS},
    {"track-light", MDI_TRACK_LIGHT},
    {"power-socket-eu", MDI_POWER_SOCKET_EU},
    {"power-plug", MDI_POWER_PLUG},
    {"power-plug-outline", MDI_POWER_PLUG_OUTLINE},
    {"toggle-switch-variant", MDI_TOGGLE_SWITCH_VARIANT},
    {"toggle-switch-variant-off", MDI_TOGGLE_SWITCH_VARIANT_OFF},
    {"light-switch", MDI_LIGHT_SWITCH},
    {"power", MDI_POWER},
    {"thermometer", MDI_THERMOMETER},
    {"water-percent", MDI_WATER_PERCENT},
    {"gauge", MDI_GAUGE},
    {"lightning-bolt", MDI_LIGHTNING_BOLT},
    {"flash", MDI_FLASH},
    {"weather-partly-cloudy", MDI_WEATHER_PARTLY_CLOUDY},
    {"motion-sensor", MDI_MOTION_SENSOR},
    {"door", MDI_DOOR},
    {"window-closed-variant", MDI_WINDOW_CLOSED_VARIANT},
    {"help-circle-outline", MDI_HELP_CIRCLE_OUTLINE},
    {"alert-circle-outline", MDI_ALERT_CIRCLE_OUTLINE},
};
} // namespace

const char *mdiGlyph(const char *name) {
    if (!name) return nullptr;
    if (strncmp(name, "mdi:", 4) == 0) name += 4; // tolerate HA's "mdi:" prefix
    for (const auto &e : kIcons) {
        if (strcmp(e.name, name) == 0) return e.glyph;
    }
    return nullptr;
}
