#pragma once

// Basic "plain" types for VR telemetry.
// These structs are designed to be simple C-compatible layouts that can be
// filled from Unity/Unreal and passed directly to the native telemetry API.

// Simple pose: position (meters) + orientation (quaternion).
struct VRPosePlain {
    float position[3];
    float rotation[4];
};

// Per-controller state (left or right).
struct ControllerStatePlain {
    VRPosePlain pose; //pose in tracking space
    unsigned int buttons; //button bitmask: 0x1 primary (A/X), 0x2 secondary (B/Y), 0x4 stick press
    float trigger; //analog trigger value from 0 to 1
    float grip; //analog grip value from 0 to 1
    // Analog stick
    float stickX;
    float stickY;
    int isActive;
};

// Each hand joint (there are 26 per hand with OpenXr specification).
struct JointSamplePlain {
    int   idIndex;   // joint index (see C3DRecorder.cpp or OpenXrHandJointEXT docs)
    int   state;     // flags/estado por joint
    float px, py, pz; // position
    float qx, qy, qz, qw; // rotation
    unsigned char hasPose; // 0=false 1=true
    unsigned char _pad_[3]; // padding for a 4byte alignment
};

// Complete VR frame data, one instance is equal to one real sample
struct VRFrameDataPlain {
    double timestampSec; // real time when frame was captured
    VRPosePlain hmdPose; // head mounted display pose
    ControllerStatePlain leftCtrl; // left controller
    ControllerStatePlain rightCtrl; // right controller
    // left hand joints. 30 max in case we receive more joints bc of an Unreal Engine weird implementation (only overriding default Unreal-openxr lib)
    JointSamplePlain leftHandJoints[30];
    int leftHandJointCount; // will be used to know how many empty array cells we can ignore
    JointSamplePlain rightHandJoints[30]; // right hand joints
    int rightHandJointCount;
};

// Optional per-frame tracking state, passed with telemetry_record_frame_ex next to a
// VRFrameDataPlain (whose layout stays unchanged). A frame recorded without it (plain
// telemetry_record_frame, or a null pointer) is written exactly as before.
struct VRFrameValidityPlain {
    // 0 = the headset has no pose in this frame: HEAD, LFHD and RFHD are written as
    // missing (residual -1) and the HMD_q* channels as 0. Any other value = pose present.
    int hmdHasPose;
    // The application's own per-device tracking bits, stored verbatim in the C3D analog
    // channels HMD_flags, L_flags and R_flags. They are written as float32, so values up to
    // 2^24 (bits 0-23) are exact. The library does not interpret them.
    unsigned int hmdFlags;
    unsigned int leftFlags;
    unsigned int rightFlags;
};