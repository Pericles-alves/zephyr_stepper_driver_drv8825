/**
 * @file drivers/stepper/stepper_drv8825.h
 *
 * @brief Public API for DRV8825 Stepper Controller Specific Functions
 *
 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Navimatix GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_STEPPER_DRV8825_H_
#define ZEPHYR_INCLUDE_DRIVERS_STEPPER_DRV8825_H_

#include <stdint.h>
#include <zephyr/drivers/stepper/stepper.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief After microstep setter fails, attempt to recover into previous state.
 *
 * @param dev Pointer to the stepper motor controller instance
 *
 * @retval 0 Success
 * @retval <0 Error code dependent on the gpio controller of the microstep pins
 */
int drv8825_microstep_recovery(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_STEPPER_STEPPER_DRV8825_H_ */