#pragma once
#include <cstdint>

// The safety states a script can see. The VM only uses each state's NAME:
// dlm.telemetry() hands the script `safety_state` as one of the strings below.
enum class SafetyState : uint8_t
{
    BOOT_INHIBITED = 0,
    CONFIG_REQUIRED,
    READY_OPEN,
    RUNNING,
    SHED_OFF,
    RECONNECT_PENDING,
    SENSOR_FAULT,
    ACTUATOR_FAULT,
    TRIP_LATCHED,
    MANUAL_OFF,
    SCHEDULE_OFF,
    OTA_INHIBITED,
    INTERNAL_CT_VERIFYING,
    THERMAL_OFF,
};

inline const char* toString(SafetyState state)
{
    switch (state)
    {
        case SafetyState::BOOT_INHIBITED: return "BOOT_INHIBITED";
        case SafetyState::CONFIG_REQUIRED: return "CONFIG_REQUIRED";
        case SafetyState::READY_OPEN: return "READY_OPEN";
        case SafetyState::RUNNING: return "RUNNING";
        case SafetyState::SHED_OFF: return "SHED_OFF";
        case SafetyState::RECONNECT_PENDING: return "RECONNECT_PENDING";
        case SafetyState::SENSOR_FAULT: return "SENSOR_FAULT";
        case SafetyState::ACTUATOR_FAULT: return "ACTUATOR_FAULT";
        case SafetyState::TRIP_LATCHED: return "TRIP_LATCHED";
        case SafetyState::MANUAL_OFF: return "MANUAL_OFF";
        case SafetyState::SCHEDULE_OFF: return "SCHEDULE_OFF";
        case SafetyState::OTA_INHIBITED: return "OTA_INHIBITED";
        case SafetyState::INTERNAL_CT_VERIFYING: return "INTERNAL_CT_VERIFYING";
        case SafetyState::THERMAL_OFF: return "THERMAL_OFF";
    }
    return "UNKNOWN";
}
