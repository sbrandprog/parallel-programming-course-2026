#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <latch>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>
#include <tuple>
#include <vector>

static constexpr size_t BucketsCount = 256;
using Buckets = std::array<uint64_t, BucketsCount>;

struct Snapshot
{
    Buckets m_buckets;
    uint64_t m_count;
    uint64_t m_sum;
    uint64_t m_min;
    uint64_t m_max;
    uint64_t m_p50;
    uint64_t m_p99;
};

class MetricsCollector
{
public:
    virtual ~MetricsCollector() = default;
    virtual void record(uint64_t value) = 0;
    virtual Snapshot snapshot() = 0;

    static uint64_t CalculatePercentile(const Snapshot & snapshot, double p)
    {
        uint64_t threshold = snapshot.m_count * p;
        uint64_t accumulator = 0;

        for (uint64_t i = 0; i < snapshot.m_buckets.size(); ++i)
        {
            accumulator += snapshot.m_buckets[i];

            if (accumulator >= threshold)
            {
                return i * 4;
            }
        }

        return snapshot.m_buckets.size() * 4;
    }
};

class SynchronousCollector : public MetricsCollector
{
public:
    void record(uint64_t value) override
    {
        ++m_buckets[std::min<uint64_t>(value / 4, 255)];
        ++m_count;
        m_sum += value;
        m_min = std::min(m_min, value);
        m_max = std::max(m_max, value);
    }

    Snapshot snapshot() override
    {
        Snapshot result = {
            .m_buckets = m_buckets,
            .m_count = m_count,
            .m_sum = m_sum,
            .m_min = m_min,
            .m_max = m_max,
            .m_p50 = 0,
            .m_p99 = 0,
        };

        result.m_p50 = CalculatePercentile(result, 0.50);
        result.m_p99 = CalculatePercentile(result, 0.99);

        return result;
    }

private:
    Buckets m_buckets{};
    uint64_t m_count = 0;
    uint64_t m_sum = 0;
    uint64_t m_min = std::numeric_limits<uint64_t>::max();
    uint64_t m_max = std::numeric_limits<uint64_t>::min();
};

class SynchronizedCollector : public MetricsCollector
{
public:
    void record(uint64_t value) override
    {
        auto lock = std::lock_guard(m_mutex);

        ++m_buckets[std::min<uint64_t>(value / 4, 255)];
        ++m_count;
        m_sum += value;
        m_min = std::min(m_min, value);
        m_max = std::max(m_max, value);
    }

    Snapshot snapshot() override
    {
        auto lock = std::lock_guard(m_mutex);

        Snapshot result = {
            .m_buckets = m_buckets,
            .m_count = m_count,
            .m_sum = m_sum,
            .m_min = m_min,
            .m_max = m_max,
            .m_p50 = 0,
            .m_p99 = 0,
        };

        result.m_p50 = CalculatePercentile(result, 0.50);
        result.m_p99 = CalculatePercentile(result, 0.99);

        return result;
    }

private:
    Buckets m_buckets{};
    uint64_t m_count = 0;
    uint64_t m_sum = 0;
    uint64_t m_min = std::numeric_limits<uint64_t>::max();
    uint64_t m_max = std::numeric_limits<uint64_t>::min();

    std::mutex m_mutex;
};

class SynchronizedEmptyCollector : public MetricsCollector
{
public:
    void record(uint64_t value) override
    {
        auto lock = std::lock_guard(m_mutex);

        std::ignore = value;
    }

    Snapshot snapshot() override
    {
        auto lock = std::lock_guard(m_mutex);

        Snapshot result = {
            .m_buckets = m_buckets,
            .m_count = m_count,
            .m_sum = m_sum,
            .m_min = m_min,
            .m_max = m_max,
            .m_p50 = 0,
            .m_p99 = 0,
        };

        result.m_p50 = CalculatePercentile(result, 0.50);
        result.m_p99 = CalculatePercentile(result, 0.99);

        return result;
    }

private:
    Buckets m_buckets{};
    uint64_t m_count = 0;
    uint64_t m_sum = 0;
    uint64_t m_min = std::numeric_limits<uint64_t>::max();
    uint64_t m_max = std::numeric_limits<uint64_t>::min();

    std::mutex m_mutex;
};

class ShardedCollector : public MetricsCollector
{
public:
    static constexpr size_t ShardsCount = 16;

    void record(uint64_t value) override
    {
        auto bucket = std::min<uint64_t>(value / 4, 255);

        auto shard_i = bucket % ShardsCount;

        {
            auto lock = std::lock_guard(m_shards[shard_i]);

            ++m_buckets[bucket];
        }

        m_count.fetch_add(1);
        m_sum.fetch_add(value);
        for (uint64_t minValue = m_min.load();
             minValue > value && !m_min.compare_exchange_weak(minValue, std::min(minValue, value));)
            ;
        for (uint64_t maxValue = m_max.load();
             maxValue < value && !m_max.compare_exchange_weak(maxValue, std::max(maxValue, value));)
            ;
    }

    Snapshot snapshot() override
    {
        Snapshot result = {
            .m_buckets = {},
            .m_count = m_count.load(std::memory_order_acquire),
            .m_sum = m_sum.load(std::memory_order_acquire),
            .m_min = m_min.load(std::memory_order_acquire),
            .m_max = m_max.load(std::memory_order_acquire),
            .m_p50 = 0,
            .m_p99 = 0,
        };

        for (size_t i = 0; i < m_shards.size(); ++i)
        {
            auto lock = std::lock_guard(m_shards[i]);

            for (size_t j = i; j < BucketsCount; j += ShardsCount)
            {
                result.m_buckets[j] = m_buckets[j];
            }
        }

        result.m_p50 = CalculatePercentile(result, 0.50);
        result.m_p99 = CalculatePercentile(result, 0.99);

        return result;
    }

private:
    std::array<std::mutex, ShardsCount> m_shards;
    Buckets m_buckets{};
    std::atomic<uint64_t> m_count = 0;
    std::atomic<uint64_t> m_sum = 0;
    std::atomic<uint64_t> m_min = std::numeric_limits<uint64_t>::max();
    std::atomic<uint64_t> m_max = std::numeric_limits<uint64_t>::min();
};

class ThreadLocalCollector : public MetricsCollector
{
    struct alignas(std::hardware_constructive_interference_size) ThreadState
    {
        std::array<std::atomic<uint64_t>, 256> buckets{};
        std::atomic<uint64_t> count{ 0 };
        std::atomic<uint64_t> sum{ 0 };
        std::atomic<uint64_t> min{ UINT64_MAX };
        std::atomic<uint64_t> max{ 0 };
    };

public:
    static uint64_t nextCollectorId()
    {
        static std::atomic<uint64_t> counter{ 1 };
        return counter.fetch_add(1);
    }

    ThreadState * getMyState()
    {
        struct TLSSlot
        {
            uint64_t id = 0;
            ThreadState * state = nullptr;
        };
        static thread_local TLSSlot slot;
        if (slot.id != m_id)
        {
            auto s = std::make_unique<ThreadState>();
            ThreadState * raw = s.get();
            {
                std::lock_guard<std::mutex> g(m_statesLock);
                m_states.push_back(std::move(s));
            }
            slot.id = m_id;
            slot.state = raw;
        }
        return slot.state;
    }

    static void relaxedAdd(std::atomic<uint64_t> & c, uint64_t delta)
    {
        c.store(c.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
    }

    void record(uint64_t value) override
    {
        ThreadState * s = getMyState();
        uint64_t b = std::min(value / 4, (uint64_t)255);

        relaxedAdd(s->buckets[b], 1);
        relaxedAdd(s->count, 1);
        relaxedAdd(s->sum, value);

        if (value < s->min.load(std::memory_order_relaxed))
            s->min.store(value, std::memory_order_relaxed);
        if (value > s->max.load(std::memory_order_relaxed))
            s->max.store(value, std::memory_order_relaxed);
    }

    Snapshot snapshot() override
    {
        std::vector<ThreadState *> states;
        {
            std::lock_guard<std::mutex> g(m_statesLock);
            states.reserve(m_states.size());
            for (auto && s : m_states)
            {
                states.push_back(s.get());
            }
        }

        Snapshot result = {
            .m_buckets = {},
            .m_count = 0,
            .m_sum = 0,
            .m_min = std::numeric_limits<uint64_t>::max(),
            .m_max = 0,
            .m_p50 = 0,
            .m_p99 = 0,
        };

        for (auto && state : states)
        {
            for (size_t i = 0; i < BucketsCount; ++i)
            {
                result.m_buckets[i] += state->buckets[i].load(std::memory_order_relaxed);
            }

            result.m_count += state->count.load(std::memory_order_relaxed);
            result.m_sum += state->sum.load(std::memory_order_relaxed);
            result.m_min = std::min(result.m_min, state->min.load(std::memory_order_relaxed));
            result.m_max = std::max(result.m_max, state->max.load(std::memory_order_relaxed));
        }

        result.m_p50 = CalculatePercentile(result, 0.50);
        result.m_p99 = CalculatePercentile(result, 0.99);

        return result;
    }

private:
    uint64_t m_id = nextCollectorId();
    std::mutex m_statesLock;
    std::vector<std::unique_ptr<ThreadState>> m_states;
};

template <bool ThirdStep = true>
class DoubleBufferedThreadLocalCollector : public MetricsCollector
{
    struct Buffer
    {
        std::array<uint64_t, 256> m_buckets{};
        uint64_t m_count = 0;
        uint64_t m_sum = 0;
        uint64_t m_min = std::numeric_limits<uint64_t>::max();
        uint64_t m_max = std::numeric_limits<uint64_t>::min();

        void clear()
        {
            m_buckets.fill(0);
            m_count = 0;
            m_sum = 0;
            m_min = std::numeric_limits<uint64_t>::max();
            m_max = std::numeric_limits<uint64_t>::min();
        }

        void addFrom(const Buffer & other)
        {
            for (size_t i = 0; i < m_buckets.size(); ++i)
            {
                m_buckets[i] += other.m_buckets[i];
            }
            m_count += other.m_count;
            m_sum += other.m_sum;
            m_min = std::min(m_min, other.m_min);
            m_max = std::max(m_max, other.m_max);
        }
    };

    struct alignas(64) ThreadState
    {
        std::atomic<int> m_state{ -1 };
        Buffer m_buffers[2];
    };

public:
    static uint64_t nextCollectorId()
    {
        static std::atomic<uint64_t> counter{ 1 };
        return counter.fetch_add(1);
    }

    ThreadState * getMyState()
    {
        struct TLSSlot
        {
            uint64_t id = 0;
            ThreadState * state = nullptr;
        };
        static thread_local TLSSlot slot;
        if (slot.id != m_id)
        {
            auto s = std::make_unique<ThreadState>();
            ThreadState * raw = s.get();
            {
                std::lock_guard<std::mutex> g(m_statesLock);
                m_states.push_back(std::move(s));
            }
            slot.id = m_id;
            slot.state = raw;
        }
        return slot.state;
    }

    void record(uint64_t value) override
    {
        auto state = getMyState();

        int buffer = 0;

        for (;;)
        {
            buffer = m_active.load();

            state->m_state = buffer;

            if constexpr (ThirdStep)
            {
                if (m_active == buffer)
                {
                    break;
                }

                state->m_state = -1;
            }
            else
            {
                break;
            }
        }

        auto && activeBuffer = state->m_buffers[buffer];

        ++activeBuffer.m_buckets[std::min<uint64_t>(value / 4, 255)];
        ++activeBuffer.m_count;
        activeBuffer.m_sum += value;
        activeBuffer.m_min = std::min(activeBuffer.m_min, value);
        activeBuffer.m_max = std::max(activeBuffer.m_max, value);

        state->m_state = -1;
    }

    Snapshot snapshot() override
    {
        Snapshot result = {
            .m_buckets = {},
            .m_count = 0,
            .m_sum = 0,
            .m_min = std::numeric_limits<uint64_t>::max(),
            .m_max = std::numeric_limits<uint64_t>::min(),
            .m_p50 = 0,
            .m_p99 = 0,
        };

        {
            auto lock = std::lock_guard(m_statesLock);

            int old = m_active;
            m_active = 1 - old;

            for (auto && state : m_states)
            {
                while (state->m_state == old)
                {
                    std::this_thread::yield();
                }
            }

            for (auto && state : m_states)
            {
                auto && buffer = state->m_buffers[old];

                m_globalData.addFrom(buffer);

                buffer.clear();
            }

            result.m_buckets = m_globalData.m_buckets;
            result.m_count = m_globalData.m_count;
            result.m_sum = m_globalData.m_sum;
            result.m_min = m_globalData.m_min;
            result.m_max = m_globalData.m_max;
        }

        result.m_p50 = CalculatePercentile(result, 0.50);
        result.m_p99 = CalculatePercentile(result, 0.99);

        return result;
    }

private:
    uint64_t m_id = nextCollectorId();
    std::atomic<int> m_active{ 0 };
    std::mutex m_statesLock;
    std::vector<std::unique_ptr<ThreadState>> m_states;
    Buffer m_globalData;
};

static std::vector<uint64_t> GenerateLoadValues()
{
    auto gen = std::mt19937_64(300376);

    std::vector<uint64_t> result;

    result.resize(1 << 20);

    const size_t n = 1023;
    const double alpha = 1.15;

    std::vector<double> weights;
    weights.resize(n);
    for (size_t k = 0; k < n; ++k)
    {
        weights[k] = 1.0 / std::pow(k + 1, alpha);
    }

    auto zipf_dist = std::discrete_distribution<size_t>(weights.begin(), weights.end());

    for (auto && elem : result)
    {
        elem = zipf_dist(gen) + 1;
    }

    return result;
}

static double Run(
    MetricsCollector & collector, const std::vector<uint64_t> & values, size_t threadsCount, size_t seconds
)
{
    auto latch = std::latch(1);
    auto stopFlag = std::atomic_flag();
    auto operations = std::vector<size_t>();
    auto threads = std::vector<std::thread>();

    operations.resize(threadsCount);
    threads.reserve(threadsCount);

    for (size_t k = 0; k < threadsCount; ++k)
    {
        threads.emplace_back(
            [&, k]()
            {
                size_t count = 0;
                size_t i = 1000 * k;
                latch.wait();

                while (!stopFlag.test(std::memory_order::acquire))
                {
                    collector.record(values[i++]);
                    count++;

                    if (i == values.size())
                    {
                        i = 0;
                    }
                }

                operations[k] = count;
            }
        );
    }

    auto start = std::chrono::steady_clock::now();
    latch.count_down();
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    stopFlag.test_and_set(std::memory_order::release);
    auto end = std::chrono::steady_clock::now();

    for (auto && thread : threads)
    {
        thread.join();
    }

    return std::accumulate(operations.begin(), operations.end(), 0.)
           / std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
}

static double Measure(
    MetricsCollector & collector, const std::vector<uint64_t> & values, size_t threadsCount, size_t seconds
)
{
    Run(collector, values, threadsCount, 5);

    static constexpr size_t RunsCount = 5;

    auto runs = std::array<double, RunsCount>();

    for (auto && run : runs)
    {
        run = Run(collector, values, threadsCount, seconds);
    }

    volatile size_t a = collector.snapshot().m_count;
    std::ignore = a;

    std::ranges::sort(runs);

    return (runs.size() % 2 == 1) ? (runs[runs.size() / 2]) : ((runs[runs.size() / 2 - 1] + runs[runs.size() / 2]) / 2);
}

static std::tuple<size_t, size_t, size_t, size_t, size_t> ConsistencyTest(
    MetricsCollector & collector, const std::vector<uint64_t> & values, size_t threadsCount
)
{
    static constexpr size_t SnapshotsCount = 10000;

    auto latch = std::latch(1);
    auto stopFlag = std::atomic_flag();
    auto operations = std::vector<size_t>();
    auto threads = std::vector<std::thread>();

    operations.resize(threadsCount);
    threads.reserve(threadsCount);

    for (size_t k = 0; k < threadsCount; ++k)
    {
        threads.emplace_back(
            [&, k]()
            {
                size_t count = 0;
                size_t i = 1000 * k;
                latch.wait();

                while (!stopFlag.test(std::memory_order::acquire))
                {
                    collector.record(values[i++]);
                    count++;

                    if (i == values.size())
                    {
                        i = 0;
                    }
                }

                operations[k] = count;
            }
        );
    }

    size_t less = 0, equal = 0, greater = 0;

    latch.count_down();
    for (size_t i = 0; i < SnapshotsCount; ++i)
    {
        auto snapshot = collector.snapshot();

        auto sum = std::accumulate(snapshot.m_buckets.begin(), snapshot.m_buckets.end(), size_t(0));

        if (sum == snapshot.m_count)
        {
            ++equal;
        }
        else if (sum < snapshot.m_count)
        {
            ++less;
        }
        else
        {
            ++greater;
        }
    }
    stopFlag.test_and_set(std::memory_order::release);

    for (auto && thread : threads)
    {
        thread.join();
    }

    auto last = collector.snapshot();
    auto operationsCount = std::accumulate(operations.begin(), operations.end(), size_t(0));

    return { less, equal, greater, last.m_count, operationsCount };
}

int main()
{
    auto values = GenerateLoadValues();

    const std::initializer_list<uint64_t> threads = { 1, 2, 4, 8 };
    const size_t seconds = 5;

    if (false)
    {
        auto collector = std::make_unique<SynchronousCollector>();

        std::cout << std::setw(22) << "Synchronous: " << " 1: " << Measure(*collector, values, 1, seconds) << std::endl;
    }

    if (false)
    {
        auto collector = std::make_unique<SynchronizedCollector>();

        for (auto threadsCount : threads)
        {
            std::cout << std::setw(22) << "Synchronized: " << std::setw(2) << threadsCount << ": "
                      << Measure(*collector, values, threadsCount, seconds) << std::endl;
        }
    }

    if (false)
    {
        auto collector = std::make_unique<SynchronizedEmptyCollector>();

        for (auto threadsCount : threads)
        {
            std::cout << std::setw(22) << "Synchronized Empty: " << std::setw(2) << threadsCount << ": "
                      << Measure(*collector, values, threadsCount, seconds) << std::endl;
        }
    }

    if (false)
    {
        auto collector = std::make_unique<ShardedCollector>();

        for (auto threadsCount : threads)
        {
            std::cout << std::setw(22) << "Sharded: " << std::setw(2) << threadsCount << ": "
                      << Measure(*collector, values, threadsCount, seconds) << std::endl;
        }
    }

    if (false)
    {
        auto collector = std::make_unique<ShardedCollector>();

        auto [less, equal, greater, count, operationsCount] = ConsistencyTest(*collector, values, 4);

        std::cout << std::setw(22) << "Sharded: " << " 4: sum less(" << less << ") equal(" << equal << ") greater("
                  << greater << "); count(" << count << ") operationsCount(" << operationsCount << ")" << std::endl;
    }

    if (false)
    {
        auto collector = std::make_unique<ThreadLocalCollector>();

        for (auto threadsCount : threads)
        {
            std::cout << std::setw(22) << "Thread Local: " << std::setw(2) << threadsCount << ": "
                      << Measure(*collector, values, threadsCount, seconds) << std::endl;
        }
    }

    if (false)
    {
        auto collector = std::make_unique<ThreadLocalCollector>();

        auto [less, equal, greater, count, operationsCount] = ConsistencyTest(*collector, values, 4);

        std::cout << std::setw(22) << "Thread Local: " << " 4: sum less(" << less << ") equal(" << equal << ") greater("
                  << greater << "); count(" << count << ") operationsCount(" << operationsCount << ")" << std::endl;
    }

    if (false)
    {
        auto collector = std::make_unique<DoubleBufferedThreadLocalCollector<true>>();

        for (auto threadsCount : threads)
        {
            std::cout << std::setw(22) << "DB Thread Local: " << std::setw(2) << threadsCount << ": "
                      << Measure(*collector, values, threadsCount, seconds) << std::endl;
        }
    }

    if (false)
    {
        auto collector = std::make_unique<DoubleBufferedThreadLocalCollector<true>>();

        auto [less, equal, greater, count, operationsCount] = ConsistencyTest(*collector, values, 4);

        std::cout << std::setw(22) << "DB Thread Local: " << " 4: sum less(" << less << ") equal(" << equal
                  << ") greater(" << greater << "); count(" << count << ") operationsCount(" << operationsCount << ")"
                  << std::endl;
    }

    // if (false)
    {
        auto collector = std::make_unique<DoubleBufferedThreadLocalCollector<false>>();

        auto [less, equal, greater, count, operationsCount] = ConsistencyTest(*collector, values, 4);

        std::cout << std::setw(22) << "DB-3 Thread Local: " << " 4: sum less(" << less << ") equal(" << equal
                  << ") greater(" << greater << "); count(" << count << ") operationsCount(" << operationsCount << ")"
                  << std::endl;
    }

    return 0;
}
