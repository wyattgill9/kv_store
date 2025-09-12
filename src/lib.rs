use std::{collections::HashMap, fmt::Debug};

pub mod num_cores;
use num_cores::CoreCount;

const SHARD_CAP:  usize = 1000;
const NUM_SHARDS: usize = 4;
const CHUNK_SIZE: usize = SHARD_CAP / NUM_SHARDS;

#[derive(Debug)]
pub struct Shard<K, V> {
    data: HashMap<K, V>,
}

#[derive(Debug)]
pub struct Node<K, V> {
    num_cores: CoreCount, // core 0- shard 0, core 1 - shard 1 (all threads pinned)
    shards:    Vec<Option<Shard<K, V>>>,
}

impl<K, V> Node<K, V> {
    pub fn new(id: usize) -> Self {
        Self {
            num_cores: num_cores::num_cpus::get(),
            shards: Vec::new(),
        }
    }
}

