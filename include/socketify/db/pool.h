#pragma once
/**
 * @file pool.h
 * @brief Thread-safe SQL connection pool.
 */

#include "socketify/db/engine.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

namespace socketify::db {

class Pool {
public:
    using Factory = std::function<SqlEnginePtr()>;

    Pool(Factory factory, std::size_t size)
        : factory_(std::move(factory)), size_(size ? size : 1) {
        for (std::size_t i = 0; i < size_; ++i) {
            free_.push_back(factory_());
        }
    }

    class Lease {
    public:
        Lease(Pool* pool, SqlEnginePtr eng) : pool_(pool), eng_(std::move(eng)) {}
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& o) noexcept : pool_(o.pool_), eng_(std::move(o.eng_)) {
            o.pool_ = nullptr;
        }
        ~Lease() {
            if (pool_ && eng_) pool_->release_(std::move(eng_));
        }
        SqlEngine& operator*() { return *eng_; }
        SqlEngine* operator->() { return eng_.get(); }
        SqlEnginePtr get() const { return eng_; }

    private:
        Pool* pool_{nullptr};
        SqlEnginePtr eng_;
    };

    Lease acquire() {
        std::unique_lock lk(mu_);
        cv_.wait(lk, [&] { return !free_.empty(); });
        auto e = std::move(free_.back());
        free_.pop_back();
        return Lease(this, std::move(e));
    }

    std::size_t size() const noexcept { return size_; }

private:
    void release_(SqlEnginePtr e) {
        std::lock_guard lk(mu_);
        free_.push_back(std::move(e));
        cv_.notify_one();
    }

    Factory factory_;
    std::size_t size_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<SqlEnginePtr> free_;
};

} // namespace socketify::db
