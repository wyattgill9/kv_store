use std::{
    collections::HashMap,
    fmt::Debug,
    hash::Hash,
    sync::{Arc, atomic::{AtomicBool, Ordering}},
    thread::{self, JoinHandle},
    time::Duration,
};

use thiserror::Error;

pub mod core_affinity;
pub mod num_cores;

use num_cores::num_cpus;

pub static CLUSTER_MAX: usize = 0;

pub trait Key: Hash + Eq + Send + Sync + 'static {}
impl<T: Hash + Eq + Send + Sync + 'static> Key for T {}

pub trait Value: Send + Sync + 'static {}
impl<T: Send + Sync + 'static> Value for T {}

#[derive(Error, Debug)]
pub enum KVError {
    #[error("unknown error occurred")]
    Unknown,
}

type KVResult<T> = Result<T, KVError>;

#[derive(Debug)]
pub struct Shard<K, V> {
    id  : usize,
    data: HashMap<K, V>,
}

impl<K: Key, V: Value> Shard<K, V> {
    pub fn new(id: usize) -> Self {
        Self {
            id,
            data: HashMap::new(),
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
    id            : usize,
    num_cores     : num_cpus::LogicalCores,
    shards        : Vec<Option<Shard<K, V>>>,
    shutdown      : Arc<AtomicBool>,
    thread_handles: Vec<JoinHandle<()>>,
}

impl<K: Key, V: Value> std::fmt::Debug for Node<K, V> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Node")
            .field("id", &self.id)
            .field("num_cores", &self.num_cores)
            .field("active_shards", &self.shards.iter().filter(|s| s.is_some()).count())
            .field("active_threads", &self.thread_handles.len())
            .finish()
    }
}

impl<K: Key, V: Value> Node<K, V> {
    pub fn new(id: usize, mut cluster_max: usize) -> Self {
        let num_cores = num_cpus::detect();
        cluster_max = usize::max(num_cores.as_usize(), cluster_max);

        let shards = (0..cluster_max)
            .map(|i| {
                if i < num_cores.as_usize() {
                    Some(Shard::new(i))
                } else {
                    None
                }
            })
            .collect();

        Self {
            id,
            num_cores,
            shards,
            shutdown      : Arc::new(AtomicBool::new(false)),
            thread_handles: Vec::new(),
        }
    }
}

#[macro_export]
macro_rules! make_node {
    (($key:ty, $value:ty), id = $id:expr, cluster_max = $cluster_max:expr) => {
        $crate::Node::<$key, $value>::new($id, $cluster_max)
    };
    (($key:ty, $value:ty), id = $id:expr) => {
        $crate::Node::<$key, $value>::new($id, $crate::CLUSTER_MAX)
    };
    (id = $id: expr, cluster_max = $cm: expr) => {
        $crate::Node::new($id, $cm)
    };
    ($id: expr) => {
        $crate::Node::new($id, $crate::CLUSTER_MAX)
    };
    () => {
        $crate::Node::new(0, $crate::CLUSTER_MAX)
    };
}
