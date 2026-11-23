#pragma once

// Starting gains; tune on the physical setup.
#define PID_KP 0.60f
#define PID_KI 0.80f
#define PID_KD 0.01f

typedef struct {
    float integral;
    float previous_measurement;
} pid_state_t;

// Setpoint, measurement and output are percentages; dt is in seconds.
float pid_update(pid_state_t *pid, float setpoint, float measurement, float dt);
