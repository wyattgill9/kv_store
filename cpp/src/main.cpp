#include <cstddef>
#include <thread>
#include <unordered_map>
#include <vector>

#include "SPSCQueue.h"

static size_t QUEUE_CAPACITY = 100;

template<typename K, typename V>
struct Node {
private:
    enum class Request : uint8_t {
        UNKNOWN
    };

    using RequestQueue = std::shared_ptr<rigtorp::SPSCQueue<Request>>;
    
    struct Shard {
        size_t                    id;
        std::unordered_map<K, V>  data;
        std::vector<RequestQueue> in_vec;      
        std::vector<RequestQueue> out_vec;      
       
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
        for (size_t src = 0; src < num_cores; ++src) {
            for (size_t dst = 0; dst < num_cores; ++dst) {
                if (src == dst) continue;

                auto queue = std::make_shared<rigtorp::SPSCQueue<Request>>(QUEUE_CAPACITY);
                shards[src].out_vec[dst] = queue; // writer
                shards[dst].in_vec[src]  = queue; // reader
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
