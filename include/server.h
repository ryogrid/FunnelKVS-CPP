#ifndef FUNNELKVS_SERVER_H
#define FUNNELKVS_SERVER_H

#include "storage.h"
#include "protocol.h"
#include <thread>
#include <vector>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>

namespace funnelkvs {

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;

public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool();
    
    template<typename F>
    void enqueue(F&& f);
    
private:
    void worker_thread();
};

class Server {
protected:
    Storage storage;
    ThreadPool thread_pool;
    int server_fd;
    std::atomic<bool> running;
    std::thread accept_thread;
    uint16_t port;
    
public:
    explicit Server(uint16_t port, size_t num_threads = 8);
    virtual ~Server();
    
    virtual void start();
    virtual void stop();
    bool is_running() const { return running.load(); }
    
protected:
    void accept_loop();
    void handle_client(int client_fd);
    virtual void process_request(int client_fd, const Request& request);
    bool receive_data(int fd, std::vector<uint8_t>& buffer, size_t expected_size);
    bool send_data(int fd, const std::vector<uint8_t>& data);
    
private:
    void setup_server_socket();
};

template<typename F>
void ThreadPool::enqueue(F&& f) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop.load()) {
            return;
        }
        tasks.emplace(std::forward<F>(f));
    }
    condition.notify_one();
}

} // namespace funnelkvs

#endif // FUNNELKVS_SERVER_H