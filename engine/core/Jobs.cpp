#include "Jobs.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace eon {

namespace {
class Pool {
public:
    Pool() {
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        const unsigned n = std::min(7u, hw > 1 ? hw - 1 : 0u);   // leave a core for the main thread / GPU driver
        for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this] { Loop(); });
    }
    ~Pool() {
        { std::lock_guard<std::mutex> l(m_); quit_ = true; }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }
    unsigned Size() const { return static_cast<unsigned>(threads_.size()); }
    void Run(size_t chunks, const std::function<void(size_t)>& job) {
        {
            std::lock_guard<std::mutex> l(m_);
            job_ = &job;
            chunks_ = chunks;
            next_.store(0);
            done_.store(0);
            ++gen_;
        }
        cv_.notify_all();
        Work();   // the caller helps
        std::unique_lock<std::mutex> l(m_);
        doneCv_.wait(l, [&] { return done_.load() == chunks_; });
        job_ = nullptr;
    }

private:
    void Work() {
        for (;;) {
            const size_t c = next_.fetch_add(1);
            if (c >= chunks_) return;
            (*job_)(c);
            if (done_.fetch_add(1) + 1 == chunks_) { std::lock_guard<std::mutex> l(m_); doneCv_.notify_all(); }
        }
    }
    void Loop() {
        unsigned seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [&] { return quit_ || (gen_ != seen && job_); });
                if (quit_) return;
                seen = gen_;
            }
            Work();
        }
    }
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_, doneCv_;
    const std::function<void(size_t)>* job_ = nullptr;
    size_t chunks_ = 0;
    std::atomic<size_t> next_{0}, done_{0};
    unsigned gen_ = 0;
    bool quit_ = false;
};
Pool& P() { static Pool p; return p; }
}

unsigned WorkerCount() { return P().Size() + 1; }

void ParallelFor(size_t n, size_t minChunk, const std::function<void(size_t, size_t)>& fn) {
    if (n == 0) return;
    const size_t workers = WorkerCount();
    const size_t chunk = std::max(minChunk, (n + workers * 4 - 1) / (workers * 4));   // ~4 chunks per worker
    const size_t chunks = (n + chunk - 1) / chunk;
    if (chunks <= 1 || workers == 1) { fn(0, n); return; }
    const std::function<void(size_t)> job = [&](size_t c) { fn(c * chunk, std::min(n, (c + 1) * chunk)); };
    P().Run(chunks, job);
}

} // namespace eon
