#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace tts::framework {

template <typename E>
concept EventConcept = requires(const E& e) {
    { e.kind_name() } -> std::convertible_to<std::string_view>;
};

using SubscriptionId = std::uint64_t;

class ScopedSubscription {
public:
    ScopedSubscription() = default;
    ScopedSubscription(class EventBus* bus, SubscriptionId id) noexcept
        : bus_(bus), id_(id) {}
    ScopedSubscription(const ScopedSubscription&) = delete;
    ScopedSubscription& operator=(const ScopedSubscription&) = delete;
    ScopedSubscription(ScopedSubscription&& o) noexcept
        : bus_(o.bus_), id_(o.id_) { o.bus_ = nullptr; o.id_ = 0; }
    ScopedSubscription& operator=(ScopedSubscription&& o) noexcept {
        if (this != &o) { reset(); bus_ = o.bus_; id_ = o.id_; o.bus_ = nullptr; o.id_ = 0; }
        return *this;
    }
    ~ScopedSubscription() { reset(); }
    void reset();

private:
    EventBus* bus_{nullptr};
    SubscriptionId id_{0};
};

class EventBus {
public:
    EventBus();
    ~EventBus();

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    template <EventConcept E>
    ScopedSubscription subscribe(std::function<void(const E&)> listener) {
        return subscribe_erased(typeid(E), [fn = std::move(listener)](const void* ev) {
            fn(*static_cast<const E*>(ev));
        });
    }

    template <EventConcept E>
    void publish_sync(const E& e) {
        dispatch_sync(typeid(E), &e);
    }

    template <EventConcept E>
    void publish_async(const E& e) {
        auto copy = std::make_shared<E>(e);
        auto erased = std::make_shared<std::function<void()>>([this, copy]() {
            dispatch_sync(typeid(E), copy.get());
        });
        enqueue_async(std::move(erased));
    }

    void start_workers(std::size_t n = 1);
    void stop_workers();

    [[nodiscard]] std::size_t pending_count() const noexcept;
    [[nodiscard]] bool is_running() const noexcept;

    ScopedSubscription subscribe_erased(std::type_index type,
                                        std::function<void(const void*)> listener);

private:
    friend class ScopedSubscription;

    void dispatch_sync(std::type_index type, const void* ev);
    void enqueue_async(std::shared_ptr<std::function<void()>> task);
    void unsubscribe(SubscriptionId id);

    struct Slot {
        std::function<void(const void*)> fn;
        SubscriptionId id;
    };

    struct TypeBucket {
        mutable std::shared_mutex mtx;
        std::vector<Slot> slots;
    };

    using BucketPtr = std::shared_ptr<TypeBucket>;

    std::shared_mutex registry_mtx_;
    std::unordered_map<std::type_index, BucketPtr> registry_;
    std::atomic<SubscriptionId> next_id_{1};

    std::atomic<bool> running_{false};
    std::vector<class BusWorker*> workers_;
    void* inbox_{nullptr};
};

class BusWorker {
public:
    explicit BusWorker(void* inbox);
    ~BusWorker();
    BusWorker(const BusWorker&) = delete;
    BusWorker& operator=(const BusWorker&) = delete;

private:
    void run();
    void* inbox_;
    class Impl;
    Impl* impl_;
};

}  // namespace tts::framework