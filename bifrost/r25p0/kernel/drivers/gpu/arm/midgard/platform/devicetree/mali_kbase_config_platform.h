/*
 * mali_kbase_config_platform.h
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

/**
 * Maximum frequency GPU will be clocked at. Given in kHz.
 * This must be specified as there is no default value.
 *
 * Attached value: number in kHz
 * Default value: NA
 */
#define GPU_FREQ_KHZ_MAX (750000)
/**
 * Minimum frequency GPU will be clocked at. Given in kHz.
 * This must be specified as there is no default value.
 *
 * Attached value: number in kHz
 * Default value: NA
 */
#define GPU_FREQ_KHZ_MIN (100000)

/**
 * CPU_SPEED_FUNC - A pointer to a function that calculates the CPU clock
 *
 * CPU clock speed of the platform is in MHz - see kbase_cpu_clk_speed_func
 * for the function prototype.
 *
 * Attached value: A kbase_cpu_clk_speed_func.
 * Default Value:  NA
 */
#define CPU_SPEED_FUNC (NULL)

/**
 * GPU_SPEED_FUNC - A pointer to a function that calculates the GPU clock
 *
 * GPU clock speed of the platform in MHz - see kbase_gpu_clk_speed_func
 * for the function prototype.
 *
 * Attached value: A kbase_gpu_clk_speed_func.
 * Default Value:  NA
 */
#define GPU_SPEED_FUNC (NULL)

/**
 * Power management configuration
 *
 * Attached value: pointer to @ref kbase_pm_callback_conf
 * Default value: See @ref kbase_pm_callback_conf
 */
#define POWER_MANAGEMENT_CALLBACKS (&pm_callbacks)

/**
 * Platform specific configuration functions
 *
 * Attached value: pointer to @ref kbase_platform_funcs_conf
 * Default value: See @ref kbase_platform_funcs_conf
 */
extern struct kbase_platform_funcs_conf dt_funcs_conf;
#define PLATFORM_FUNCS (&dt_funcs_conf)

/** Power model for IPA
 *
 * Attached value: pointer to @ref mali_pa_model_ops
 */
#ifdef CONFIG_DEVFREQ_THERMAL
#define POWER_MODEL_CALLBACKS (&t83x_model_ops)
extern struct devfreq_cooling_ops t83x_model_ops;
#else
#define POWER_MODEL_CALLBACKS (NULL)
#endif
extern struct kbase_pm_callback_conf pm_callbacks;

/**
 * Autosuspend delay
 *
 * The delay time (in milliseconds) to be used for autosuspend
 */
#define AUTO_SUSPEND_DELAY (100)
