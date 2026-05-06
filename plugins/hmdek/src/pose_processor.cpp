#include "android_hmd_plugin.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#include <algorithm>
#include <cmath>

namespace android_hmd {

namespace {

constexpr double kPi = 3.14159265358979323846;

double ClampUnit(double v) {
    return std::clamp(v, -1.0, 1.0);
}

double NormalizeAlpha(double alpha, double dt, double reference_hz) {
    alpha = std::clamp(alpha, 0.0, 1.0);
    if (alpha <= 0.0) {
        return 0.0;
    }
    if (alpha >= 1.0 || dt <= 0.0 || reference_hz <= 0.0) {
        return alpha;
    }

    const double reference_dt = 1.0 / reference_hz;
    const double ratio = dt / reference_dt;
    return 1.0 - std::pow(1.0 - alpha, ratio);
}

void NormalizeQuaternion(double& qw, double& qx, double& qy, double& qz) {
    const double norm = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
    if (norm <= 1e-12) {
        qw = 1.0;
        qx = qy = qz = 0.0;
        return;
    }

    qw /= norm;
    qx /= norm;
    qy /= norm;
    qz /= norm;
}

} // namespace

TrackingPose PoseProcessor::Process(const TrackingPose& raw) {
    const auto now = std::chrono::steady_clock::now();

    if (!m_initialized) {
        TrackingPose initial = raw;
        NormalizeQuaternion(initial.qw, initial.qx, initial.qy, initial.qz);
        initial.linVelX = 0.0;
        initial.linVelY = 0.0;
        initial.linVelZ = 0.0;
        initial.angVelX = 0.0;
        initial.angVelY = 0.0;
        initial.angVelZ = 0.0;

        m_prev_raw = initial;
        m_smoothed = initial;
        m_last_process_time = now;
        m_initialized = true;

        ApplyPitchOffset(initial);
        return initial;
    }

    double dt = std::chrono::duration<double>(now - m_last_process_time).count();
    m_last_process_time = now;
    if (!(dt > 0.0)) {
        dt = 1.0 / (std::max)(1.0, m_cfg.sensor_hz);
    }
    dt = std::clamp(dt, 1.0 / 240.0, 1.0 / 20.0);

    const double pos_alpha = NormalizeAlpha(m_cfg.ema_alpha_pos, dt, m_cfg.sensor_hz);
    const double rot_alpha = NormalizeAlpha(m_cfg.ema_alpha_rot, dt, m_cfg.sensor_hz);
    const double vel_alpha = NormalizeAlpha(m_cfg.ema_alpha_vel, dt, m_cfg.sensor_hz);

    TrackingPose result = raw;
    NormalizeQuaternion(result.qw, result.qx, result.qy, result.qz);

    const double dx_raw = raw.x - m_smoothed.x;
    const double dy_raw = raw.y - m_smoothed.y;
    const double dz_raw = raw.z - m_smoothed.z;
    const double pos_delta = std::sqrt(dx_raw * dx_raw + dy_raw * dy_raw + dz_raw * dz_raw);
    if (pos_delta < m_cfg.pos_deadzone_m) {
        result.x = m_smoothed.x;
        result.y = m_smoothed.y;
        result.z = m_smoothed.z;
    } else {
        result.x = m_smoothed.x + (raw.x - m_smoothed.x) * pos_alpha;
        result.y = m_smoothed.y + (raw.y - m_smoothed.y) * pos_alpha;
        result.z = m_smoothed.z + (raw.z - m_smoothed.z) * pos_alpha;
    }

    double target_qw = result.qw;
    double target_qx = result.qx;
    double target_qy = result.qy;
    double target_qz = result.qz;

    double dot = target_qw * m_smoothed.qw + target_qx * m_smoothed.qx +
                 target_qy * m_smoothed.qy + target_qz * m_smoothed.qz;
    if (dot < 0.0) {
        target_qw = -target_qw;
        target_qx = -target_qx;
        target_qy = -target_qy;
        target_qz = -target_qz;
        dot = -dot;
    }

    const double angle = 2.0 * std::acos(ClampUnit(dot));
    if (angle < m_cfg.rot_deadzone_rad) {
        result.qw = m_smoothed.qw;
        result.qx = m_smoothed.qx;
        result.qy = m_smoothed.qy;
        result.qz = m_smoothed.qz;
    } else {
        result.qw = m_smoothed.qw + (target_qw - m_smoothed.qw) * rot_alpha;
        result.qx = m_smoothed.qx + (target_qx - m_smoothed.qx) * rot_alpha;
        result.qy = m_smoothed.qy + (target_qy - m_smoothed.qy) * rot_alpha;
        result.qz = m_smoothed.qz + (target_qz - m_smoothed.qz) * rot_alpha;
        NormalizeQuaternion(result.qw, result.qx, result.qy, result.qz);
    }

    const bool phone_has_linvel =
        (raw.linVelX != 0.0 || raw.linVelY != 0.0 || raw.linVelZ != 0.0);
    const bool phone_has_angvel =
        (raw.angVelX != 0.0 || raw.angVelY != 0.0 || raw.angVelZ != 0.0);

    double target_lin_x = 0.0;
    double target_lin_y = 0.0;
    double target_lin_z = 0.0;
    if (phone_has_linvel) {
        target_lin_x = raw.linVelX;
        target_lin_y = raw.linVelY;
        target_lin_z = raw.linVelZ;
    }

    double target_ang_x = 0.0;
    double target_ang_y = 0.0;
    double target_ang_z = 0.0;
    if (phone_has_angvel) {
        target_ang_x = raw.angVelX;
        target_ang_y = raw.angVelY;
        target_ang_z = raw.angVelZ;
    }

    result.linVelX = m_smoothed.linVelX + (target_lin_x - m_smoothed.linVelX) * vel_alpha;
    result.linVelY = m_smoothed.linVelY + (target_lin_y - m_smoothed.linVelY) * vel_alpha;
    result.linVelZ = m_smoothed.linVelZ + (target_lin_z - m_smoothed.linVelZ) * vel_alpha;

    result.angVelX = m_smoothed.angVelX + (target_ang_x - m_smoothed.angVelX) * vel_alpha;
    result.angVelY = m_smoothed.angVelY + (target_ang_y - m_smoothed.angVelY) * vel_alpha;
    result.angVelZ = m_smoothed.angVelZ + (target_ang_z - m_smoothed.angVelZ) * vel_alpha;

    ApplyPitchOffset(result);

    m_prev_raw = raw;
    m_smoothed = result;
    return result;
}

void PoseProcessor::ApplyPitchOffset(TrackingPose& pose) const {
    if (std::abs(m_cfg.pitch_offset_deg) < 1e-6) {
        return;
    }

    const double pitch = m_cfg.pitch_offset_deg * kPi / 180.0;
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);

    const double old_y = pose.y;
    const double old_z = pose.z;
    pose.y = old_y * cp - old_z * sp;
    pose.z = old_y * sp + old_z * cp;

    const double half = pitch * 0.5;
    const double ow = std::cos(half);
    const double ox = std::sin(half);

    const double qw = pose.qw;
    const double qx = pose.qx;
    const double qy = pose.qy;
    const double qz = pose.qz;

    pose.qw = ow * qw - ox * qx;
    pose.qx = ow * qx + ox * qw;
    pose.qy = ow * qy - ox * qz;
    pose.qz = ow * qz + ox * qy;
    NormalizeQuaternion(pose.qw, pose.qx, pose.qy, pose.qz);

    const double lvy = pose.linVelY;
    const double lvz = pose.linVelZ;
    pose.linVelY = lvy * cp - lvz * sp;
    pose.linVelZ = lvy * sp + lvz * cp;

    const double avy = pose.angVelY;
    const double avz = pose.angVelZ;
    pose.angVelY = avy * cp - avz * sp;
    pose.angVelZ = avy * sp + avz * cp;
}

} // namespace android_hmd
