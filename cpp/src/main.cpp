#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stop_token>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <iostream>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_policy.h>
#endif

#include "SPSCQueue.h"
#include "xxhash.h"

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<typename T, typename... Rest>
constexpr bool multi_is_same() {
    return (std::is_same_v<T, Rest> || ...);
} 

static size_t QUEUE_CAPACITY = 10000;

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

    struct FlushRequest {
        std::promise<void> reply;
    };

    using RequestVariant = std::variant<GetRequest, PutRequest, FlushRequest>;
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

        auto flush() -> std::future<void> {
            FlushRequest request {
                .reply = {}
            };

            auto future = request.reply.get_future();

            while (!in_queue->try_push(std::move(request)));

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
                },
                // guarentees everything before it is processed
                [this](FlushRequest& req) {
                    req.reply.set_value();
                }
            }, request);
        }

        auto start() -> void {
            worker = std::jthread([this](std::stop_token stoken) {
                this->run(stoken);
            });
        }

    private:
        auto pin_to_cpu(size_t cpu_id) -> void {
#ifdef __linux__
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(cpu_id % std::thread::hardware_concurrency(), &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
#ifdef __APPLE__
          thread_affinity_policy_data_t policy;
          policy.affinity_tag = cpu_id; // 0-3 for P-cores, 4-9 for E-cores
          thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY,
                            (thread_policy_t)&policy, THREAD_AFFINITY_POLICY_COUNT);
#endif
        }
    };

    size_t             id;
    size_t             num_cores;
    std::vector<Shard> shards;

    // xxhash?
    inline size_t hash_key(const K& key) {
        if constexpr (multi_is_same<K, int, size_t>()) {
            return key;
        } else {
             return std::hash<K>{}(key);
        }
    }

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
        flush();
        for (auto& shard : shards) {
            shard.worker.request_stop();
        }
    }
   
    auto insert(K key, V value) -> void {
        size_t shard_id = hash_key(key) % num_cores;
        shards[shard_id].put(key, value);
    }

    auto get(K key) -> std::optional<V> {
        size_t shard_id = hash_key(key) % num_cores;
        return shards[shard_id].get(key).get();
    }

    auto flush() -> void {
        std::vector<std::future<void>> futures;

        for(auto& shard : shards) {
            futures.push_back(shard.flush());
        }

        for(auto& fut : futures) {
            fut.get();
        }
    }
};

template <typename K, typename V>
Node<K, V> make_node(int id) {
    return Node<K, V>(id);
}

auto main() -> int {
    using clock = std::chrono::steady_clock;

    constexpr int n = 10'000;
    auto node = make_node<size_t, std::string>(0);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto start = clock::now();
    for (size_t i = 0; i < n; ++i) {
        node.insert(i, "value");
    }
    node.flush();
    auto end = clock::now();

    std::chrono::duration<double> duration = end - start;

    std::cout << "Put " << n << " items in "
              << duration.count() << " seconds. ("
              << n * (1/duration.count()) << " insertions/s)\n";

    return 0;
}
