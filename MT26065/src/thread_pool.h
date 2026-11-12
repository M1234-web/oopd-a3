
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include <memory>
#include <type_traits>
#include <exception>

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> jobs;

    std::mutex mutex_;
    std::condition_variable ready;
    bool stopping = false;

public:
    explicit ThreadPool(int count) {
        if (count <= 0) {
            throw std::invalid_argument(
                "Thread count must be positive"
            );
        }

        try {
            for (int i = 0; i < count; ++i) {
                workers.emplace_back([this]() {
                    for (;;) {
                        std::function<void()> job;

                        {
                            std::unique_lock<std::mutex> lock(mutex_);

                            ready.wait(lock, [this]() {
                                return stopping || !jobs.empty();
                            });

                            if (stopping && jobs.empty()) {
                                return;
                            }

                            job = std::move(jobs.front());
                            jobs.pop();
                        }

                        job();
                    }
                });
            }
        }
        catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping = true;
            }

            ready.notify_all();

            for (auto& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }

            throw;
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <class F>
    auto submit(F&& f) -> std::future<decltype(f())> {
        using Result = decltype(f());

        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::forward<F>(f)
        );

        std::future<Result> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (stopping) {
                throw std::runtime_error(
                    "Submit on stopped thread pool"
                );
            }

            jobs.emplace([task]() {
                (*task)();
            });
        }

        ready.notify_one();

        return result;
    }

    template <class F>
    void parallelFor(int taskCount, F&& fn) {
        if (taskCount <= 0) {
            return;
        }

        // One shared callable is used by all submitted tasks.
        auto taskFunction =
            std::make_shared<std::decay_t<F>>(
                std::forward<F>(fn)
            );

        std::vector<std::future<void>> futures;
        futures.reserve(static_cast<std::size_t>(taskCount));

        for (int i = 0; i < taskCount; ++i) {
            futures.emplace_back(
                submit([taskFunction, i]() {
                    (*taskFunction)(i);
                })
            );
        }

        // Wait for every task, even if one task throws.
        // Rethrow the first exception after all tasks finish.
        std::exception_ptr firstException = nullptr;

        for (auto& future : futures) {
            try {
                future.get();
            }
            catch (...) {
                if (!firstException) {
                    firstException = std::current_exception();
                }
            }
        }

        if (firstException) {
            std::rethrow_exception(firstException);
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping = true;
        }

        ready.notify_all();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
};

#endif
