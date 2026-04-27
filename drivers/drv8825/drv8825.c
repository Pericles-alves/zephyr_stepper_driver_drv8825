/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Navimatix GmbH
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_drv8825

#include <zephyr/drivers/stepper/stepper.h>
#include <zephyr/drivers/gpio.h>
#include "drv8825.h"
#include <step_dir_stepper_common.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(drv8825, CONFIG_DRV8825_LOG_LEVEL);

/* Enable and wake up times of the drv8825 stepper controller family. Only after they have elapsed
 * are controller output signals guaranteed to be valid.
 */
#define DRV8825_ENABLE_TIME  K_USEC(5)
#define DRV8825_WAKE_UP_TIME K_USEC(1200)

/**
 * @brief DRV8825 stepper driver configuration data.
 *
 * This structure contains all the devicetree specifications for the pins
 * needed by a given DRV8825 stepper driver.
 */
struct drv8825_config {
	struct step_dir_stepper_common_config common;
	struct gpio_dt_spec sleep_pin;
	struct gpio_dt_spec fault_pin;
    struct gpio_dt_spec reset_pin;
	struct gpio_dt_spec m2_pin;
};

/* Struct for storing the states of output pins. */
struct drv8825_pin_states {
	uint8_t sleep: 1;
	uint8_t en: 1;
	uint8_t m0: 2;
	uint8_t m1: 2;
    uint8_t m2: 2;
};

/**
 * @brief DRV8825 stepper driver data.
 */
struct drv8825_data {
	const struct device *dev;
	struct drv8825_pin_states pin_states;
	enum stepper_micro_step_resolution ustep_res;
	struct gpio_callback fault_cb_data;
	stepper_event_cb_t fault_cb;
	void *fault_cb_user_data;
};

STEP_DIR_STEPPER_STRUCT_CHECK(struct drv8825_config);

static int drv8825_set_microstep_pin(const struct device *dev, const struct gpio_dt_spec *pin,
				     int value)
{
	int ret;

	/* Reset microstep pin as it may have been disconnected. */
	ret = gpio_pin_configure_dt(pin, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("%s: Failed to reset micro-step pin (error: %d)", dev->name, ret);
		return ret;
	}

	/* Set microstep pin */
	switch (value) {
	case 0:
		ret = gpio_pin_set_dt(pin, 0);
		break;
	case 1:
		ret = gpio_pin_set_dt(pin, 1);
		break;
	default:
		break;
	}

	if (ret != 0) {
		LOG_ERR("%s: Failed to set micro-step pin (error: %d)", dev->name, ret);
		return ret;
	}
	return 0;
}

/*
 * If microstep setter fails, attempt to recover into previous state.
 */
int drv8825_microstep_recovery(const struct device *dev)
{
	const struct drv8825_config *config = dev->config;
	struct drv8825_data *data = dev->data;
	int ret;

	uint8_t m0_value = data->pin_states.m0;
	uint8_t m1_value = data->pin_states.m1;

	ret = drv8825_set_microstep_pin(dev, &config->common.m0_pin, m0_value);
	if (ret != 0) {
		LOG_ERR("%s: Failed to restore microstep configuration (error: %d)", dev->name,
			ret);
		return ret;
	}

	ret = drv8825_set_microstep_pin(dev, &config->common.m1_pin, m1_value);
	if (ret != 0) {
		LOG_ERR("%s: Failed to restore microstep configuration (error: %d)", dev->name,
			ret);
		return ret;
	}

    ret = drv8825_set_microstep_pin(dev, &config->m2_pin, m2_value);
	if (ret != 0) {
		LOG_ERR("%s: Failed to restore microstep configuration (error: %d)", dev->name,
			ret);
		return ret;
	}

	return 0;
}

static int drv8825_check_en_sleep_reset_pin(const struct drv8825_config *config)
{
	bool has_sleep_pin = config->sleep_pin.port != NULL;
	bool has_enable_pin = config->common.en_pin.port != NULL;
    bool has_reset_pin = config->reset_pin.port != NULL;

	if (!has_sleep_pin && !has_enable_pin && !has_reset_pin) {
		LOG_ERR("Failed to enable/disable device, neither sleep pin nor enable pin nor reset pin are "
			"available. The device is always on.");
		return -ENOTSUP;
	}

	return 0;
}

static int drv8825_set_en_pin_state(const struct device *dev, bool enable)
{
	const struct drv8825_config *config = dev->config;
	struct drv8825_data *data = dev->data;
	bool has_enable_pin = config->common.en_pin.port != NULL;
	int ret;

	if (has_enable_pin) {
		ret = gpio_pin_set_dt(&config->common.en_pin, enable);
		if (ret != 0) {
			LOG_ERR("%s: Failed to set en_pin (error: %d)", dev->name, ret);
			return ret;
		}
		data->pin_states.en = enable ? 1U : 0U;
	}

	return 0;
}

static int drv8825_set_sleep_pin_state(const struct device *dev, bool enable)
{
	int ret;
	const struct drv8825_config *config = dev->config;
	struct drv8825_data *data = dev->data;
	bool has_sleep_pin = config->sleep_pin.port != NULL;

	if (has_sleep_pin) {
		ret = gpio_pin_set_dt(&config->sleep_pin, !enable);
		if (ret != 0) {
			LOG_ERR("%s: Failed to set sleep_pin (error: %d)", dev->name, ret);
			return ret;
		}
		data->pin_states.sleep = enable ? 0U : 1U;
	}

	return 0;
}

static int drv8825_set_reset_pin_state(const struct device *dev, bool enable)
{
	int ret;
	const struct drv8825_config *config = dev->config;
	struct drv8825_data *data = dev->data;
	bool has_reset_pin = config->reset_pin.port != NULL;

	if (has_reset_pin) {
		ret = gpio_pin_set_dt(&config->reset_pin, !enable);
		if (ret != 0) {
			LOG_ERR("%s: Failed to set reset_pin (error: %d)", dev->name, ret);
			return ret;
		}
		data->pin_states.reset = enable ? 0U : 1U;
	}

	return 0;
}


static int drv8825_enable(const struct device *dev)
{
	const struct drv8825_config *config = dev->config;
	struct drv8825_data *data = dev->data;
	bool has_enable_pin = config->common.en_pin.port != NULL;
	bool has_sleep_pin = config->sleep_pin.port != NULL;
	bool has_fault_pin = config->fault_pin.port != NULL;
	k_timeout_t enable_timeout;
	int ret;

	ret = drv8825_check_en_sleep_pin(config);
	if (ret != 0) {
		return ret;
	}

	ret = drv8825_set_sleep_pin_state(dev, true);
	if (ret != 0) {
		return ret;
	}

	ret = drv8825_set_en_pin_state(dev, true);
	if (ret != 0) {
		return ret;
	}

	if (has_enable_pin) {
		enable_timeout = DRV8825_ENABLE_TIME;
	}

	if (has_sleep_pin) {
		enable_timeout = DRV8825_WAKE_UP_TIME;
	}

	if (has_fault_pin) {
		/* Wait after enable/wakeup until the fault pin is guaranteed to be in the
		 * proper state.
		 */
		k_sleep(enable_timeout);
		ret = gpio_add_callback_dt(&config->fault_pin, &data->fault_cb_data);
		if (ret != 0) {
			LOG_ERR("%s: Failed to add fault callback (error: %d)", dev->name, ret);
			return ret;
		}
	}

	return ret;
}

static int drv8825_disable(const struct device *dev)
{
	const struct drv8825_config *config = dev->config;
	struct drv8825_data *data = dev->data;
	bool has_fault_pin = config->fault_pin.port != NULL;
	int ret;

	ret = drv8825_check_en_sleep_pin(config);
	if (ret != 0) {
		return ret;
	}

	ret = drv8825_set_sleep_pin_state(dev, false);
	if (ret != 0) {
		return ret;
	}

	ret = drv8825_set_en_pin_state(dev, false);
	if (ret != 0) {
		return ret;
	}

    ret = drv8825_set_reset_pin_state(dev, false);
    if (ret != 0) {
        return ret;
    }

	if (has_fault_pin) {
		ret = gpio_remove_callback_dt(&config->fault_pin, &data->fault_cb_data);
		if (ret != 0) {
			LOG_ERR("%s: Failed to remove fault callback (error: %d)", dev->name, ret);
			return ret;
		}
	}

	return ret;
}

static int drv8825_set_fault_cb(const struct device *dev, stepper_event_cb_t fault_cb,
				void *user_data)
{
	struct drv8825_data *data = dev->data;

	data->fault_cb = fault_cb;
	data->fault_cb_user_data = user_data;

	return 0;
}

static int drv8825_set_micro_step_res(const struct device *dev,
				      enum stepper_micro_step_resolution micro_step_res)
{
	const struct drv8825_config *config = dev->config;
	struct drv8825_data *data = dev->data;
	int ret;

	uint8_t m0_value = 0;
	uint8_t m1_value = 0;
    uint8_t m2_value = 0;

	if ((config->common.m0_pin.port == NULL) || (config->common.m1_pin.port == NULL) || (config->m2_pin.port == NULL)) {

		LOG_ERR("%s: Failed to set microstep resolution: microstep pins are not defined "
			"(error: %d)",
			dev->name, -ENOTSUP);
		return -ENOTSUP;
	}

	/* 0: low
	 * 1: high
	 * 2: Hi-Z
	 * 3: 330kΩ
	 */
	switch (micro_step_res) {
	case STEPPER_MICRO_STEP_1:
		m0_value = 0;
		m1_value = 0;
        m2_value = 0;
		break;
	case STEPPER_MICRO_STEP_2:
		m0_value = 1;
		m1_value = 0;
        m2_value = 0;
		break;
	case STEPPER_MICRO_STEP_4:
		m0_value = 0;
		m1_value = 1;
        m2_value = 0;
		break;
	case STEPPER_MICRO_STEP_8:
		m0_value = 1;
		m1_value = 1;
        m2_value = 0;
		break;
	case STEPPER_MICRO_STEP_16:
		m0_value = 0;
		m1_value = 0;
        m2_value = 1;
		break;
	case STEPPER_MICRO_STEP_32:
		m0_value = 1;
		m1_value = 0;
        m2_value = 1;
		break;
	default:
		return -ENOTSUP;
	};

	ret = drv8825_set_microstep_pin(dev, &config->common.m0_pin, m0_value);
	if (ret != 0) {
		return ret;
	}

	ret = drv8825_set_microstep_pin(dev, &config->common.m1_pin, m1_value);
	if (ret != 0) {
		return ret;
	}

    ret = drv8825_set_microstep_pin(dev, &config->m2_pin, m2_value);
	if (ret != 0) {
		return ret;
	}

	data->ustep_res = micro_step_res;
	data->pin_states.m0 = m0_value;
	data->pin_states.m1 = m1_value;
    data->pin_states.m2 = m2_value;
	return 0;
}

static int drv8825_get_micro_step_res(const struct device *dev,
				      enum stepper_micro_step_resolution *micro_step_res)
{
	struct drv8825_data *data = dev->data;
	*micro_step_res = data->ustep_res;
	return 0;
}

void fault_event(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct drv8825_data *data = CONTAINER_OF(cb, struct drv8825_data, fault_cb_data);

	if (data->fault_cb != NULL) {
		data->fault_cb(data->dev, STEPPER_EVENT_FAULT_DETECTED,
			data->fault_cb_user_data);
	} else {
		LOG_WRN_ONCE("%s: Fault pin triggered but no callback is set", dev->name);
	}
}

static int drv8825_init(const struct device *dev)
{
	const struct drv8825_config *const config = dev->config;
	struct drv8825_data *const data = dev->data;
	int ret;

	/* Configure sleep pin if it is available */
	if (config->sleep_pin.port != NULL) {
		ret = gpio_pin_configure_dt(&config->sleep_pin, GPIO_OUTPUT_ACTIVE);
		if (ret != 0) {
			LOG_ERR("%s: Failed to configure sleep_pin (error: %d)", dev->name, ret);
			return ret;
		}
		data->pin_states.sleep = 1U;
	}

    /* Configure reset pin if it is available */
	if (config->reset_pin.port != NULL) {
		ret = gpio_pin_configure_dt(&config->reset_pin, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("%s: Failed to configure reset_pin (error: %d)", dev->name, ret);
			return ret;
		}
		data->pin_states.reset = 0U;
	}

	/* Configure enable pin if it is available */
	if (config->common.en_pin.port != NULL) {
		ret = gpio_pin_configure_dt(&config->common.en_pin, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("%s: Failed to configure en_pin (error: %d)", dev->name, ret);
			return ret;
		}
		data->pin_states.en = 0U;
	}

	/* Configure microstep pin 0 if it is available */
	if (config->common.m0_pin.port != NULL) {
		ret = gpio_pin_configure_dt(&config->common.m0_pin, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("%s: Failed to configure m0_pin (error: %d)", dev->name, ret);
			return ret;
		}
		data->pin_states.m0 = 0U;
	}

	/* Configure microstep pin 1 if it is available */
	if (config->common.m1_pin.port != NULL) {
		ret = gpio_pin_configure_dt(&config->common.m1_pin, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("%s: Failed to configure m1_pin (error: %d)", dev->name, ret);
			return ret;
		}
		data->pin_states.m1 = 0U;
	}

	/* Configure microstep pin 2 if it is available */
	if (config->m2_pin.port != NULL) {
		ret = gpio_pin_configure_dt(&config->m2_pin, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("%s: Failed to configure m2_pin (error: %d)", dev->name, ret);
			return ret;
		}
		data->pin_states.m2 = 0U;
	}

	if ((config->common.m0_pin.port != NULL) && (config->common.m1_pin.port != NULL)\
        && (config->m2_pin.port != NULL)) {
		ret = drv8825_set_micro_step_res(dev, data->ustep_res);
		if (ret != 0) {
			return ret;
		}
	}

	/* Configure fault pin if it is available */
	if (config->fault_pin.port != NULL) {
		ret = gpio_pin_configure_dt(&config->fault_pin, GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("%s: Failed to configure fault_pin (error: %d)", dev->name, ret);
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(&config->fault_pin,
						      GPIO_INT_EDGE_TO_INACTIVE);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", ret,
				config->fault_pin.port->name, config->fault_pin.pin);
			return ret;
		}

		gpio_init_callback(&data->fault_cb_data, fault_event, BIT(config->fault_pin.pin));
	}

	return 0;
}

static DEVICE_API(stepper, drv8825_stepper_api) = {
	.enable = drv8825_enable,
	.disable = drv8825_disable,
	.set_event_cb = drv8825_set_fault_cb,
	.set_micro_step_res = drv8825_set_micro_step_res,
	.get_micro_step_res = drv8825_get_micro_step_res,
};

#define DRV8825_DEVICE(inst)                                                                       \
                                                                                                   \
	static const struct drv8825_config drv8825_config_##inst = {                               \
		.common = STEP_DIR_STEPPER_DT_INST_COMMON_CONFIG_INIT(inst),                       \
		.sleep_pin = GPIO_DT_SPEC_INST_GET_OR(inst, sleep_gpios, {0}),                     \
		.fault_pin = GPIO_DT_SPEC_INST_GET_OR(inst, fault_gpios, {0}),                     \
		.reset_pin = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                     \
		.m2_pin = GPIO_DT_SPEC_INST_GET_OR(inst, m2_gpios, {0}),                           \
	};                                                                                         \
                                                                                                   \
	static struct drv8825_data drv8825_data_##inst = {                                         \
		.ustep_res = DT_INST_PROP(inst, micro_step_res),                                   \
		.dev = DEVICE_DT_INST_GET(inst),                                                   \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &drv8825_init, NULL, &drv8825_data_##inst,                     \
			      &drv8825_config_##inst, POST_KERNEL, CONFIG_STEPPER_INIT_PRIORITY,   \
			      &drv8825_stepper_api);

DT_INST_FOREACH_STATUS_OKAY(DRV8825_DEVICE)