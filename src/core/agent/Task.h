#pragma once

#include <coroutine>
#include <variant>
#include <optional>
#include <exception>
#include <utility>

namespace NexusAI {

// ── Basic C++20 coroutine Task<T> ─────────────────────────
// A lightweight coroutine task type for async Agent operations.

template <typename T = void>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::variant<std::monostate, T, std::exception_ptr> result;

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(T value) {
            result.template emplace<T>(std::move(value));
        }
        void unhandled_exception() {
            result = std::current_exception();
        }
    };

    Task() = default;
    explicit Task(handle_type h) : m_handle(h) {}
    Task(Task&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (m_handle) m_handle.destroy();
            m_handle = std::exchange(other.m_handle, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { if (m_handle) m_handle.destroy(); }

    bool isReady() const { return m_handle && m_handle.done(); }

    T result() {
        if (!m_handle || !m_handle.done())
            return T{};

        if (std::holds_alternative<std::exception_ptr>(m_handle.promise().result))
            std::rethrow_exception(std::get<std::exception_ptr>(m_handle.promise().result));

        if (std::holds_alternative<T>(m_handle.promise().result))
            return std::get<T>(std::move(m_handle.promise().result));

        return T{};
    }

    void resume() {
        if (m_handle && !m_handle.done())
            m_handle.resume();
    }

    // Awaitable: allows co_await on this Task
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> caller) noexcept {
        if (m_handle) {
            m_handle.resume();
            if (m_handle.done())
                caller.resume();
        }
    }
    T await_resume() { return result(); }

private:
    handle_type m_handle{nullptr};
};

// ── void specialization ──────────────────────────────────
template <>
class Task<void> {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::exception_ptr exception;

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { exception = std::current_exception(); }
    };

    Task() = default;
    explicit Task(handle_type h) : m_handle(h) {}
    Task(Task&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Task(const Task&) = delete;
    ~Task() { if (m_handle) m_handle.destroy(); }

    void resume() {
        if (m_handle && !m_handle.done())
            m_handle.resume();
    }

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> caller) noexcept {
        if (m_handle) {
            m_handle.resume();
            if (m_handle.done())
                caller.resume();
        }
    }
    void await_resume() {}

private:
    handle_type m_handle{nullptr};
};

} // namespace NexusAI
