// One background thread for the share page (docs/config-sharing-design.md §2, §5.1-5.3):
// decoding + checking + diffing a pasted code, and backing up myclient_config.txt.
// It never touches the settings variables; the UI thread takes results with TryTakeResult().
// Uses std::filesystem only (no <windows.h>), so it is tested on Linux too.
#pragma once

#include "Code.h"
#include "Snapshot.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace ConfigShare {

constexpr size_t KeepBackups = 5;

struct DecodeRequest {
    std::string text;
    Snapshot now;
};

struct BackupRequest {
    std::filesystem::path config;      // myclient_config.txt
    std::filesystem::path backupDir;   // <exe folder>/config_backups
};

struct WorkerResult {
    enum class Type : uint8_t { Decode, Backup } type = Type::Decode;
    DecodeResult decoded;   // Decode
    DiffResult diff;        // Decode (against `now`, every category the code carries)
    bool backupOk = false;  // Backup
    std::filesystem::path backupFile;   // Backup: the copy made (empty when there was no config yet)
};

// §5.3: copy the config to config_backups/myclient_config-YYYYMMDD-HHMMSS.txt, keep the newest 5.
// No config yet (first start) counts as success. Any failure to copy is a failure.
inline bool BackupConfig(const BackupRequest& req, std::filesystem::path* made = nullptr, std::time_t now = std::time(nullptr))
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (made) made->clear();
    if (!fs::exists(req.config, ec)) return !ec;
    fs::create_directories(req.backupDir, ec);
    if (ec) return false;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    fs::path target = req.backupDir / ("myclient_config-" + std::string(stamp) + ".txt");
    for (int n = 2; fs::exists(target, ec) && n < 100; ++n)   // two backups in one second
    {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "-%02d", n);   // zero-padded so -02 .. -99 sort in order
        target = req.backupDir / ("myclient_config-" + std::string(stamp) + suffix + ".txt");
    }
    fs::copy_file(req.config, target, fs::copy_options::none, ec);
    if (ec) return false;
    if (made) *made = target;

    // Prune: keep the newest 5. Compared by the name without ".txt", so "...-120000" comes
    // before "...-120000-02" (with the extension, '-' < '.' would put the second copy first).
    std::vector<fs::path> backups;
    for (const auto& entry : fs::directory_iterator(req.backupDir, ec)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file(ec) && name.rfind("myclient_config-", 0) == 0 && entry.path().extension() == ".txt")
            backups.push_back(entry.path());
    }
    std::sort(backups.begin(), backups.end(),
              [](const fs::path& a, const fs::path& b) { return a.stem().string() < b.stem().string(); });
    while (backups.size() > KeepBackups) {
        fs::remove(backups.front(), ec);   // a copy that cannot be removed is left; the backup itself succeeded
        backups.erase(backups.begin());
    }
    return true;
}

inline WorkerResult RunDecode(const DecodeRequest& req)
{
    WorkerResult r;
    r.type = WorkerResult::Type::Decode;
    r.decoded = DecodeCode(req.text);
    if (r.decoded.ok()) r.diff = Diff(req.now, r.decoded.snapshot, r.decoded.categories);
    return r;
}

class Worker {
public:
    Worker() = default;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    // Called on the worker thread after each result is stored (e.g. to repaint the UI).
    void SetOnDone(std::function<void()> f)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        onDone_ = std::move(f);
    }

    // A new request replaces one that has not started yet. False while one is running.
    bool Decode(DecodeRequest req) { return Post(Job{std::move(req), {}, true}); }
    bool Backup(BackupRequest req) { return Post(Job{{}, std::move(req), false}); }

    bool Busy()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return job_.has_value() || running_;
    }

    // UI thread: never waits. Oldest finished result first; none is dropped, so a backup
    // result is still there when a paste finishes before the UI took it.
    std::optional<WorkerResult> TryTakeResult()
    {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || results_.empty()) return std::nullopt;
        std::optional<WorkerResult> out = std::move(results_.front());
        results_.pop_front();
        return out;
    }

private:
    struct Job {
        DecodeRequest decode;
        BackupRequest backup;
        bool isDecode;
    };

    bool Post(Job job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (running_) return false;
            job_ = std::move(job);
            if (!thread_.joinable()) thread_ = std::thread([this] { Loop(); });   // started on first use
        }
        wake_.notify_all();
        return true;
    }

    void Loop()
    {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stop_ || job_.has_value(); });
                if (stop_) return;
                job = std::move(*job_);
                job_.reset();
                running_ = true;
            }
            WorkerResult r;
            if (job.isDecode) {
                r = RunDecode(job.decode);
            } else {
                r.type = WorkerResult::Type::Backup;
                r.backupOk = BackupConfig(job.backup, &r.backupFile);
            }
            std::function<void()> done;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                results_.push_back(std::move(r));
                running_ = false;
                done = onDone_;
            }
            if (done) done();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    std::optional<Job> job_;
    std::deque<WorkerResult> results_;
    std::function<void()> onDone_;
    bool running_ = false;
    bool stop_ = false;
};

} // namespace ConfigShare
