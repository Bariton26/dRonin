/**
 ******************************************************************************
 * @addtogroup PIOS PIOS Core hardware abstraction layer
 * @{
 * @addtogroup PIOS_ADC ADC layer functions
 * @brief Upper level Analog to Digital converter layer
 * @{
 *
 * @file       pios_adc.h
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2012.
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2013-2014
 * @brief      ADC layer functions header
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef PIOS_ADC_H
#define PIOS_ADC_H

#include <stdint.h>		/* uint*_t */
#include <stdbool.h>	/* bool */
#include "pios_queue.h"

struct pios_adc_driver {
	void (*init)(uintptr_t id);
	int32_t (*get_pin)(uintptr_t id, uint32_t pin);
	bool (*available)(uintptr_t id, uint32_t device_pin);
	uint8_t (*number_of_channels)(uintptr_t id);
	float (*lsb_voltage)(uintptr_t id);
};

/* Public Functions */
extern int32_t PIOS_ADC_DevicePinGet(uintptr_t adc_id, uint32_t device_pin);
extern bool PIOS_ADC_Available(uintptr_t adc_id, uint32_t device_pin);
extern int32_t PIOS_ADC_GetChannelRaw(uint32_t channel);
extern float PIOS_ADC_GetChannelVolt(uint32_t channel);

#endif /* PIOS_ADC_H */

/**
  * @}
  * @}
  */
