#include "MockPlatform.h"
#include <atomic>
#include <chrono>
#include <filesystem>

namespace lmc {

void PostGate::hold(int n) { std::lock_guard<std::mutex> lk(mtx_); toHold_ = n; }

void PostGate::enter() {
    std::unique_lock<std::mutex> lk(mtx_);
    if (toHold_ == 0) return;
    --toHold_;
    ++inside_;
    cv_.notify_all();
    cv_.wait(lk, [this] { return inside_ == 0; });
}

bool PostGate::waitUntilInside() {
    std::unique_lock<std::mutex> lk(mtx_);
    return cv_.wait_for(lk, std::chrono::seconds(5), [this] { return inside_ > 0; });
}

void PostGate::release() {
    std::unique_lock<std::mutex> lk(mtx_);
    inside_ = 0;
    cv_.notify_all();
}
namespace {
std::string makeTempDir() {
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path();
    static std::atomic<unsigned> seq{0};
    for (;;) {
        std::error_code ec;
        fs::path cand = base / ("lmc_c3d_test_" + std::to_string(seq.fetch_add(1)));
        if (fs::create_directories(cand, ec) && !ec) return cand.string();
    }
}
} // namespace

WinTempDir::WinTempDir() : dir_(makeTempDir()), resolver_(dir_) {}

IPathResolver* WinTempDir::paths() { return &resolver_; }

std::string WinTempDir::c3dPath() {
    std::string out;
    resolver_.c3dFilePath("s1", out);
    return out;
}

bool CaptureLogSink::errorMentions(const std::string& needle) const {
    for (size_t i = 0; i < errors.size(); ++i)
        if (errors[i].find(needle) != std::string::npos) return true;
    return false;
}

bool CaptureLogSink::infoMentions(const std::string& needle) const {
    for (size_t i = 0; i < infos.size(); ++i)
        if (infos[i].find(needle) != std::string::npos) return true;
    return false;
}

static ILogSink* g_savedSink = nullptr;

ScopedCaptureLog::ScopedCaptureLog() {
    g_savedSink = nullptr;   // nullptr means "the built-in stderr sink"
    Log::set(&sink_);
}

ScopedCaptureLog::~ScopedCaptureLog() { Log::set(g_savedSink); }

} // namespace lmc
