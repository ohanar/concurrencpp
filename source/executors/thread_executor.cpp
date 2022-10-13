#include "concurrencpp/executors/constants.h"
#include "concurrencpp/executors/thread_executor.h"

using concurrencpp::thread_executor;

namespace {
    auto default_it() -> typename std::list<concurrencpp::details::thread>::iterator {
        return {};
    }
    thread_local auto s_tl_this_it = default_it();
}  // namespace

thread_executor::thread_executor() :
    derivable_executor<concurrencpp::thread_executor>(details::consts::k_thread_executor_name), m_abort(false), m_atomic_abort(false) {
}

thread_executor::~thread_executor() noexcept {
    assert(m_workers.empty());
    assert(!m_last_retired.joinable());
}

void thread_executor::enqueue_impl(std::unique_lock<std::mutex>& lock, concurrencpp::task& task) {
    assert(lock.owns_lock());

    auto& new_thread = m_workers.emplace_front();
    new_thread = details::thread(details::make_executor_worker_name(name),
                                 [this, this_it = m_workers.begin(), task = std::move(task)]() mutable {
                                     s_tl_this_it = this_it;
                                     task();
                                     if (s_tl_this_it == this_it) {
                                         retire_worker(this_it);
                                     }
                                 });
}

void thread_executor::enqueue(concurrencpp::task task) {
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_abort) {
        details::throw_runtime_shutdown_exception(name);
    }

    enqueue_impl(lock, task);
}

void thread_executor::enqueue(std::span<concurrencpp::task> tasks) {
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_abort) {
        details::throw_runtime_shutdown_exception(name);
    }

    for (auto& task : tasks) {
        enqueue_impl(lock, task);
    }
}

int thread_executor::max_concurrency_level() const noexcept {
    return details::consts::k_thread_executor_max_concurrency_level;
}

bool thread_executor::shutdown_requested() const {
    return m_atomic_abort.load(std::memory_order_relaxed);
}

void thread_executor::shutdown() {
    const auto abort = m_atomic_abort.exchange(true, std::memory_order_relaxed);
    if (abort) {
        return;  // shutdown had been called before.
    }

    std::unique_lock<std::mutex> lock(m_lock);
    m_abort = true;
    m_condition.wait(lock, [this] {
        return m_workers.empty() || (m_workers.size() == 1 && m_workers.begin() == s_tl_this_it);
    });

    if (m_last_retired.joinable()) {
        m_last_retired.join();
    }

    if (m_workers.begin() == s_tl_this_it) {
        assert(m_workers.size() == 1);
        std::exchange(s_tl_this_it, default_it())->detach();
        m_workers.clear();
    }
}

void thread_executor::retire_worker(std::list<details::thread>::iterator it) {
    std::unique_lock<std::mutex> lock(m_lock);
    auto last_retired = std::exchange(m_last_retired, std::move(*it));
    m_workers.erase(it);

    lock.unlock();
    m_condition.notify_one();

    if (last_retired.joinable()) {
        last_retired.join();
    }
}
