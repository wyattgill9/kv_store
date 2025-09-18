use kv_store::make_node;

fn main() {
    let test_node = make_node!((u64, u64), id = 0);
    std::thread::sleep(std::time::Duration::from_millis(100));
    test_node.run();

    // println!("{:?}", test_node);
}
