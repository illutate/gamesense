#include "skeet/engine/jobs.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <immintrin.h>
// Microsoft documents this exact CRT export without a public header declaration:
// https://learn.microsoft.com/cpp/c-runtime-library/reference/purecall
extern "C" int __cdecl _purecall();
#endif

namespace skeet::engine {
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
void consume(JobTask& task, std::uint32_t stride) {
    const auto end = task.end;
    auto position = load_pointer(task.cursor);
    while (position < end) {
        position = add_pointer(task.cursor, stride);
        const auto next = position + stride;
        if (position < std::min(end, next)) {
            task.work(reinterpret_cast<void*>(position));
            add_word(task.completed, 1);
            position = next;
        }
    }
    invoke<std::int32_t>(&task, 1);
}
#if defined(_M_IX86) || defined(__i386__)
#define TASK_MEMBER __thiscall
#else
#define TASK_MEMBER
#endif
std::int32_t TASK_MEMBER retain_entry(void* task) {
    return retain_job(*static_cast<JobTask*>(task));
}
std::int32_t TASK_MEMBER release_entry(void* task) {
    return release_job(*static_cast<JobTask*>(task));
}
void TASK_MEMBER consume_eight_entry(void* task) {
    consume_eight_byte_jobs(*static_cast<JobTask*>(task));
}
void TASK_MEMBER consume_thirty_six_entry(void* task) {
    consume_thirty_six_byte_jobs(*static_cast<JobTask*>(task));
}
}

std::int32_t retain_job(JobTask& task) {
    return static_cast<std::int32_t>(add_word(task.references, 1) + 1);
}
std::int32_t release_job(JobTask& task) {
    const auto remaining = add_word(task.references, 0xffffffff) - 1;
    if (remaining == 0)
        invoke<JobTask*, std::uint8_t>(&task, 2, 1);
    return static_cast<std::int32_t>(remaining);
}
void consume_eight_byte_jobs(JobTask& task) {
    consume(task, 8);
}
void consume_thirty_six_byte_jobs(JobTask& task) {
    consume(task, 0x24);
}

JobMemory native_job_memory() {
#if defined(_WIN32)
    const auto native = GetModuleHandleA("ntdll.dll");
    if (!native)
        throw std::runtime_error("ntdll.dll is not loaded");
    const auto allocate = GetProcAddress(native, "RtlAllocateHeap");
    const auto free = GetProcAddress(native, "RtlFreeHeap");
    if (!allocate || !free)
        throw std::runtime_error("Native job memory export is missing");
    // Bind this source build's CRT directly; prior loading of a different CRT
    // module is not a requirement of source-owned process-heap storage.
    return {reinterpret_cast<JobMemory::AllocateHeap>(allocate),
            reinterpret_cast<JobMemory::FreeHeap>(free), GetProcessHeap(),
            reinterpret_cast<void*>(&_purecall)};
#else
    throw std::runtime_error("Native job memory requires Windows");
#endif
}
void* native_thread_pool() {
#if defined(_WIN32)
    const auto library = GetModuleHandleA("vstdlib.dll");
    if (!library)
        throw std::runtime_error("vstdlib.dll is not loaded");
    const auto symbol = GetProcAddress(library, "g_pThreadPool");
    if (!symbol)
        throw std::runtime_error("vstdlib!g_pThreadPool export is missing");
    auto* pool = *reinterpret_cast<void**>(symbol);
    if (!pool)
        throw std::runtime_error("vstdlib!g_pThreadPool is null");
    return pool;
#else
    throw std::runtime_error("Native thread pool requires Windows");
#endif
}

JobDispatcher::JobDispatcher(void* thread_pool, JobMemory memory)
    : eight_{}, thirty_six_{}, destroyed_{}, pool_(thread_pool), memory_(memory) {
    if (!pool_ || !memory_.allocate || !memory_.free || !memory_.purecall)
        throw std::invalid_argument(
            "Job dispatcher requires the actual thread pool and heap exports");
    const auto destructor = &destruction_entry;
    eight_ = {
        {{reinterpret_cast<void*>(&retain_entry), reinterpret_cast<void*>(&release_entry),
          reinterpret_cast<void*>(destructor), reinterpret_cast<void*>(&consume_eight_entry)}},
        this};
    thirty_six_ = {
        {{reinterpret_cast<void*>(&retain_entry), reinterpret_cast<void*>(&release_entry),
          reinterpret_cast<void*>(destructor), reinterpret_cast<void*>(&consume_thirty_six_entry)}},
        this};
    destroyed_ = {memory_.purecall, memory_.purecall, reinterpret_cast<void*>(destructor),
                  memory_.purecall};
}

JobTask* JobDispatcher::create(JobWork work, TaskTable& table) {
    // Complete43387448 allocation shape, specialized to native job size0x1C.
    const auto size = sizeof(JobTask);
    auto* allocation = memory_.allocate(memory_.process_heap, 8, size + 0x20);
    if (!allocation)
        return nullptr;
    const auto raw = reinterpret_cast<std::uintptr_t>(allocation);
    const auto header = (raw + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - raw);
    const auto allocation_size = static_cast<std::uint32_t>(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return new (reinterpret_cast<void*>(header + 16))
        JobTask{table.methods.data(), 0, 1, work, 0, 0, 0};
}
JobTask* JobDispatcher::create_eight(JobWork work) {
    return create(work, eight_);
}
JobTask* JobDispatcher::create_thirty_six(JobWork work) {
    return create(work, thirty_six_);
}
JobTask* JobDispatcher::destroy(JobTask& task, std::uint8_t flags) {
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
JobTask* TASK_MEMBER JobDispatcher::destruction_entry(void* object, std::uint8_t flags) {
    return destroy(*static_cast<JobTask*>(object), flags);
}

void JobDispatcher::dispatch(JobTask& task, void* items, std::uint32_t count,
                             std::uint32_t stride) {
    if (count == 0)
        return;
    const auto first = reinterpret_cast<std::uintptr_t>(items);
    store_pointer(task.cursor, first);
    task.end = first + static_cast<std::uintptr_t>(count) * stride;
    const auto available = invoke<std::int32_t>(pool_, 8);
    auto workers = std::min(static_cast<std::int32_t>(count - 1), available);
    if (workers < 1) {
        invoke<std::int32_t>(&task, 0);
        consume(task, stride);
        return;
    }
    workers = std::min(workers, 32);
    std::array<void*, 32> handles{};
    for (std::int32_t i = 0; i < workers; ++i) {
        void* handle = nullptr;
        invoke<void, JobTask*, void**, int, int>(pool_, 15, &task, &handle, 0, 8);
        if (handle) {
            invoke<std::int32_t>(&task, 0);
            handles[static_cast<std::size_t>(i)] = handle;
        }
    }
    invoke<std::int32_t>(&task, 0);
    consume(task, stride);
    for (std::int32_t i = 0; i < workers; ++i)
        if (auto* handle = handles[static_cast<std::size_t>(i)])
            invoke<void>(handle, 1);
    while (load_word(task.completed) != count) {
#if defined(_WIN32)
        _mm_pause();
#else
        __builtin_ia32_pause();
#endif
    }
}
void JobDispatcher::dispatch_eight(JobTask& task, void* items, std::uint32_t count) {
    dispatch(task, items, count, 8);
}
void JobDispatcher::dispatch_thirty_six(JobTask& task, void* items, std::uint32_t count) {
    dispatch(task, items, count, 0x24);
}

#undef TASK_MEMBER
} // namespace skeet::engine
