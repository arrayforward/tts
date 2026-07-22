#include "event_bus.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace tts::framework {

namespace {

class Inbox {
public:
    void push(std::shared_ptr<std::function<void()>> task) {
        {
            std::lock_guard lk(mtx_);
            queue_.push_back(std::move(task));
        }
        cv_.notify_one();
    }

    bool try_pop(std::shared_ptr<std::function<void()>>& out) {
        std::lock_guard lk(mtx_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    std::shared_ptr<std::function<void()>> wait_pop() {
        std::unique_lock lk(mtx_);
        cv_.wait(lk, [&] { return !queue_.empty() || stop_.stop_requested(); });
        if (queue_.empty()) return nullptr;
        auto t = std::move(queue_.front());
        queue_.pop_front();
        return t;
    }

    void close() {
        stop_.request_stop();
        cv_.notify_all();
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mtx_);
        return queue_.size();
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<std::function<void()>>> queue_;
    std::stop_source stop_;
};

}  // namespace

struct BusWorker::Impl {
    Inbox* inbox{nullptr};
    std::jthread thread;

    explicit Impl(Inbox* in) : inbox(in) {
        thread = std::jthread([this](std::stop_token st) { run_loop(st); });
    }

    void run_loop(std::stop_token st) {
        while (!st.stop_requested()) {
            std::shared_ptr<std::function<void()>> task;
            if (inbox->try_pop(task)) {
                if (task) (*task)();
                continue;
            }
            auto popped = inbox->wait_pop();
            if (!popped) return;
            if (popped) (*popped)();
        }
    }
};

BusWorker::BusWorker(void* inbox_v) {
    auto* in = static_cast<Inbox*>(inbox_v);
    impl_ = new Impl(in);
}

BusWorker::~BusWorker() {
    delete impl_;
    impl_ = nullptr;
}

void ScopedSubscription::reset() {
    if (bus_ && id_ != 0) {
        bus_->unsubscribe(id_);
        bus_ = nullptr;
        id_ = 0;
    }
}

EventBus::EventBus() : inbox_(static_cast<void*>(new Inbox())) {}

EventBus::~EventBus() {
    stop_workers();
    delete static_cast<Inbox*>(inbox_);
    inbox_ = nullptr;
}

ScopedSubscription EventBus::subscribe_erased(std::type_index type,
                                              std::function<void(const void*)> listener) {
    BucketPtr bucket;
    {
        std::shared_lock rl(registry_mtx_);
        auto it = registry_.find(type);
        if (it != registry_.end()) bucket = it->second;
    }
    if (!bucket) {
        std::unique_lock wl(registry_mtx_);
        auto it = registry_.find(type);
        if (it != registry_.end()) {
            bucket = it->second;
        } else {
            bucket = std::make_shared<TypeBucket>();
            registry_.emplace(type, bucket);
        }
    }
    SubscriptionId id = next_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::unique_lock lk(bucket->mtx);
        bucket->slots.push_back(Slot{std::move(listener), id});
    }
    return ScopedSubscription(this, id);
}

void EventBus::dispatch_sync(std::type_index type, const void* ev) {
    BucketPtr bucket;
    {
        std::shared_lock rl(registry_mtx_);
        auto it = registry_.find(type);
        if (it == registry_.end()) return;
        bucket = it->second;
    }
    std::vector<Slot> snapshot;
    {
        std::shared_lock lk(bucket->mtx);
        snapshot = bucket->slots;
    }
    for (auto& s : snapshot) {
        s.fn(ev);
    }
}

void EventBus::enqueue_async(std::shared_ptr<std::function<void()>> task) {
    auto* in = static_cast<Inbox*>(inbox_);
    in->push(std::move(task));
}

void EventBus::unsubscribe(SubscriptionId id) {
    std::shared_lock rl(registry_mtx_);
    for (auto& [_, bucket] : registry_) {
        std::unique_lock lk(bucket->mtx);
        auto& slots = bucket->slots;
        slots.erase(std::remove_if(slots.begin(), slots.end(),
                                   [id](const Slot& s) { return s.id == id; }),
                    slots.end());
    }
}

void EventBus::start_workers(std::size_t n) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    auto* in = static_cast<Inbox*>(inbox_);
    for (std::size_t i = 0; i < n; ++i) {
        workers_.push_back(new BusWorker(static_cast<void*>(in)));
    }
}

void EventBus::stop_workers() {
    if (!running_.exchange(false)) return;
    auto* in = static_cast<Inbox*>(inbox_);
    if (in) in->close();
    for (auto* w : workers_) {
        delete w;
    }
    workers_.clear();
}

std::size_t EventBus::pending_count() const noexcept {
    auto* in = static_cast<Inbox*>(inbox_);
    return in ? in->size() : 0;
}

bool EventBus::is_running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

}  // namespace tts::framework