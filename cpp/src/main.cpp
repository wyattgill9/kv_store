#include <cstddef>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__linux__) || defined(APPLE) 
void bind_core(int cpu) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}
#endif

struct SPSCQueue {
    // placeholder
    static std::pair<SPSCQueue, SPSCQueue> create_pair(size_t capacity) {
        return { SPSCQueue(), SPSCQueue() };
    }   
};

template<typename K, typename V>
struct Node {
private:
    struct Shard {
        size_t                   id;
        std::unordered_map<K, V> data;
        std::vector<SPSCQueue>   in_vec;      
        std::vector<SPSCQueue>   out_vec;
       
        Shard(size_t id_, size_t num_cores)
            : id(id_), in_vec(num_cores), out_vec(num_cores) {}
    };

    size_t             id;
    size_t             num_cores;
    std::vector<Shard> shards;

public:
   Node(size_t id)
       : id(id),
         num_cores(std::thread::hardware_concurrency())
    {
        shards.reserve(num_cores);
        for(int i = 0; i < num_cores; i++) {
            shards.emplace_back(i, num_cores);
        }

        for (size_t src = 0; src < num_cores; src++) {
            for (size_t dst = 0; dst < num_cores; dst++) {
                if (src == dst) continue;

                auto [prod, cons]        = SPSCQueue::create_pair(100);
                shards[src].out_vec[dst] = std::move(prod);
                shards[dst].in_vec[src]  = std::move(cons);
            }
        }
    }
};

template <typename K, typename V>
Node<K, V> make_node(int id) {
    return Node<K, V>(id);
}

int main() {
    auto node = make_node<int, int>(0);
    return 0;
}
