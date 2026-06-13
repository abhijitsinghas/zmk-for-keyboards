#!/usr/bin/env python3
"""Apply local no-fork OLED/ext-power ZMK patches.

This script intentionally uses exact text replacement instead of `patch` so the
GitHub Actions build fails with a clear message if upstream source drifts.
"""

from pathlib import Path
import sys

ROOT = Path.cwd()


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        print(f"ERROR: expected exactly one match in {path}, found {count}", file=sys.stderr)
        sys.exit(1)
    path.write_text(text.replace(old, new, 1))
    print(f"patched {path}")


ssd1306 = ROOT / "zephyr" / "drivers" / "display" / "ssd1306.c"
ext_power = ROOT / "zmk" / "app" / "src" / "ext_power_generic.c"

ssd1306_old = '''static int ssd1306_resume(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t cmd_buf[] = {
		SSD1306_DISPLAY_ON,
	};

	/* Turn on supply if pin connected */
	if (config->supply.port) {
		gpio_pin_set_dt(&config->supply, 1);
		k_sleep(K_MSEC(SSD1306_SUPPLY_DELAY));
	}

	return ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
}
'''

ssd1306_new = '''static int ssd1306_set_contrast(const struct device *dev, const uint8_t contrast);

static int ssd1306_resume(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t cmd_buf[] = {
		SSD1306_SET_ENTIRE_DISPLAY_OFF,
		(config->color_inversion ? SSD1306_SET_REVERSE_DISPLAY
					 : SSD1306_SET_NORMAL_DISPLAY),
	};
	uint8_t on_cmd[] = {
		SSD1306_DISPLAY_ON,
	};

	/* Turn on supply if pin connected */
	if (config->supply.port) {
		gpio_pin_set_dt(&config->supply, 1);
		k_sleep(K_MSEC(SSD1306_SUPPLY_DELAY));
	}

	/*
	 * External power can be cut while the MCU remains running. In that
	 * case the SSD1306 loses its controller register state, and a simple
	 * DISPLAY_ON command is not enough to bring the panel back. Re-send
	 * the software init sequence without toggling the hardware reset pin.
	 */
#if (DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(solomon_ssd1306fb, i2c) || \\
	DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(solomon_ssd1309fb, i2c) || \\
	DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(sinowealth_sh1106, i2c))
	if (config->write_bus == ssd1306_write_bus_i2c) {
		(void)i2c_recover_bus(config->bus.i2c.bus);
	}
#endif

	if (ssd1306_set_timing_setting(dev)) {
		return -EIO;
	}

	if (ssd1306_set_hardware_config(dev)) {
		return -EIO;
	}

	if (ssd1306_set_panel_orientation(dev)) {
		return -EIO;
	}

	if (!config->ssd1309_compatible) {
		if (ssd1306_set_charge_pump(dev)) {
			return -EIO;
		}

		if (ssd1306_set_iref_mode(dev)) {
			return -EIO;
		}
	}

	if (ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true)) {
		return -EIO;
	}

	if (ssd1306_set_contrast(dev, CONFIG_SSD1306_DEFAULT_CONTRAST)) {
		return -EIO;
	}

	return ssd1306_write_bus(dev, on_cmd, sizeof(on_cmd), true);
}
'''

replace_once(ssd1306, ssd1306_old, ssd1306_new)

include_old = '''#include <zephyr/drivers/gpio.h>
'''
include_new = '''#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
'''
replace_once(ext_power, include_old, include_new)

ext_power_old = '''    data->status = true;
    return ext_power_save_state();
}
'''

ext_power_new = '''    data->status = true;

#if DT_HAS_CHOSEN(zephyr_display)
    /*
     * If external power also supplies the display, restoring power leaves
     * OLED controllers powered but uninitialized. Wait briefly for VCC to
     * settle, then unblank the display so its driver can re-send init.
     */
    k_msleep(config->init_delay_ms ? config->init_delay_ms : 10);

    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (device_is_ready(display)) {
        int ret = display_blanking_off(display);
        if (ret < 0) {
            LOG_WRN("Failed to resume display after ext-power enable: %d", ret);
        }
    }
#endif

    return ext_power_save_state();
}
'''
replace_once(ext_power, ext_power_old, ext_power_new)

print("OLED/ext-power patches applied successfully")
