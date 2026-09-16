#include "MockPlatform.h"
#include <atomic>
#include <filesystem>

namespace lmc {
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

} // namespace lmc
