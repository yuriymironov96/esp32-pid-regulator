#include "pid.h"

#include <math.h>

float pid_update(pid_state_t *pid, float setpoint, float measurement, float dt)
{
    const float error = setpoint - measurement;
    const float p = PID_KP * error;

    // Differentiate the measurement to avoid a kick on setpoint changes.
    const float d = -PID_KD * (measurement - pid->previous_measurement) / dt;
    pid->previous_measurement = measurement;

    const float increment = PID_KI * error * dt;
    const float next_integral = pid->integral + increment;
    const float output = p + next_integral + d;

    // Prevent integral buildup when it would drive PWM further into saturation.
    if ((output >= 0.0f && output <= 100.0f) ||
        (output > 100.0f && increment < 0.0f) ||
        (output < 0.0f && increment > 0.0f)) {
        pid->integral = next_integral;
    }

    return fminf(fmaxf(p + pid->integral + d, 0.0f), 100.0f);
}
