use std::{
    collections::HashMap,
    fmt::Debug,
    hash::Hash,
    sync::{Arc, atomic::{AtomicBool, Ordering}},
    thread::{self, JoinHandle},
    time::Duration,
};

pub mod core_affinity;
pub mod num_cores;

use num_cores::num_cpus;

pub static CLUSTER_MAX: usize = 0;

#[derive(Debug)]
pub struct Shard<K, V> {
    id  : usize,
    data: HashMap<K, V>,
}

impl<K, V> Shard<K, V>
where
    K: Hash + Eq,
{
    pub fn new(id: usize) -> Self {
        Self {
            id,
            data: HashMap::new(),
        }
    }

    pub fn insert(&mut self, key: K, value: V) -> Option<V> {
        self.data.insert(key, value)
    }
}

pub struct Node<K, V> {
    id            : usize,
    num_cores     : num_cpus::LogicalCores,
    shards        : Vec<Option<Shard<K, V>>>,
    shutdown      : Arc<AtomicBool>,
    thread_handles: Vec<JoinHandle<()>>,
}

impl<K, V> std::fmt::Debug for Node<K, V> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Node")
            .field("id", &self.id)
            .field("num_cores", &self.num_cores)
            .field("active_shards", &self.shards.iter().filter(|s| s.is_some()).count())
            .field("active_threads", &self.thread_handles.len())
            .finish()
    }
}

impl<K, V> Node<K, V>
where
    K: Send + Sync + Hash + Eq + Debug + 'static,
    V: Send + Sync + Hash + Eq + Debug + 'static,
{
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

    pub fn start(&mut self) {
        for (core_id, opt_shard) in self.shards.iter_mut().enumerate() {
            if let Some(shard) = opt_shard.take() {
                let shutdown_flag = Arc::clone(&self.shutdown);
                
                let handle = thread::spawn(move || {
                    let core = core_affinity::CoreId { id: core_id };
                    core_affinity::set_for_current(core);

                    println!("Thread on core {} started with shard {}", core_id, shard.id);

                    while !shutdown_flag.load(Ordering::Relaxed) {
                        thread::sleep(Duration::from_millis(500));
                    }
                    
                    println!("Thread on core {} shutting down", core_id);
                });
                
                self.thread_handles.push(handle);
            }
        }
    }
}

impl<K, V> Drop for Node<K, V> {
    fn drop(&mut self) {
        self.shutdown.store(true, Ordering::Relaxed);
        
        while let Some(handle) = self.thread_handles.pop() {
            if let Err(e) = handle.join() {
                eprintln!("Thread panicked: {:?}", e);
            }
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
