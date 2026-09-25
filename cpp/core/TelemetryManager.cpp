#include "TelemetryManager.h"
#include "platform/Interfaces.h"
#include "platform/Log.h"
#include "HttpHeaders.h"
#include <sstream>
#include <chrono>
#include <deque>
#include <condition_variable>
#include <thread>
#include <cmath>
#include <cstdio>
#include <iomanip>

// Bit masks used for controller button states. The full set of bits, including the
// stick-press bit (0x4) which is not serialized, is documented on ControllerStatePlain.
static constexpr unsigned BTN_PRIMARY   = 0x1u; // A/X
static constexpr unsigned BTN_SECONDARY = 0x2u; // B/Y

GestorTelemetria::GestorTelemetria() {}
// Destructor, is a safety fallback in case shutdown() was not called explicitly.
GestorTelemetria::~GestorTelemetria() {
    // Signal stop to the worker thread.
    {
        std::lock_guard<std::mutex> lk(qmtx_);
        if (!stopWorker_) {
            stopWorker_ = true;
            qcv_.notify_all();
        }
    }
    // Wait for the worker thread to finish if it was running.
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool GestorTelemetria::initialize(const UploaderConfig& cfg, lmc::IHttpTransport* transport) {
        {
            // Initialize front buffer and configuration used by the producer.
            std::lock_guard <std::mutex> lock(mtx_);
            cfg_ = cfg;
            transport_ = transport;
            buffer_.clear();
            framesCount_ = 0;
            sessionId_ = cfg.sessionId;
            deviceInfo_ = cfg.deviceInfo;
            // Reserve space to minimize reallocations and reduce the risk of
            // losing frames due to allocations at high frequency.
            buffer_.reserve(cfg_.framesPerFile);
        }
    // --- Start background worker thread ---
    // Double initialize() without a matching shutdown(): the old worker is still
    // joinable and assigning to it would call std::terminate. Signal stop first so
    // the join drains it instead of hanging forever, then start fresh.
    {
        std::lock_guard<std::mutex> lk(qmtx_);
        if (worker_.joinable()) {
            stopWorker_ = true;
            qcv_.notify_all();
        }
    }
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lk(qmtx_);
        stopWorker_ = false;
        queue_.clear();
    }
    chunkIndex_ = 0;
    // Launch the worker thread which will run workerLoop().
    worker_ = std::thread(&GestorTelemetria::workerLoop, this);
    return true;
}

void GestorTelemetria::recordFrame(const VRFrameDataPlain& frame) {
    //when buffer reach end it will hold a complete chunk to enqueue AFTER releasing the mutex
    std::vector<VRFrameDataPlain> chunk;
    {
        std::lock_guard <std::mutex> lock(mtx_);
        buffer_.push_back(frame);
        framesCount_++;
        // When we reach framesPerFile, change the buffer into a new chunk.
        if (cfg_.framesPerFile > 0 && framesCount_ >= cfg_.framesPerFile) {
            chunk.swap(buffer_);
            framesCount_ = 0;
            buffer_.reserve(cfg_.framesPerFile);
        }
    }
    // If we produced a full chunk, enqueue it for asynchronous upload.
    if (!chunk.empty()) {
        std::unique_lock<std::mutex> lk(qmtx_);
        // Simple backpressure: if the queue is too large, drop the oldest chunk (as said before this should never happen).
        if (queue_.size() >= maxQueuedChunks_) {
            queue_.pop_front();
        }
        queue_.push_back(std::move(chunk));
        lk.unlock();
        // Wake up the worker thread so it can process the new chunk.
        qcv_.notify_one();
    }
}

void GestorTelemetria::flushAndUpload() {
    // Move any frames currently in buffer_ into a chunk and enqueue it,
    // even if we did not reach framesPerFile.
    std::vector<VRFrameDataPlain> chunk;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (buffer_.empty()) return;
        chunk.swap(buffer_);
        framesCount_ = 0;
        buffer_.reserve(cfg_.framesPerFile);
    }
    // Enqueue the partially-filled chunk as if it were a complete one.
    std::unique_lock<std::mutex> lk(qmtx_);
    if (queue_.size() >= maxQueuedChunks_) {
        queue_.pop_front(); // drop oldest chunk if queue is too large (again not happening BUT)
    }
    queue_.push_back(std::move(chunk));
    lk.unlock();
    qcv_.notify_one();
}

void GestorTelemetria::shutdown() {
    // First, flush remaining frames into the queue.
    flushAndUpload();
    // Then, signal the worker thread to stop and wake it up.
    {
        std::lock_guard<std::mutex> lk(qmtx_);
        stopWorker_ = true;
    }
    qcv_.notify_all();
    // Wait for the worker to finish any pending uploads.
    if (worker_.joinable()) worker_.join();
}

// Numbers go through JsonNum so a NaN/Inf coming from the engine (e.g. an
// uninitialised pose) is written as null instead of "nan"/"inf", which would make
// the whole chunk unparseable and the server reject it.
struct JsonNum { double v; };
static std::ostream& operator<<(std::ostream& os, JsonNum n) {
    if (std::isfinite(n.v)) os << n.v; else os << "null";
    return os;
}

// Session id and device info come from the engine (product name, device model),
// so quotes, backslashes and control characters have to be escaped.
static void writeJsonString(std::ostream& os, const std::string& s) {
    os << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    os << buf;
                } else {
                    os << static_cast<char>(c);
                }
        }
    }
    os << '"';
}

static void writeController(std::ostream& os, const ControllerStatePlain& c, const UploaderConfig& cfgFlags) {
    os << "{";
    os << "\"tracked\":" << (c.isActive ? "true" : "false") << ",";
    os << "\"orientation\":[" << JsonNum{c.pose.rotation[0]} << "," << JsonNum{c.pose.rotation[1]} << ","
       << JsonNum{c.pose.rotation[2]} << "," << JsonNum{c.pose.rotation[3]} << "],";
    os << "\"position\":[" << JsonNum{c.pose.position[0]} << "," << JsonNum{c.pose.position[1]} << ","
       << JsonNum{c.pose.position[2]} << "]";

    // Conditional fields, only when enabled in initialConfig.json
    if (cfgFlags.trigger) {
        os << ",\"trigger\":" << JsonNum{c.trigger};
    }
    if (cfgFlags.grip) {
        os << ",\"grip\":" << JsonNum{c.grip};
    }
    if (cfgFlags.primaryButton) {
        os << ",\"primary\":" << (((c.buttons & BTN_PRIMARY) != 0u) ? "true" : "false");
    }
    if (cfgFlags.secondaryButton) {
        os << ",\"secondary\":" << (((c.buttons & BTN_SECONDARY) != 0u) ? "true" : "false");
    }
    if (cfgFlags.joystick) {
        os << ",\"joystick_x\":" << JsonNum{c.stickX};
        os << ",\"joystick_y\":" << JsonNum{c.stickY};
    }
    os << "}";
}

static void writeHand(std::ostream& os, const JointSamplePlain* joints, int count) {
    os << "{";
    os << "\"joint_count\":" << count << ",";
    os << "\"joints\":[";
    for (int j = 0; j < count && j < 30; ++j) {
        const auto& s = joints[j];
        if (j) os << ",";
        os << "{"
           << "\"id\":" << s.idIndex
           << ",\"state\":" << s.state
           << ",\"orientation\":[" << JsonNum{s.qx} << "," << JsonNum{s.qy} << "," << JsonNum{s.qz} << "," << JsonNum{s.qw} << "]"
           << ",\"position\":[" << JsonNum{s.px} << "," << JsonNum{s.py} << "," << JsonNum{s.pz} << "]"
           << ",\"has_pose\":" << (s.hasPose ? "true":"false")
           << "}";
    }
    os << "]";
    os << "}";
}

// Serialize a sequence of frames into the flat JSON format.
// The resulting JSON is an array of frame objects, each with head_pose, controllers (left/right) or optional hand joint data.
static std::string toJsonFlat(const std::vector<VRFrameDataPlain>& frames, const std::string& sessionId, const std::string& deviceInfo, const UploaderConfig& cfgFlags) {
    std::ostringstream os;
    // The stream's default 6 significant digits are fine for poses (1e-5 m) but not
    // for the timestamp: after 1000 s it would be rounded to 10 ms, less than one
    // frame at 72-90 Hz, so consecutive frames would share the same timestamp.
    const std::streamsize poseDigits = os.precision();
    os << "[";
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& f = frames[i];
        os << "{";
        os << "\"session_id\":"; writeJsonString(os, sessionId); os << ",";
        os << std::setprecision(15) << "\"timestamp\":" << JsonNum{f.timestampSec} << std::setprecision(poseDigits) << ",";
        os << "\"device_info\":"; writeJsonString(os, deviceInfo); os << ",";

        // Estructura para head pose
        os << "\"head_pose\":{";
        os << "\"orientation\":[" << JsonNum{f.hmdPose.rotation[0]} << "," << JsonNum{f.hmdPose.rotation[1]} << ","
           << JsonNum{f.hmdPose.rotation[2]} << "," << JsonNum{f.hmdPose.rotation[3]} << "],";
        os << "\"position\":[" << JsonNum{f.hmdPose.position[0]} << "," << JsonNum{f.hmdPose.position[1]} << ","
           << JsonNum{f.hmdPose.position[2]} << "]";
        os << "},";

        // Estructura para controladores
        os << "\"controllers\":{";
        os << "\"left\":";
        writeController(os, f.leftCtrl, cfgFlags);
        os << ",\"right\":";
        writeController(os, f.rightCtrl, cfgFlags);
        os << "}";  // cierra controllers

        // Joints de las manos. The separator is written here, not after the
        // controllers block, so a frame without hands does not end in ",}".
        if (cfgFlags.handTracking) {
            os << ",\"hands\":{";
            os << "\"left\":";
            writeHand(os, f.leftHandJoints, f.leftHandJointCount);
            os << ",\"right\":";
            writeHand(os, f.rightHandJoints, f.rightHandJointCount);
            os << "}"; // cierra hands
        }

        os << "}"; // cierra el objeto frame completo
        if (i + 1 < frames.size()) os << ",";
    }
    os << "]";
    return os.str();
}


void GestorTelemetria::serializeAndSend(const std::vector<VRFrameDataPlain>& chunk) {
    const bool saveLocal = !cfg_.jsonOutputPrefix.empty();
    if (!transport_ && !saveLocal) return;
    // Convert the chunk into JSON according to the configured feature flags
    const std::string json = toJsonFlat(chunk, sessionId_, deviceInfo_, cfg_);
    // Local copy first, so it exists even when the upload fails or is disabled.
    if (saveLocal) {
        char suffix[32];
        std::snprintf(suffix, sizeof(suffix), "_chunk%04d.json", ++chunkIndex_);
        const std::string path = cfg_.jsonOutputPrefix + suffix;
        if (lmc::writeStringToFile(path, json)) {
            lmc::Log::info("Saved chunk locally, frames: %zu, path: %s", chunk.size(), path.c_str());
        } else {
            lmc::Log::err("Could not save chunk locally at '%s' (directory missing or read-only?)", path.c_str());
        }
    }
    if (!transport_) return;
    // Perform the HTTP upload via the platform transport.
    const auto headers = lmc::buildSupabaseHeaders(cfg_.apiKey);
    bool ok = transport_->postJson(cfg_.endpointUrl, json, headers);
    if (ok) {
        lmc::Log::info("Uploaded chunk, frames: %zu", chunk.size());
    } else {
        lmc::Log::err("Upload FAILED, frames: %zu", chunk.size());
    }
}

void GestorTelemetria::workerLoop() {
    for (;;) {
        std::vector<VRFrameDataPlain> chunk;
        {
            // Wait until there is work to do or a stop signal.
            std::unique_lock<std::mutex> lk(qmtx_);
            qcv_.wait(lk, [&]{ return stopWorker_ || !queue_.empty(); });
            if (stopWorker_ && queue_.empty()) break; // Stop requested and no more chunks to process.
            chunk = std::move(queue_.front());
            queue_.pop_front();
        }
        auto t0 = std::chrono::steady_clock::now();

        // Serialize and upload outside of the queue lock.
        serializeAndSend(chunk);

        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        lmc::Log::info("worker: subido chunk de %zu frames en %lld ms",
                       chunk.size(), (long long)ms);
    }
}