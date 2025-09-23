#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <iostream>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#endif

#include "SPSCQueue.h"

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

static size_t QUEUE_CAPACITY = 100;

template <typename K, typename V> struct Node {
private:
    struct GetRequest {
        K key;
        std::promise<std::optional<V>> reply;
    };

    struct PutRequest {
        K key;
        V value;       
        std::promise<bool> reply;
    };

    using RequestVariant = std::variant<GetRequest, PutRequest>;
    using RequestQueue   = rigtorp::SPSCQueue<RequestVariant>;

    struct Shard {
        size_t                        id;
        std::unordered_map<K, V>      data;
        std::unique_ptr<RequestQueue> in_queue; // client requests

        std::jthread                  worker;
        std::stop_source              stop_src;
        
        Shard(size_t id_)
            : id(id_) {
                in_queue = std::make_unique<RequestQueue>(QUEUE_CAPACITY);
            }

        [[nodiscard]] auto insert(K key, V value) -> bool {
            auto [it, inserted] = data.insert_or_assign(key, value);
            return inserted;
        }

        auto put(K key, V value) -> std::future<bool> {
            PutRequest request {
                .key   = key,
                .value = value,
                .reply = {}
            };

            auto future = request.reply.get_future();

            while(!in_queue->try_push(std::move(request)));

            return future;
        }
       
        auto get(K key) -> std::future<std::optional<V>> {
            GetRequest request {
                .key   = key,
                .reply = {}
            };

            auto future = request.reply.get_future();

            while(!in_queue->try_push(std::move(request)));

            return future;
        }
        
        auto run(std::stop_token stoken) -> void {
            pin_to_cpu(id);

            for(;;) {
                if (stoken.stop_requested()) {
                    return;
                }

                while(!in_queue->front()) {
                    std::this_thread::yield();
                }

                RequestVariant request = std::move(*in_queue->front());
                in_queue->pop();

                handle_request(std::move(request));
            }
        }

        void handle_request(RequestVariant&& request) {
            std::visit(overloaded {
                [this](GetRequest& req) {
                    auto it = data.find(req.key);
                    if(it != data.end()) {
                        req.reply.set_value(it->second);
                    } else {
                        return req.reply.set_value(std::nullopt);
                    }
                },
                [this](PutRequest& req) {
                    auto [it, inserted] = data.insert_or_assign(req.key, req.value);
                    req.reply.set_value(inserted);
                }
            }, request);
        }

        auto start() -> void {
            worker = std::jthread(&Shard::run, this, stop_src);
        }

    private:
        auto pin_to_cpu(size_t cpu_id) -> void {
#ifdef __linux__
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(cpu_id % std::thread::hardware_concurrency(), &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
        }
    };

    size_t             id;
    size_t             num_cores;
    std::vector<Shard> shards;

public:
    explicit Node(size_t id)
     : id(id), num_cores(std::thread::hardware_concurrency()) {

        for (size_t i = 0; i < num_cores; i++) {
            shards.emplace_back(i);
        }

        for (auto& shard : shards) {
            shard.start();
        }
    }

    ~Node() {
        for (auto& shard : shards) {
            shard.worker.request_stop();
        }
    }
   
    // [[nodiscard]]
    auto insert(K key, V value) -> bool {
        size_t shard_id = std::hash<K>{}(key) % num_cores;
        return shards[shard_id].put(key, value).get(); 
    }

    auto get(K key) -> std::optional<V> {
        size_t shard_id = std::hash<K>{}(key) % num_cores;
        return shards[shard_id].get(key).get();
    }
};

template <typename K, typename V>
Node<K, V> make_node(int id) {
    return Node<K, V>(id);
}

auto main() -> int {
    auto node = make_node<int, std::string>(0);

    // std::cout << node.insert(0, "Hello, World!") << "\n";
    // std::cout << node.get(0).value() << "\n";

    // for(int i = 0; i < 100; i++) {
        // node.insert(i, "Hello, World");
    // }
    // 

    std::cout << node.get(0).value() << "\n";

    // for(int i = 0; i < 100; i++) {
        // std::cout << node.get(i).value() << "\n";
    // }

    return 0;
}
