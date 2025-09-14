use kv_store::make_node;

fn main() {
    let mut test_node = make_node!((u64, u64), id = 0, cluster_max = 10);
    std::thread::sleep(std::time::Duration::from_millis(100));

    test_node.start();

    println!("{:?}", test_node);
}
