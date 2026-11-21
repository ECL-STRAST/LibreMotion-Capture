#include "C3DRecorder.h"
#include "platform/Interfaces.h"
#include "platform/Log.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

#include <ezc3d/ezc3d.h> // Libreria externa open source para tratamiento de archivos c3d
#include <ezc3d/Parameters.h>
#include <ezc3d/Data.h>

#define LOGI(...) lmc::Log::info(__VA_ARGS__)
#define LOGE(...) lmc::Log::err(__VA_ARGS__)

// Simple 3D vector helper used internally for geometry operations
struct Vector3 {
    float x, y, z;
};

C3DRecorder::C3DRecorder() {}
C3DRecorder::~C3DRecorder() {
    // Safety, if someone forgets to call C3Dfinalize explicitly, we end it automatically to avoid losing data.
    if (initialized_ && !finalized_) {
        C3Dfinalize();
    }
}

bool C3DRecorder::C3DisInitialized() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return initialized_;
}

bool C3DRecorder::C3Dinitialize(const UploaderConfig& cfg, int frameRate, lmc::IPathResolver* resolver) {
    std::lock_guard<std::mutex> lock(mtx_);
    // An in-progress session is never clobbered by a stray re-initialize.
    if (initialized_ && !finalized_) {
        LOGI("C3DRecorder already recording, ignoring re-initialize.");
        return true;
    }
    // A finalized session must not block the next one: without this the old
    // finalized_ flag made every later C3DrecordFrame a no-op (Unity play mode
    // and Unreal PIE reuse the process, so only the first session got a file).
    if (finalized_) {
        LOGI("C3DRecorder: previous session finalized, starting a new one.");
        finalized_ = false;
        frames_.clear();
        validity_.clear();
    }

    if (resolver == nullptr) {
        LOGE("C3DRecorder: null path resolver, cannot build C3D path");
        initialized_ = false;
        return false;
    }
    if (!resolver->c3dFilePath(cfg.sessionId, filePath_) || filePath_.empty()) {
        LOGE("C3DRecorder: empty file path, disabling C3D recording");
        initialized_ = false;
        return false;
    }

    frameRate_ = frameRate;
    if (frameRate_ <= 0) frameRate_ = 60;

    frames_.clear();
    validity_.clear();
    finalized_ = false;
    initialized_ = true;

    LOGI("C3DRecorder initialized. Path='%s', frameRate=%d",
         filePath_.c_str(), frameRate_);

    // We do not open any file yet; ezc3d will write everything in finalize.
    return true;
}

void C3DRecorder::C3DrecordFrame(const VRFrameDataPlain& frame) {
    C3DrecordFrame(frame, nullptr);
}

void C3DRecorder::C3DrecordFrame(const VRFrameDataPlain& frame, const VRFrameValidityPlain* validity) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || finalized_) {
        return;
    }
    frames_.push_back(frame);
    FrameValidity fv;
    if (validity) {
        fv.reported = true;
        fv.v = *validity;
    }
    validity_.push_back(fv);
}

void C3DRecorder::C3Dflush() {
    // nothing until longer time session requires it
}

void C3DRecorder::C3Dfinalize() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || finalized_) {
        return;
    }

    if (frames_.empty()) {
        LOGI("C3DRecorder: no frames to write, skipping C3D file.");
        finalized_ = true;
        return;
    }

    LOGI("C3DRecorder: writing C3D file '%s' with %zu frames",
         filePath_.c_str(), frames_.size());

    writeC3D();

    finalized_ = true;
}

// OpenXRHandJoints names, but changing palm and wrist order bc thats how its received via unity/unreal
// Tags are shorter to match similar tags received from full body measurements INDEX_PROXIMAl=FIN as in Gait Model (LFIN y RFIN)
// The rest of the tag codes: Thumb T - Index I - Middle M - Ring R - Little L
// Metacarpal MC - Proximal PX - Distal DI - TIP TP - Intermediate IM
static const char* kHandJointNames[26] = {
        "WRT",
        "PLM",
        "TMC",
        "TPX",
        "TDI",
        "TTP",
        "IMC",
        "FIN",
        "IIM",
        "IDI",
        "ITP",
        "MMC",
        "MPX",
        "MIM",
        "MDI",
        "MTP",
        "RMC",
        "RPX",
        "RIM",
        "RDI",
        "RTP",
        "LMC",
        "LPX",
        "LIM",
        "LDI",
        "LTP"
};

//
void C3DRecorder::buildPointNames(std::vector<std::string>& outPointNames) const {
    outPointNames.clear();
    outPointNames.reserve(3 + 26 + 26 + 4);

    outPointNames.emplace_back("HEAD");
    outPointNames.emplace_back("LFHD");
    outPointNames.emplace_back("RFHD");

    // Left hand joints in XrHandJointEXT order
    for (int i = 0; i < 26; ++i) {
        outPointNames.emplace_back(std::string("L") + kHandJointNames[i]);
    }

    // Right hand joints in XrHandJointEXT order
    for (int i = 0; i < 26; ++i) {
        outPointNames.emplace_back(std::string("R") + kHandJointNames[i]);
    }

    // Wxtra wrist markers similar to Plug-in Gait conventions
    outPointNames.emplace_back("LWRA");
    outPointNames.emplace_back("LWRB");
    outPointNames.emplace_back("RWRA");
    outPointNames.emplace_back("RWRB");
}

void C3DRecorder::buildAnalogNames(std::vector<std::string>& outAnalogNames) const {
    outAnalogNames.clear();

    auto addQuat = [&](const std::string& prefix) {
        outAnalogNames.emplace_back(prefix + "_qx");
        outAnalogNames.emplace_back(prefix + "_qy");
        outAnalogNames.emplace_back(prefix + "_qz");
        outAnalogNames.emplace_back(prefix + "_qw");
    };

    addQuat("HMD");

    // Manos: L_*
    for (int i = 0; i < 26; ++i) {
        addQuat(std::string("L") + kHandJointNames[i]);
    }

    // Manos: R_*
    for (int i = 0; i < 26; ++i) {
        addQuat(std::string("R") + kHandJointNames[i]);
    }
    // Necessary extra channel since our record frequency has jitter (not expected usually on C3d files)
    outAnalogNames.emplace_back("RealTime");
    // Appended after RealTime so that every earlier channel keeps its label and index.
    // RealTime is float32 (about 0.1 ms steps after a few minutes); the time stamp is
    // also written in two parts whose sum is the double time to float32 precision of
    // the fraction: whole seconds (exact up to 2^24 s) and the rest, in [0, 1).
    outAnalogNames.emplace_back("RealTime_s");
    outAnalogNames.emplace_back("RealTime_frac");
    // The application's per-device tracking bits (telemetry_record_frame_ex), as exact
    // small integers; -1 when the frame was recorded without them.
    outAnalogNames.emplace_back("HMD_flags");
    outAnalogNames.emplace_back("L_flags");
    outAnalogNames.emplace_back("R_flags");
}

// helper to calculate 2 points from center-eye to match Gait Model front head points
// First we rotate the given vector so we can add a fixed width
void calculateHeadWidth(const VRPosePlain& midPos,Vector3& pointRight,Vector3& pointLeft, float headWidth) {

    const float halfWidth = headWidth / 2.0f;

    // Extract quaternion components
    float qx = midPos.rotation[0], qy =  midPos.rotation[1], qz =  midPos.rotation[2], qw =  midPos.rotation[3];

    // Lateral vector in local head space
    Vector3 vLocal;
    vLocal.x = halfWidth; vLocal.y = 0.0f; vLocal.z = 0.0f;

    // Vector part of the quaternion
    Vector3 qv;
    qv.x = qx; qv.y = qy; qv.z = qz;

    // t = 2 * cross(qv, vLocal)
    Vector3 t;
    t.x = 2.0f * (qv.y * vLocal.z - qv.z * vLocal.y);
    t.y = 2.0f * (qv.z * vLocal.x - qv.x * vLocal.z);
    t.z = 2.0f * (qv.x * vLocal.y - qv.y * vLocal.x);

    // vWorld = vLocal + qw * t + cross(qv, t)
    Vector3 vWorld;
    vWorld.x = vLocal.x + qw * t.x + (qv.y * t.z - qv.z * t.y);
    vWorld.y = vLocal.y + qw * t.y + (qv.z * t.x - qv.x * t.z);
    vWorld.z = vLocal.z + qw * t.z + (qv.x * t.y - qv.y * t.x);

    Vector3 worldOffset = vWorld;

    // Apply to the head position
    pointRight = {
            midPos.position[0] + worldOffset.x,
            midPos.position[1] + worldOffset.y,
            midPos.position[2] + worldOffset.z
    };

    pointLeft = {
            midPos.position[0] - worldOffset.x,
            midPos.position[1] - worldOffset.y,
            midPos.position[2] - worldOffset.z
    };
}

//Same concept as HeadWith, but this time the data parameter is different
void calculateWristWidthFromJoint(const JointSamplePlain& joint, Vector3& pointRight, Vector3& pointLeft, float wristWidth) {
    const float halfWidth = wristWidth / 2.0f;

    float qx = joint.qx;
    float qy = joint.qy;
    float qz = joint.qz;
    float qw = joint.qw;

    // Vector lateral en el sistema local de la muneca
    Vector3 vLocal;
    vLocal.x = halfWidth; vLocal.y = 0.0f; vLocal.z = 0.0f;

    // Parte vectorial del cuaternion
    Vector3 qv;
    qv.x = qx; qv.y = qy; qv.z = qz;

    // t = 2 * cross(qv, vLocal)
    Vector3 t;
    t.x = 2.0f * (qv.y * vLocal.z - qv.z * vLocal.y);
    t.y = 2.0f * (qv.z * vLocal.x - qv.x * vLocal.z);
    t.z = 2.0f * (qv.x * vLocal.y - qv.y * vLocal.x);

    // vWorld = vLocal + qw * t + cross(qv, t)
    Vector3 vWorld;
    vWorld.x = vLocal.x + qw * t.x + (qv.y * t.z - qv.z * t.y);
    vWorld.y = vLocal.y + qw * t.y + (qv.z * t.x - qv.x * t.z);
    vWorld.z = vLocal.z + qw * t.z + (qv.x * t.y - qv.y * t.x);

    // Aplicar a la posicion del joint
    pointRight = {
            joint.px + vWorld.x,
            joint.py + vWorld.y,
            joint.pz + vWorld.z
    };

    pointLeft = {
            joint.px - vWorld.x,
            joint.py - vWorld.y,
            joint.pz - vWorld.z
    };
}

// Joint slot (0..25, kHandJointNames order) of a sample. Engines send the XR joint
// index in idIndex; the array position only matches it when every joint of the
// layout is present, so the id is what decides the marker/channel. -1 = unusable.
static int jointSlot(const JointSamplePlain& s) {
    return (s.idIndex >= 0 && s.idIndex < 26) ? s.idIndex : -1;
}

// Wrist sample of a hand (slot 0), or nullptr when the hand did not report it.
static const JointSamplePlain* findWrist(const JointSamplePlain* joints, int count) {
    for (int j = 0; j < count && j < 30; ++j)
        if (jointSlot(joints[j]) == 0) return &joints[j];
    return nullptr;
}

// Convert from meters to millimeters (could be left as it is, but mm is more common with c3d files)
inline void metersToMillimeters(float& x, float& y, float& z) {
    const float k = 1000.0f;
    x *= k;
    y *= k;
    z *= k;
}
inline void metersToMillimeters(Vector3& v) {
    metersToMillimeters(v.x, v.y, v.z);
}

// // Axis conversion OpenXR -> Gait: X_g = -Z_old (towards)  Y_g = -X_old (left)  Z_g =  Y_old (up)
inline void openxrToGaitAxes(float& x, float& y, float& z) {
    const float ox = x;
    const float oy = y;
    const float oz = z;

    x = -oz;  // delantero +
    y = -ox;  // izquierda +
    z =  oy;  // arriba +
}
inline void openxrToGaitAxes(Vector3& v) {
    openxrToGaitAxes(v.x, v.y, v.z);
}

// Quaternion multiplication q_out = q_a * q_b (Hamilton product). Used in next method
inline void quatMul(const float ax, const float ay, const float az, const float aw,
                    const float bx, const float by, const float bz, const float bw,
                    float& rx, float& ry, float& rz, float& rw) {
    rx = aw*bx + ax*bw + ay*bz - az*by;
    ry = aw*by - ax*bz + ay*bw + az*bx;
    rz = aw*bz + ax*by - ay*bx + az*bw;
    rw = aw*bw - ax*bx - ay*by - az*bz;
}

// Apply OpenXR -> Gait axis change to a quaternion (x,y,z,w).
inline void openxrToGaitAxesQuat(float& qx, float& qy, float& qz, float& qw) {
    // Cuaternion fijo que implementa la misma transformacion que openxrToGaitAxes en vectores
    const float ax =  0.5f;
    const float ay = -0.5f;
    const float az = -0.5f;
    const float aw =  0.5f;

    float rx, ry, rz, rw;
    quatMul(ax, ay, az, aw, qx, qy, qz, qw, rx, ry, rz, rw);

    qx = rx;
    qy = ry;
    qz = rz;
    qw = rw;
}

// Main function that builds an ezc3d::c3d object in memory and writes it to disk
// using the information stored in frames_
void C3DRecorder::writeC3D() {
    // Create an empty C3D
    ezc3d::c3d c3d;

    // Point and analog channel names
    std::vector<std::string> pointNames;
    std::vector<std::string> analogNames;
    buildPointNames(pointNames);
    buildAnalogNames(analogNames);

    const size_t nbPoints  = pointNames.size();
    const size_t nbAnalogs = analogNames.size();
    const size_t nbFrames  = frames_.size();

    // Declare points and analog channels so that parameters are consistent.
    for (const auto& name : pointNames) {
        c3d.point(name);
    }
    for (const auto& name : analogNames) {
        c3d.analog(name);
    }

    // Basic RATE and UNITS parameters
    {
        // POINT:UNITS
        ezc3d::ParametersNS::GroupNS::Parameter pointUnits("UNITS");
        pointUnits.set(std::vector<std::string>{"mm"});
        c3d.parameter("POINT", pointUnits);
        // POINT:RATE
        ezc3d::ParametersNS::GroupNS::Parameter pointRate("RATE");
        pointRate.set(std::vector<double>{ static_cast<double>(frameRate_) });
        c3d.parameter("POINT", pointRate);

        // ANALOG:RATE = frameRate as well (1 analog sample per frame).
        ezc3d::ParametersNS::GroupNS::Parameter analogRate("RATE");
        analogRate.set(std::vector<double>{ static_cast<double>(frameRate_) });
        c3d.parameter("ANALOG", analogRate);
    }

    // For each recorded frame, create a C3D frame and fill points + analogs.
    for (size_t i = 0; i < nbFrames; ++i) {
        const VRFrameDataPlain& f = frames_[i];
        const FrameValidity& fv = validity_[i];

        ezc3d::DataNS::Frame frame;

        // --- Puntos 3D ---
        {
            using ezc3d::DataNS::Points3dNS::Point;
            using ezc3d::DataNS::Points3dNS::Points;

            Points points(nbPoints);

            // Start by setting all points to (0,0,0) with residual -1 (missing marker).
            for (size_t pi = 0; pi < nbPoints; ++pi) {
                Point p;
                p.x(0.0);
                p.y(0.0);
                p.z(0.0);
                p.residual(-1.0);
                points.point(p, static_cast<int>(pi));
            }

            // Indices consistent with buildPointNames()
            const size_t IDX_HEAD        = 0;
            const size_t IDX_LFHD        = 1;
            const size_t IDX_RFHD        = 2;
            const size_t IDX_L_HAND_BASE = 3;                        // 26 joints L
            const size_t IDX_R_HAND_BASE = IDX_L_HAND_BASE + 26;     // 26 joints R
            const int FIN_JOINT_INDEX = 7; // en kHandJointNames[7] = "FIN"
            const size_t IDX_LFIN = IDX_L_HAND_BASE + FIN_JOINT_INDEX;
            const size_t IDX_RFIN = IDX_R_HAND_BASE + FIN_JOINT_INDEX;
            const size_t IDX_LWRA        = IDX_R_HAND_BASE + 26;
            const size_t IDX_LWRB        = IDX_LWRA + 1;
            const size_t IDX_RWRA        = IDX_LWRA + 2;
            const size_t IDX_RWRB        = IDX_LWRA + 3;

            // Headset pose present unless the application reported otherwise
            // (telemetry_record_frame_ex with hmdHasPose == 0). Without a pose, HEAD, LFHD
            // and RFHD stay missing (residual -1) instead of a tracked point at the origin.
            const bool hmdPresent = !fv.reported || fv.v.hmdHasPose != 0;
            if (hmdPresent) {
                // HMD (HEAD)
                {
                    float x = f.hmdPose.position[0];
                    float y = f.hmdPose.position[1];
                    float z = f.hmdPose.position[2];

                    openxrToGaitAxes(x, y, z);
                    metersToMillimeters(x, y, z);

                    Point p;
                    p.x(x);
                    p.y(y);
                    p.z(z);
                    p.residual(0.0);
                    points.point(p, static_cast<int>(IDX_HEAD));
                }

                // Extra head markers LFHD / RFHD from head width
                Vector3 headRight, headLeft;
                calculateHeadWidth(f.hmdPose, headRight, headLeft, 0.15f);

                // LFHD = left side
                openxrToGaitAxes(headLeft);
                metersToMillimeters(headLeft);
                {
                    Point p;
                    p.x(headLeft.x);
                    p.y(headLeft.y);
                    p.z(headLeft.z);
                    p.residual(0.0);
                    points.point(p, static_cast<int>(IDX_LFHD));
                }

                // RFHD = right side
                openxrToGaitAxes(headRight);
                metersToMillimeters(headRight);
                {
                    Point p;
                    p.x(headRight.x);
                    p.y(headRight.y);
                    p.z(headRight.z);
                    p.residual(0.0);
                    points.point(p, static_cast<int>(IDX_RFHD));
                }
            }

            // L_CTRL (if its valid it will have 0 joints but we do a double check)
            if (f.leftCtrl.isActive && f.leftHandJointCount==0) {
                float x = f.leftCtrl.pose.position[0];
                float y = f.leftCtrl.pose.position[1];
                float z = f.leftCtrl.pose.position[2];

                const bool isZero = (x == 0.0f && y == 0.0f && z == 0.0f);
                openxrToGaitAxes(x, y, z);
                metersToMillimeters(x, y, z);

                if (!isZero) {
                    Point p;
                    p.x(x);
                    p.y(y);
                    p.z(z);
                    p.residual(0.0);
                    points.point(p, static_cast<int>(IDX_LFIN));
                }
            }

            // R_CTRL (if active)
            if (f.rightCtrl.isActive && f.rightHandJointCount==0) {
                float x = f.rightCtrl.pose.position[0];
                float y = f.rightCtrl.pose.position[1];
                float z = f.rightCtrl.pose.position[2];
                const bool isZero = (x == 0.0f && y == 0.0f && z == 0.0f);

                openxrToGaitAxes(x, y, z);
                metersToMillimeters(x, y, z);

                if (!isZero) {
                    Point p;
                    p.x(x);
                    p.y(y);
                    p.z(z);
                    p.residual(0.0);
                    points.point(p, static_cast<int>(IDX_RFIN));
                }
            }

            // Hands: joints L
            for (int j = 0; j < f.leftHandJointCount && j < 30; ++j) {
                const auto& s = f.leftHandJoints[j];
                const int slot = jointSlot(s);
                if (slot < 0) continue;
                float x = s.px;
                float y = s.py;
                float z = s.pz;

                const bool isZero = (x == 0.0f && y == 0.0f && z == 0.0f);
                openxrToGaitAxes(x, y, z);
                metersToMillimeters(x, y, z);

                if(!isZero) {
                    Point p;
                    p.x(x);
                    p.y(y);
                    p.z(z);
                    p.residual(s.hasPose ? 0.0 : -1.0);
                    points.point(p, static_cast<int>(IDX_L_HAND_BASE + slot));
                }
            }

            // Hands: joints R
            for (int j = 0; j < f.rightHandJointCount && j < 30; ++j) {
                const auto& s = f.rightHandJoints[j];
                const int slot = jointSlot(s);
                if (slot < 0) continue;
                float x = s.px;
                float y = s.py;
                float z = s.pz;

                const bool isZero = (x == 0.0f && y == 0.0f && z == 0.0f);
                openxrToGaitAxes(x, y, z);
                metersToMillimeters(x, y, z);

                if(!isZero) {
                    Point p;
                    p.x(x);
                    p.y(y);
                    p.z(z);
                    p.residual(s.hasPose ? 0.0 : -1.0);
                    points.point(p, static_cast<int>(IDX_R_HAND_BASE + slot));
                }
            }

            // Extra wrist points LWRA/LWRB/RWRA/RWRB
            // Left hand: joint 0 = WRIST
            if (const JointSamplePlain* wristPtr = findWrist(f.leftHandJoints, f.leftHandJointCount)) {
                const auto& wrist = *wristPtr;
                if (wrist.hasPose) {
                    Vector3 wra, wrb;
                    calculateWristWidthFromJoint(wrist, wra, wrb, 0.06f);

                    const bool isZero = (wrist.px == 0.0f && wrist.py == 0.0f && wrist.pz == 0.0f);
                    openxrToGaitAxes(wra);
                    metersToMillimeters(wra);
                    openxrToGaitAxes(wrb);
                    metersToMillimeters(wrb);

                    if(!isZero){
                        Point p;
                        p.x(wra.x);
                        p.y(wra.y);
                        p.z(wra.z);
                        p.residual(0.0);
                        points.point(p, static_cast<int>(IDX_LWRA));
                    }
                    if(!isZero){
                        Point p;
                        p.x(wrb.x);
                        p.y(wrb.y);
                        p.z(wrb.z);
                        p.residual(0.0);
                        points.point(p, static_cast<int>(IDX_LWRB));
                    }
                }
            }

            // Right hand
            if (const JointSamplePlain* wristPtr = findWrist(f.rightHandJoints, f.rightHandJointCount)) {
                const auto& wrist = *wristPtr;
                if (wrist.hasPose) {
                    Vector3 wra, wrb;
                    calculateWristWidthFromJoint(wrist, wra, wrb, 0.06f);

                    const bool isZero = (wrist.px == 0.0f && wrist.py == 0.0f && wrist.pz == 0.0f);
                    openxrToGaitAxes(wra);
                    metersToMillimeters(wra);
                    openxrToGaitAxes(wrb);
                    metersToMillimeters(wrb);

                    // OJO: invertimos A/B para que RWRA/RWRB no queden cruzados
                    if(!isZero){
                        Point p;
                        p.x(wrb.x);
                        p.y(wrb.y);
                        p.z(wrb.z);
                        p.residual(0.0);
                        points.point(p, static_cast<int>(IDX_RWRA));
                    }
                    if(!isZero){
                        Point p;
                        p.x(wra.x);
                        p.y(wra.y);
                        p.z(wra.z);
                        p.residual(0.0);
                        points.point(p, static_cast<int>(IDX_RWRB));
                    }
                }
            }

            frame.points() = points;
        }

        // --- Analogs (cuaterniones + flag) ---
        {
            using ezc3d::DataNS::AnalogsNS::Channel;
            using ezc3d::DataNS::AnalogsNS::SubFrame;
            using ezc3d::DataNS::AnalogsNS::Analogs;
            const size_t nbAnalogs = analogNames.size();
            // Single subframe per frame (ANALOG:RATE == POINT:RATE)
            SubFrame subframe;
            subframe.nbChannels(nbAnalogs);

            auto setChan = [&](size_t idx, float value) {
                Channel ch;
                ch.data(value);
                subframe.channel(ch, idx);
            };

            // Every channel starts at 0 and is then written at the fixed position its
            // label has in buildAnalogNames(): HMD, then 26 slots per hand. Writing
            // sequentially instead shifted every later column whenever a hand was
            // missing or a controller quaternion was present (e.g. the left
            // controller ended up under LWRT_q*).
            for (size_t c = 0; c < nbAnalogs; ++c) setChan(c, 0.0f);

            auto setQuat = [&](size_t firstIdx, float qx, float qy, float qz, float qw) {
                if (firstIdx + 3 >= nbAnalogs) return;
                openxrToGaitAxesQuat(qx, qy, qz, qw);
                setChan(firstIdx + 0, qx);
                setChan(firstIdx + 1, qy);
                setChan(firstIdx + 2, qz);
                setChan(firstIdx + 3, qw);
            };
            const size_t ANA_HMD    = 0;
            const size_t ANA_L_BASE = 4;               // 26 joints x 4
            const size_t ANA_R_BASE = ANA_L_BASE + 26 * 4;
            const size_t FIN_SLOT   = 7;               // kHandJointNames[7] = "FIN"

            // HMD quaternion (left at 0 when the application reported no headset pose)
            if (!fv.reported || fv.v.hmdHasPose != 0) {
                setQuat(ANA_HMD, f.hmdPose.rotation[0], f.hmdPose.rotation[1],
                        f.hmdPose.rotation[2], f.hmdPose.rotation[3]);
            }

            // Controller quaternions go to the FIN slot, the same marker that holds the
            // controller position in the point data.
            if (f.leftCtrl.isActive && f.leftHandJointCount == 0) {
                setQuat(ANA_L_BASE + FIN_SLOT * 4, f.leftCtrl.pose.rotation[0], f.leftCtrl.pose.rotation[1],
                        f.leftCtrl.pose.rotation[2], f.leftCtrl.pose.rotation[3]);
            }
            if (f.rightCtrl.isActive && f.rightHandJointCount == 0) {
                setQuat(ANA_R_BASE + FIN_SLOT * 4, f.rightCtrl.pose.rotation[0], f.rightCtrl.pose.rotation[1],
                        f.rightCtrl.pose.rotation[2], f.rightCtrl.pose.rotation[3]);
            }

            // Joint quaternions, placed by joint id
            for (int j = 0; j < f.leftHandJointCount && j < 30; ++j) {
                const auto& s = f.leftHandJoints[j];
                const int slot = jointSlot(s);
                if (slot >= 0) setQuat(ANA_L_BASE + (size_t)slot * 4, s.qx, s.qy, s.qz, s.qw);
            }
            for (int j = 0; j < f.rightHandJointCount && j < 30; ++j) {
                const auto& s = f.rightHandJoints[j];
                const int slot = jointSlot(s);
                if (slot >= 0) setQuat(ANA_R_BASE + (size_t)slot * 4, s.qx, s.qy, s.qz, s.qw);
            }

            // Extra channel for real time, at the fixed index it has always had (the
            // channel after the 1 + 2 * 26 quaternions); later channels are appended.
            const size_t ANA_REALTIME = ANA_R_BASE + 26 * 4;
            if (ANA_REALTIME + 5 < nbAnalogs) {
                setChan(ANA_REALTIME, static_cast<float>(f.timestampSec));
                // RealTime_s + RealTime_frac: the double time stamp split so that float32
                // keeps it (seconds exact; the fraction to about 6e-8 s).
                const double whole = std::floor(f.timestampSec);
                setChan(ANA_REALTIME + 1, static_cast<float>(whole));
                setChan(ANA_REALTIME + 2, static_cast<float>(f.timestampSec - whole));
                // Per-device tracking bits, or -1 when they were not reported.
                setChan(ANA_REALTIME + 3, fv.reported ? static_cast<float>(fv.v.hmdFlags) : -1.0f);
                setChan(ANA_REALTIME + 4, fv.reported ? static_cast<float>(fv.v.leftFlags) : -1.0f);
                setChan(ANA_REALTIME + 5, fv.reported ? static_cast<float>(fv.v.rightFlags) : -1.0f);
            }

            Analogs analogs;
            analogs.subframe(subframe, 0);
            frame.analogs() = analogs;
        }

        // Here we finally add the frame to c3d file
        c3d.frame(frame);
    }

    // Last, write the full c3d file to disk
    c3d.write(filePath_);

    // ezc3d::write() opens the fstream and never checks is_open(), so a missing or
    // read-only output directory loses the session without any error. Confirm the
    // artifact really exists before claiming success.
    std::ifstream written(filePath_, std::ios::binary | std::ios::ate);
    const long long bytes = written.is_open() ? static_cast<long long>(written.tellg()) : -1;
    written.close();
    if (bytes <= 0) {
        LOGE("C3DRecorder: write produced no file at '%s' (directory missing or read-only?)",
             filePath_.c_str());
        return;
    }
    LOGI("C3DRecorder: file written OK (%lld bytes): '%s'", bytes, filePath_.c_str());
}
