// Этап 1–2: реализация Scheduler, point() и await().
// Деструктор должен дождаться (join) всех запущенных потоков, в том числе после
// deadlock.
#include "scheduler.hpp"
#include <stdexcept>

namespace sched
{
namespace
{
thread_local Scheduler * tl_sched = nullptr;
thread_local int tl_id = -1;

class Aborted : public std::exception
{
};

Scheduler & current_sched()
{
    if (!tl_sched)
    {
        throw std::logic_error("операция с общими данными вне планировщика");
    }

    return *tl_sched;
}

} // namespace

Scheduler::Scheduler()
{
}

Scheduler::~Scheduler()
{
    join_all();
}

void Scheduler::spawn(std::function<void()> body)
{
    auto c = std::make_unique<ThreadCtx>();

    c->body = std::move(body);

    threads_.push_back(std::move(c));
}

void Scheduler::pause(ThreadCtx & c)
{
    to_sched_.release();

    c.wake.acquire();

    if (aborted_)
    {
        throw Aborted{};
    }
}

void Scheduler::wait_for(std::function<bool()> cond, const std::string & text)
{
    ThreadCtx & c = *threads_[tl_id];

    if (c.dirty || !cond())
    {
        c.dirty = false;

        c.wait = cond;

        pause(c);

        c.wait = nullptr;
    }

    after_access(text);
}

void Scheduler::thread_main(int id)
{
    tl_sched = this;

    tl_id = id;

    ThreadCtx & c = *threads_[id];

    c.wake.acquire();

    try
    {
        if (!aborted_)
        {
            c.body();
        }
    }
    catch (const Aborted &)
    {
    }
    catch (...)
    {
        if (!error_)
        {
            error_ = std::current_exception();
        }
    }

    c.finished = true;

    if (!aborted_)
    {
        to_sched_.release();
    }

    tl_sched = nullptr;
}

void Scheduler::run(Strategy & strategy)
{
    trace_.clear();

    deadlock.clear();

    for (int i = 0; i < int(threads_.size()); i++)
    {
        threads_[i]->th = std::thread(
            [this, i]
            {
                thread_main(i);
            }
        );
    }

    try
    {
        std::vector<int> enabled;
        int last = -1;

        while (true)
        {
            enabled.clear();

            bool alive = false;

            for (int i = 0; i < int(threads_.size()); i++)
            {
                ThreadCtx & c = *threads_[i];

                if (c.finished)
                {
                    continue;
                }

                alive = true;

                if (!c.wait || c.wait())
                {
                    enabled.push_back(i);
                }
            }

            if (!alive)
            {
                break;
            }

            if (enabled.empty())
            {
                deadlock = held_locks();

                abort_all();

                break;
            }

            int next = strategy.pick(enabled, last);

            last = next;

            trace_ += char('A' + next);

            threads_[next]->wake.release();

            to_sched_.acquire();
        }
    }
    catch (...)
    {
        abort_all();

        throw;
    }

    join_all();

    if (error_)
    {
        std::rethrow_exception(error_);
    }
}

void Scheduler::abort_all()
{
    aborted_ = true;

    for (auto & t : threads_)
    {
        if (!t->finished)
        {
            t->wake.release();
        }
    }

    join_all();
}

void Scheduler::join_all()
{
    for (auto & t : threads_)
    {
        if (t->th.joinable())
        {
            t->th.join();
        }
    }
}

int Scheduler::add_lock(const std::string & name)
{
    locks_.push_back(LockState{ name });

    return int(locks_.size()) - 1;
}

int Scheduler::current_id() const
{
    if (tl_sched != this)
    {
        throw std::logic_error("sched_mutex вне планировщика");
    }

    return tl_id;
}

std::string Scheduler::held_locks() const
{
    std::string out = "held={";

    bool first = true;

    for (auto & l : locks_)
    {
        if (l.owner < 0)
        {
            continue;
        }

        if (!first)
        {
            out += ", ";
        }

        out += l.name + ":" + char('A' + l.owner);

        first = false;
    }

    return out + "}";
}

std::string Scheduler::trace_string() const
{
    return trace_;
}

void before_access()
{
    Scheduler & s = current_sched();

    Scheduler::ThreadCtx & c = *s.threads_[tl_id];

    if (c.dirty)
    {
        c.dirty = false;

        s.pause(c);
    }
}

void after_access(const std::string & text)
{
    Scheduler & s = current_sched();

    s.threads_[tl_id]->dirty = true;

    if (s.logging)
    {
        s.log.push_back(std::string(1, char('A' + tl_id)) + "  " + text);
    }
}

void point()
{
    after_access("window");
}

void await(std::function<bool()> cond)
{
    current_sched().wait_for(cond, "await done");
}

} // namespace sched
