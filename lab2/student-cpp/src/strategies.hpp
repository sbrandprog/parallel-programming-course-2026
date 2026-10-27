#pragma once
// Стратегии выбора следующего потока.
// FirstAlive — этап 1, Replay — этап 2, RandomPick — этап 3, Dfs — этап 4 (ограничение bound — этап 5).
// enabled — номера потоков, которые могут сделать шаг, по возрастанию; last — прошлый выбранный (-1 в начале).
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "strategy.hpp"

namespace sched
{

// Всегда первый поток из enabled.
struct FirstAlive : Strategy
{
    int pick(const std::vector<int> & enabled, int) override
    {
        return enabled[0];
    }
};

// Проигрывает трассу: на i-м решении выбирает поток trace[i] ('A' -> 0, 'B' -> 1).
struct Replay : Strategy
{
    explicit Replay(std::string trace)
        : trace_(std::move(trace))
    {
    }
    // Сбросить позицию в трассе перед прогоном.
    void begin_run() override
    {
        pos_ = 0;
    }
    int pick(const std::vector<int> & enabled, int) override
    {
        if (pos_ >= trace_.size())
        {
            throw std::logic_error("replay: трасса короче прогона");
        }

        char letter = trace_[pos_++];

        int want = letter - 'A';

        for (int id : enabled)
        {
            if (id == want)
            {
                return want;
            }
        }

        throw std::logic_error(std::string("replay: поток ") + letter + " не может сделать шаг");
    }

private:
    std::string trace_;
    std::size_t pos_ = 0;
};

// Случайный поток: enabled[rnd() % enabled.size()], rnd — std::mt19937(seed).
struct RandomPick : Strategy
{
    explicit RandomPick(uint32_t seed)
        : rnd_(seed)
    {
    }
    int pick(const std::vector<int> & enabled, int) override
    {
        return enabled[rnd_() % enabled.size()];
    }

private:
    std::mt19937 rnd_;
};

// Stateless DFS по дереву выборов: каждый прогон с нуля, повторяет префикс прошлого и берёт следующий вариант.
// bound — максимум вытеснений (этап 5); для dfs runner передаёт INT_MAX.
struct Dfs : Strategy
{
    explicit Dfs(int bound)
        : bound_(bound)
    {
    }
    // Подготовиться к новому прогону (позиция в пути, счётчик вытеснений).
    void begin_run() override
    {
        depth_ = 0;
        preemptions_ = 0;
    }
    int pick(const std::vector<int> & enabled, int last) override
    {
        if (depth_ == path_.size())
        {
            Node node{ {}, 0 };

            for (int id : enabled)
            {
                if (preemptions_ + preemption_cost(id, last, enabled) <= bound_)
                {
                    node.options.push_back(id);
                }
            }

            path_.push_back(node);
        }

        Node & node = path_[depth_++];

        int id = node.options[node.idx];

        preemptions_ += preemption_cost(id, last, enabled);

        return id;
    }
    // Выбрать следующий путь; false — дерево обойдено.
    bool end_run() override
    {
        while (!path_.empty() && path_.back().idx + 1 == path_.back().options.size())
        {
            path_.pop_back();
        }

        if (path_.empty())
        {
            return false;
        }

        path_.back().idx++;

        return true;
    }

private:
    struct Node
    {
        std::vector<int> options;
        std::size_t idx;
    };

    // Вытеснение: переключение с last, если last ещё может ходить.
    static int preemption_cost(int id, int last, const std::vector<int> & enabled)
    {
        if (last < 0 || id == last)
        {
            return 0;
        }
        for (int e : enabled)
        {
            if (e == last)
            {
                return 1;
            }
        }
        return 0;
    }

    int bound_;
    std::vector<Node> path_;
    std::size_t depth_ = 0;
    int preemptions_ = 0;
};

} // namespace sched
