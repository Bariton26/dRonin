/**
 ******************************************************************************
 * @addtogroup Modules Modules
 * @{
 * @addtogroup AltitudeHoldModule Altitude hold module
 * @{
 *
 * @file       altitudehold.c
 * @author     dRonin, http://dRonin.org/, Copyright (C) 2015-2016
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2012-2014
 * @brief      This module runs an EKF to estimate altitude from just a barometric
 *             sensor and controls throttle to hold a fixed altitude
 *
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
 *
 * Additional note on redistribution: The copyright and license notices above
 * must be maintained in each individual source file that is a derivative work
 * of this source file; otherwise redistribution is prohibited.
 */

/**
 * Input object: @ref PositionActual
 * Input object: @ref VelocityActual
 * Input object: @ref ManualControlCommand
 * Input object: @ref AltitudeHoldDesired
 * Output object: @ref StabilizationDesired
 *
 * Hold the VTOL aircraft at a fixed altitude by running nested
 * control loops to stay at the AltitudeHoldDesired height.
 */

#include "openpilot.h"
#include "physical_constants.h"
#include "misc_math.h"
#include "pid.h"

#include "attitudeactual.h"
#include "altitudeholdsettings.h"
#include "altitudeholddesired.h"
#include "altitudeholdstate.h"
#include "flightstatus.h"
#include "stabilizationdesired.h"
#include "positionactual.h"
#include "velocityactual.h"
#include "modulesettings.h"
#include "pios_thread.h"
#include "pios_queue.h"

// Private constants
#define MAX_QUEUE_SIZE 4
#define STACK_SIZE_BYTES 648
#define TASK_PRIORITY PIOS_THREAD_PRIO_LOW

// Private variables
static struct pios_thread *altitudeHoldTaskHandle;
static struct pios_queue *queue;
static bool module_enabled;

// Private functions
static void altitudeHoldTask(void *parameters);

/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t AltitudeHoldStart()
{
	// Start main task if it is enabled
	if (module_enabled) {
		altitudeHoldTaskHandle = PIOS_Thread_Create(altitudeHoldTask, "AltitudeHold", STACK_SIZE_BYTES, NULL, TASK_PRIORITY);
		TaskMonitorAdd(TASKINFO_RUNNING_ALTITUDEHOLD, altitudeHoldTaskHandle);
		return 0;
	}
	return -1;

}

/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t AltitudeHoldInitialize()
{
#ifdef MODULE_AltitudeHold_BUILTIN
	module_enabled = true;
#else
	uint8_t module_state[MODULESETTINGS_ADMINSTATE_NUMELEM];
	ModuleSettingsAdminStateGet(module_state);
	if (module_state[MODULESETTINGS_ADMINSTATE_ALTITUDEHOLD] == MODULESETTINGS_ADMINSTATE_ENABLED) {
		module_enabled = true;
	} else {
		module_enabled = false;
	}
#endif

	if (!PIOS_SENSORS_GetQueue(PIOS_SENSOR_BARO)) {
		module_enabled = false;
		return -1;
	}

	if (AltitudeHoldSettingsInitialize() == -1) {
		module_enabled = false;
		return -1;
	}

	if(module_enabled) {
		if (AltitudeHoldDesiredInitialize() == -1) {
			module_enabled = false;
			return -1;
		}

		if (AltitudeHoldStateInitialize() == -1) {
			module_enabled = false;
			return -1;
		}

		// Create object queue
		queue = PIOS_Queue_Create(MAX_QUEUE_SIZE, sizeof(UAVObjEvent));

		return 0;
	}

	return -1;
}
MODULE_INITCALL(AltitudeHoldInitialize, AltitudeHoldStart);

/**
 * Module thread, should not return.
 */
static void altitudeHoldTask(void *parameters)
{
	bool engaged = false;

	AltitudeHoldDesiredData altitudeHoldDesired;
	StabilizationDesiredData stabilizationDesired;
	AltitudeHoldSettingsData altitudeHoldSettings;

	UAVObjEvent ev;
	struct pid velocity_pid;

	pid_zero(&velocity_pid);

	// Listen for object updates.
	AltitudeHoldSettingsConnectQueue(queue);
	FlightStatusConnectQueue(queue);

	AltitudeHoldSettingsGet(&altitudeHoldSettings);
	// Main task loop
	const uint32_t dt_ms = 20;

	pid_configure(&velocity_pid, altitudeHoldSettings.VelocityKp,
		          altitudeHoldSettings.VelocityKi, 0.0f, 1.0f,
			  dt_ms / 1000.0f);

	AlarmsSet(SYSTEMALARMS_ALARM_ALTITUDEHOLD, SYSTEMALARMS_ALARM_OK);

	uint32_t timeout = dt_ms;

	while (1) {
		if (PIOS_Queue_Receive(queue, &ev, timeout) != true) {

		} else if (ev.obj == FlightStatusHandle()) {

			uint8_t flight_mode;
			FlightStatusFlightModeGet(&flight_mode);

			if (flight_mode == FLIGHTSTATUS_FLIGHTMODE_ALTITUDEHOLD && !engaged) {
				// Copy the current throttle as a starting point for integral
				StabilizationDesiredThrustGet(&velocity_pid.iAccumulator);
				engaged = true;
			} else if (flight_mode != FLIGHTSTATUS_FLIGHTMODE_ALTITUDEHOLD)
				engaged = false;

			// Run loop at 20 Hz when engaged otherwise just slowly wait for it to be engaged
			timeout = engaged ? dt_ms : 100;

			continue;

		} else if (ev.obj == AltitudeHoldSettingsHandle()) {
			AltitudeHoldSettingsGet(&altitudeHoldSettings);

			pid_configure(&velocity_pid,
					altitudeHoldSettings.VelocityKp,
					altitudeHoldSettings.VelocityKi,
					0.0f, 1.0f, dt_ms / 1000.0f);
			continue;
		}

		// When engaged compute altitude controller output
		if (engaged) {
			float position_z, velocity_z, altitude_error;

			PositionActualDownGet(&position_z);
			VelocityActualDownGet(&velocity_z);
			position_z = -position_z; // Use positive up convention
			velocity_z = -velocity_z; // Use positive up convention

			// Compute the altitude error
			AltitudeHoldDesiredGet(&altitudeHoldDesired);
			altitude_error = altitudeHoldDesired.Altitude - position_z;

			bool landing = altitudeHoldDesired.Land == ALTITUDEHOLDDESIRED_LAND_TRUE;

			// For landing mode allow throttle to go negative to allow the integrals
			// to stop winding up
			const float min_throttle = landing ? -0.1f : 0.0f;

			float velocity_desired = altitude_error * altitudeHoldSettings.PositionKp + altitudeHoldDesired.ClimbRate;
			float throttle_desired = pid_apply_antiwindup(&velocity_pid, 
			                    velocity_desired - velocity_z,
			                    min_throttle, 1.0f // positive limits since this is throttle
			                    );

			AltitudeHoldStateData altitudeHoldState;
			altitudeHoldState.VelocityDesired = velocity_desired;
			altitudeHoldState.Integral = velocity_pid.iAccumulator;
			altitudeHoldState.AngleGain = 1.0f;

			if (altitudeHoldSettings.AttitudeComp > 0) {
				// Thrust desired is at this point the mount desired in the up direction, we can
				// account for the attitude if desired
				AttitudeActualData attitudeActual;
				AttitudeActualGet(&attitudeActual);

				// Project a unit vector pointing up into the body frame and
				// get the z component
				float fraction = attitudeActual.q1 * attitudeActual.q1 -
				                 attitudeActual.q2 * attitudeActual.q2 -
				                 attitudeActual.q3 * attitudeActual.q3 +
				                 attitudeActual.q4 * attitudeActual.q4;

				// Add ability to scale up the amount of compensation to achieve
				// level forward flight
				fraction = powf(fraction, (float) altitudeHoldSettings.AttitudeComp / 100.0f);

				// Dividing by the fraction remaining in the vertical projection will
				// attempt to compensate for tilt. This acts like the thrust is linear
				// with the output which isn't really true. If the fraction is starting
				// to go negative we are inverted and should shut off throttle
				throttle_desired = (fraction > 0.1f) ? (throttle_desired / fraction) : 0.0f;

				altitudeHoldState.AngleGain = 1.0f / fraction;
			}

			altitudeHoldState.Thrust = throttle_desired;
			AltitudeHoldStateSet(&altitudeHoldState);

			stabilizationDesired.Thrust = bound_min_max(throttle_desired, min_throttle, 1.0f);

			stabilizationDesired.ReprojectionMode = STABILIZATIONDESIRED_REPROJECTIONMODE_NONE;

			if (landing) {
				stabilizationDesired.StabilizationMode[STABILIZATIONDESIRED_STABILIZATIONMODE_ROLL] = STABILIZATIONDESIRED_STABILIZATIONMODE_ATTITUDE;
				stabilizationDesired.StabilizationMode[STABILIZATIONDESIRED_STABILIZATIONMODE_PITCH] = STABILIZATIONDESIRED_STABILIZATIONMODE_ATTITUDE;
				stabilizationDesired.StabilizationMode[STABILIZATIONDESIRED_STABILIZATIONMODE_YAW] = STABILIZATIONDESIRED_STABILIZATIONMODE_AXISLOCK;
				stabilizationDesired.Roll = 0;
				stabilizationDesired.Pitch = 0;
				stabilizationDesired.Yaw = 0;
			} else {
				stabilizationDesired.StabilizationMode[STABILIZATIONDESIRED_STABILIZATIONMODE_ROLL] = STABILIZATIONDESIRED_STABILIZATIONMODE_ATTITUDE;
				stabilizationDesired.StabilizationMode[STABILIZATIONDESIRED_STABILIZATIONMODE_PITCH] = STABILIZATIONDESIRED_STABILIZATIONMODE_ATTITUDE;
				stabilizationDesired.StabilizationMode[STABILIZATIONDESIRED_STABILIZATIONMODE_YAW] = STABILIZATIONDESIRED_STABILIZATIONMODE_AXISLOCK;
				stabilizationDesired.Roll = altitudeHoldDesired.Roll;
				stabilizationDesired.Pitch = altitudeHoldDesired.Pitch;
				stabilizationDesired.Yaw = altitudeHoldDesired.Yaw;
			}
			StabilizationDesiredSet(&stabilizationDesired);
		}

	}
}

/**
 * @}
 * @}
 */

