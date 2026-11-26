#include "skeet/lagcomp/history_jobs.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <new>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <immintrin.h>
#endif

namespace skeet::lagcomp {
namespace {
std::uint32_t add_word(std::uint32_t& target, std::uint32_t value) {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(InterlockedExchangeAdd(
        reinterpret_cast<volatile long*>(&target), static_cast<long>(value)));
#else
    return __atomic_fetch_add(&target, value, __ATOMIC_SEQ_CST);
#endif
}
std::uint32_t load_word(std::uint32_t& target) {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(
        InterlockedCompareExchange(reinterpret_cast<volatile long*>(&target), 0, 0));
#else
    return __atomic_load_n(&target, __ATOMIC_SEQ_CST);
#endif
}
std::uintptr_t add_pointer(std::uintptr_t& target, std::uintptr_t value) {
#if defined(_WIN64)
    return static_cast<std::uintptr_t>(InterlockedExchangeAdd64(
        reinterpret_cast<volatile long long*>(&target), static_cast<long long>(value)));
#elif defined(_WIN32)
    return static_cast<std::uintptr_t>(InterlockedExchangeAdd(
        reinterpret_cast<volatile long*>(&target), static_cast<long>(value)));
#else
    return __atomic_fetch_add(&target, value, __ATOMIC_SEQ_CST);
#endif
}
std::uintptr_t load_pointer(std::uintptr_t& target) {
#if defined(_WIN32)
    return reinterpret_cast<std::uintptr_t>(InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(&target), nullptr, nullptr));
#else
    return __atomic_load_n(&target, __ATOMIC_SEQ_CST);
#endif
}
void store_pointer(std::uintptr_t& target, std::uintptr_t value) {
#if defined(_WIN32)
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&target),
                               reinterpret_cast<void*>(value));
#else
    __atomic_exchange_n(&target, value, __ATOMIC_SEQ_CST);
#endif
}
#if defined(_M_IX86) || defined(__i386__)
#define HISTORY_JOB_MEMBER __thiscall
#else
#define HISTORY_JOB_MEMBER
#endif
std::int32_t HISTORY_JOB_MEMBER retain_entry(void* task) {
    return engine::retain_job(*static_cast<engine::JobTask*>(task));
}
std::int32_t HISTORY_JOB_MEMBER release_entry(void* task) {
    return engine::release_job(*static_cast<engine::JobTask*>(task));
}
void HISTORY_JOB_MEMBER consume_entry(void* task) {
    consume_history_jobs(*static_cast<engine::JobTask*>(task));
}
}

void initialize_history_job_queue(HistoryJobQueue& queue) noexcept {
    queue.begin = queue.items.data();
    queue.end = queue.items.data();
    queue.limit = queue.items.data() + queue.items.size();
}
std::uint32_t history_job_count(const HistoryJobQueue& queue) noexcept {
    const auto difference = std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(queue.end) -
                                   reinterpret_cast<std::uintptr_t>(queue.begin)));
    return static_cast<std::uint32_t>(difference / 12);
}
bool append_history_job(HistoryJobQueue& queue, bool asynchronous, void* player,
                        float speed_fraction, LagRecord& record) noexcept {
    if (!asynchronous || history_job_count(queue) >= 64 || queue.begin != queue.items.data())
        return false;
    // Complete4341F0AD reserves12 bytes and ignores its stack argument.
    auto* item = queue.end++;
    item->player = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player));
    item->speed_fraction = speed_fraction;
    item->record = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&record));
    return true;
}

void consume_history_jobs(engine::JobTask& task) {
    const auto end = task.end;
    auto position = load_pointer(task.cursor);
    while (position < end) {
        position = add_pointer(task.cursor, 12);
        const auto next = position + 12;
        if (position < std::min(end, next)) {
            task.work(reinterpret_cast<void*>(position));
            add_word(task.completed, 1);
            position = next;
        }
    }
    engine::invoke<std::int32_t>(&task, 1);
}

HistoryJobDispatcher::HistoryJobDispatcher(void* thread_pool, engine::JobMemory memory)
    : table_{}, destroyed_{}, pool_(thread_pool), memory_(memory) {
    if (!pool_ || !memory_.allocate || !memory_.free || !memory_.purecall)
        throw std::invalid_argument("History jobs require the actual thread pool and heap exports");
    table_ = {
        {{reinterpret_cast<void*>(&retain_entry), reinterpret_cast<void*>(&release_entry),
          reinterpret_cast<void*>(&destruction_entry), reinterpret_cast<void*>(&consume_entry)}},
        this};
    destroyed_ = {memory_.purecall, memory_.purecall, reinterpret_cast<void*>(&destruction_entry),
                  memory_.purecall};
}
engine::JobTask* HistoryJobDispatcher::create() {
    const auto size = sizeof(engine::JobTask);
    auto* allocation = memory_.allocate(memory_.process_heap, 8, size + 0x20);
    if (!allocation)
        return nullptr;
    const auto raw = reinterpret_cast<std::uintptr_t>(allocation);
    const auto header = (raw + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - raw);
    const auto allocation_size = static_cast<std::uint32_t>(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    // Complete4341F142: the supplied native argument is ignored; the worker
    // is always4336A5B3, represented by the canonical concrete source entry.
    return new (reinterpret_cast<void*>(header + 16))
        engine::JobTask{table_.methods.data(), 0, 1, &execute_history_record_job, 0, 0, 0};
}
engine::JobTask* HistoryJobDispatcher::destroy(engine::JobTask& task, std::uint8_t flags) {
    const auto* table = reinterpret_cast<TaskTable*>(task.vtable);
    auto& owner = *table->owner;
    task.vtable = owner.destroyed_.data();
    if (flags & 1) {
        auto* object = reinterpret_cast<std::byte*>(&task);
        std::uint32_t displacement;
        std::memcpy(&displacement, object - 8, 4);
        owner.memory_.free(owner.memory_.process_heap, 0, object - 16 - displacement);
    }
    return &task;
}
engine::JobTask* HISTORY_JOB_MEMBER HistoryJobDispatcher::destruction_entry(void* object,
                                                                            std::uint8_t flags) {
    return destroy(*static_cast<engine::JobTask*>(object), flags);
}

void HistoryJobDispatcher::dispatch(engine::JobTask& task, HistoryJobItem* items,
                                    std::uint32_t count) {
    if (!count)
        return;
    const auto first = reinterpret_cast<std::uintptr_t>(items);
    store_pointer(task.cursor, first);
    task.end = first + static_cast<std::uintptr_t>(count) * 12;
    const auto available = engine::invoke<std::int32_t>(pool_, 8);
    auto workers = std::min(static_cast<std::int32_t>(count - 1), available);
    if (workers <= 0) {
        engine::invoke<std::int32_t>(&task, 0);
        consume_history_jobs(task);
        return;
    }
    workers = std::min(workers, 32);
    std::array<void*, 32> handles{};
    for (std::int32_t i = 0; i < workers; ++i) {
        void* handle = nullptr;
        engine::invoke<void, engine::JobTask*, void**, int, int>(pool_, 15, &task, &handle, 0, 8);
        if (handle) {
            engine::invoke<std::int32_t>(&task, 0);
            handles[static_cast<std::size_t>(i)] = handle;
        }
    }
    engine::invoke<std::int32_t>(&task, 0);
    consume_history_jobs(task);
    for (std::int32_t i = 0; i < workers; ++i)
        if (auto* handle = handles[static_cast<std::size_t>(i)])
            engine::invoke<void>(handle, 1);
    while (load_word(task.completed) != count) {
#if defined(_WIN32)
        _mm_pause();
#else
        __builtin_ia32_pause();
#endif
    }
}
void dispatch_history_jobs(HistoryJobQueue& queue, HistoryJobDispatcher& dispatcher) {
    if (queue.begin == queue.end)
        return;
    auto* task = dispatcher.create();
    dispatcher.dispatch(*task, queue.begin, history_job_count(queue));
    engine::release_job(*task);
}

#undef HISTORY_JOB_MEMBER
} // namespace skeet::lagcomp
