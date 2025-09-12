use kv_store;

fn main() {
    let test: kv_store::Node<u32, u32> = kv_store::Node::new(0);
    println!("{:?}", test);
}
