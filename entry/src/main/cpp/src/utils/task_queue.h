//
// Created on 2024/3/14.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#ifndef ARDEMO_TASK_QUEUE_H
#define ARDEMO_TASK_QUEUE_H

#include <mutex>
#include <queue>
#include <thread>

class TaskQueue {
private:
    std::mutex mtx;
    std::condition_variable cv;
    std::queue<std::function<void()>> queue;
    std::atomic<bool> isRunning;
    std::unique_ptr<std::thread> t = nullptr;
public:
    TaskQueue() : isRunning(false) {}
    ~TaskQueue() {}

    // std::forward is used to forward the parameter to the function
    template<typename F, typename... Args>
    void Push(F&& f, Args&&... args) {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        cv.notify_one();
    }

    template <typename F, typename... Args> void PushTop(F &&f, Args &&...args) {
        std::queue<std::function<void()>> tmp;
        tmp.push(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::lock_guard<std::mutex> lock(mtx);
        std:swap(queue, tmp);
        cv.notify_one();
    }

    void Start() {
        if (isRunning) {
            return;
        }
        isRunning = true;
        t = std::make_unique<std::thread>([this] {
            while(isRunning) {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [this] { return !queue.empty(); });
                auto task = queue.front();
                queue.pop();
                lock.unlock();
                task();
            }
        });
    }

    void Stop() {
        if (!isRunning) {
            return;
        }
        isRunning = false;
        std::unique_lock<std::mutex> lock(mtx);
        cv.notify_one();
        lock.unlock();
        t->join();
    }
};
#endif //ARDEMO_TASK_QUEUE_H
