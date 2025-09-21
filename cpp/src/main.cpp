#include <chrono>
#include <cstddef>
#include <memory>
#include <ratio>
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

static size_t QUEUE_CAPACITY = 100;

template <typename K, typename V> struct Node {
private:
    using RequestVariant = std::variant<
        K,                // GET
        std::pair<K, V>  // PUT
    >;

    using RequestQueue     = rigtorp::SPSCQueue<RequestVariant>;
    using InterThreadQueue = std::shared_ptr<rigtorp::SPSCQueue<RequestVariant>>;

    struct Shard {
        size_t                        id;
        std::unordered_map<K, V>      data;

        std::unique_ptr<RequestQueue> in_queue; // client requests
        std::vector<InterThreadQueue> in_vec;   // to   other shards
        std::vector<InterThreadQueue> out_vec;  // from other shards

        std::jthread                  worker;
        // std::atomic_flag              running;
        
        Shard(size_t id_, size_t num_cores)
            : id(id_), in_vec(num_cores), out_vec(num_cores) {
                in_queue = std::make_unique<RequestQueue>(QUEUE_CAPACITY);
            }

        // TODO: Impl manualy so i can use a atomic flag for running
        // Shard(Shard&&) noexcept    = default
        // Shard& operator=(Shard&&) = default;
        // Shard(const Shard&)            = default;
        // Shard& operator=(const Shard&) = default;

        [[nodiscard]] auto insert(std::pair<K, V> kv) -> bool {
            return data.insert(kv).second;
        }

        [[nodiscard]] auto find(K key) -> V{
            return data.find(key)->second;
        } 
        
        auto run() -> void {
            pin_to_cpu(id);
            // TODO: actual loop
        }

        auto start() -> void {
            worker = std::jthread(&Shard::run, this);
        }

        // void stop() {
        //     If we have the running atomic flag we would also make it false here
        // }

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
            shards.emplace_back(i, num_cores);
        }

        for (size_t src = 0; src < num_cores; ++src) {
            for (size_t dst = 0; dst < num_cores; ++dst) {
                if (src == dst) continue;

                auto queue = std::make_shared<rigtorp::SPSCQueue<RequestVariant>>(QUEUE_CAPACITY);
                shards[src].out_vec[dst] = queue; // writer
                shards[dst].in_vec[src]  = queue; // reader
            }
        }

        for (auto& shard : shards) {
            shard.start();
        }
    }
   
    [[nodiscard]] auto insert(K key, V value) -> bool {
        size_t shard_id = std::hash<K>{}(key) % num_cores;
        return shards[shard_id].insert({key, value});      
    }

    auto get(K key) -> V {
        size_t shard_id = std::hash<K>{}(key) % num_cores;
        return shards[shard_id].find(key);
    }
};

template <typename K, typename V>
Node<K, V> make_node(int id) {
    return Node<K, V>(id);
}

auto main() -> int {
    auto node = make_node<int, std::string>(0);

    std::cout << node.insert(0, "Hello, World!") << "\n";
    std::cout << node.get(0) << "\n";

    return 0;
}
