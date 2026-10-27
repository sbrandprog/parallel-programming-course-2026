#pragma once
// Этап 1: планировщик, который ведёт настоящие потоки строго по одному (передача хода через семафоры).
// Этап 2: обнаружение deadlock в главном цикле и корректное завершение застрявших потоков.
// Внутреннее устройство (поля, семафоры, вспомогательные функции для shared и sched_mutex) придумываете сами.
#include <exception>
#include <functional>
#include <memory>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

#include "strategy.hpp"

namespace sched
{
class sched_mutex;

class Scheduler
{
public:
    Scheduler();
    ~Scheduler();
    // Зарегистрировать поток. Потоки получают буквы A, B, C… в порядке вызова spawn.
    void spawn(std::function<void()> body);
    // Запустить потоки и вести их по одному: на каждой паузе спросить strategy.pick(enabled, last).
    void run(Strategy & strategy);
    // Трасса прогона: буквы потоков, выбранных во всех решениях подряд, например "AAAABB".
    std::string trace_string() const;

    // Если true, каждая операция с общими данными добавляет строку в log. Выставляет runner.
    bool logging = false;
    // Лог прогона: строки "<поток>  <операция>", например "A  lock mu", "A  balance -> 100".
    std::vector<std::string> log;
    // Пустая строка, если deadlock не было; иначе "held={a:A, b:B}" (этап 2).
    std::string deadlock;

private:
    friend void point();
    friend void await(std::function<bool()> cond);
    friend void before_access();
    friend void after_access(const std::string & text);
    friend class sched_mutex;

    struct ThreadCtx
    {
        std::function<void()> body;
        std::thread th;
        std::binary_semaphore wake{ 0 };
        std::function<bool()> wait;
        bool dirty = false;
        bool finished = false;
    };

    struct LockState
    {
        std::string name;
        int owner = -1;
    };

    void thread_main(int id);
    void pause(ThreadCtx & c);
    void wait_for(std::function<bool()> cond, const std::string & text);
    void abort_all();
    void join_all();
    int add_lock(const std::string & name);
    int current_id() const;
    std::string held_locks() const;

    std::vector<std::unique_ptr<ThreadCtx>> threads_;
    std::vector<LockState> locks_;
    std::binary_semaphore to_sched_{ 0 };
    std::string trace_;
    std::exception_ptr error_;
    bool aborted_ = false;
};

// window() из тестовых программ: пауза перед следующей операцией с общими данными обязательна.
void point();
// Ждать, пока cond не станет true. До этого поток не попадает в enabled.
void await(std::function<bool()> cond);

void before_access();
void after_access(const std::string & text);

} // namespace sched
