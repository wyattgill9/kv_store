use std::{
    collections::HashMap, fmt::Debug, hash::Hash, thread::{self, JoinHandle}
};

use rtrb::{RingBuffer, Consumer, Producer};
use thiserror::Error;

pub mod core_affinity;
pub mod num_cores;

impl From<usize> for core_affinity::CoreId {
    fn from(value: usize) -> Self {
        core_affinity::CoreId { id: value }
    }
}

use num_cores::num_cpus;

pub static CLUSTER_MAX: usize = 0;

pub trait Key: Hash + Eq + Send + Sync + 'static {}
impl<T: Hash + Eq + Send + Sync + 'static> Key for T {}

pub trait Value: Hash + Eq + Send + Sync + 'static {}
impl<T: Hash + Eq + Send + Sync + 'static> Value for T {}

#[derive(Error, Debug)]
pub enum KVError {
    #[error("unknown error occurred")]
    Unknown,
}

type KVResult<T> = Result<T, KVError>;

pub enum Request<K, V> {
    PUT(K, V),
    GET(K),
}

pub struct Shard<K, V> {
    id      : usize,
    data    : HashMap<K, V>,
    out_vec : Vec<Option<Producer<Request<K, V>>> >,
    in_vec  : Vec<Option<Consumer<Request<K, V>>> >, 
}

impl<K, V> Shard<K, V>
where
    K: Key,
    V: Value
{
    fn new(id: usize, num_cores: usize) -> Self {
        Shard {
            id,
            data: HashMap::new(),
            out_vec: (0..num_cores).map(|_| None).collect(),
            in_vec: (0..num_cores).map(|_| None).collect(),
        }
    }

    fn run(mut self) {
        core_affinity::set_for_current(self.id.into());
        loop {
            println!("d");
            // for consumer in self.in_vec.iter_mut().flatten() {
                // while let Ok(request) = consumer.pop() {
                    // match request {
                        // Request::PUT(key, value) => { self.insert(key, value); }
                        // Request::GET(key) => { self.get(&key); }
                    // }
                // }
            // }
            std::thread::sleep(std::time::Duration::from_micros(1));
        }
    }

    pub fn send(&mut self, dst: usize, request: Request<K, V>) -> KVResult<()> {
        if let Some(queue) = &mut self.out_vec[dst] {
            queue.push(request).map_err(|_r| KVError::Unknown)
        } else {
            Err(KVError::Unknown)
        }
    }

    pub fn insert(&mut self, key: K, value: V) -> Option<V> {
        self.data.insert(key, value)
    }

    pub fn get(&self, key: &K) -> Option<&V> {
        self.data.get(key)
    }
}

pub struct Node<K: Key, V: Value> {
    id        : usize,
    num_cores : usize,
    shards    : Vec<Shard<K, V>>,
}

impl<K: Key, V: Value> std::fmt::Debug for Node<K, V> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Node")
            .field("id", &self.id)
            .field("num_cores", &self.num_cores)
            .field("active_shards", &self.shards.len())
            .finish()
    }
}

impl<K, V> Node<K, V>
where
    K: Key,
    V: Value
{
    pub fn new(id: usize) -> Self { // maybe something later like max cores in cluster idk
        let num_cores = num_cpus::detect();
        
        let mut shards: Vec<Shard<K, V>> = (0..num_cores)
            .map(|i| Shard::new(i, num_cores))
            .collect();

        for src in 0..num_cores {
            for dst in 0..num_cores {
                if src == dst {
                    continue
                }
                let (prod, cons) = RingBuffer::<Request<K, V>>::new(100);
                shards[src].out_vec[dst] = Some(prod);
                shards[dst].in_vec[src]  = Some(cons);
            }
        }

        Self {
            id,
            num_cores,
            shards,
        }
    }

    pub fn run(self) {
        let handles: Vec<_> = self.shards
            .into_iter()
            .map(|shard| thread::spawn(move || shard.run()))
            .collect();

        for handle in handles {
            handle.join().ok();
        }
    }

    fn send_shard(&mut self, shard_id: usize, req: Request<K, V>) -> Result<(), KVError> {
        self.shards[0].send(shard_id, req) // abuse shard 0 out vec to reach the other shards todo: maybe fix this is kinda shitty
    }
}

#[macro_export]
macro_rules! make_node {
    (($key:ty, $value:ty), id = $id:expr) => {
        $crate::Node::<$key, $value>::new($id)
    };
    (id = $id: expr) => {
        $crate::Node::new($id)
    };
    () => {
        $crate::Node::new(0)
    };
}
