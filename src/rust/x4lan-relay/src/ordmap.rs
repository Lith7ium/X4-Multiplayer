//! An insertion-ordered map with Python `dict` semantics.
//!
//! The relay's output order is part of the wire behaviour (snapshot packets are emitted
//! by iterating dicts), so every map that is iterated must preserve insertion order:
//! assigning an existing key keeps its position, removing and re-inserting moves it to
//! the end. Lookups are O(1) through a hash index.

use std::collections::HashMap;
use std::hash::Hash;

#[derive(Clone, Debug)]
pub struct OrderedMap<K, V> {
    entries: Vec<Option<(K, V)>>,
    index: HashMap<K, usize>,
}

impl<K: Hash + Eq + Clone, V> Default for OrderedMap<K, V> {
    fn default() -> Self {
        Self { entries: Vec::new(), index: HashMap::new() }
    }
}

impl<K: Hash + Eq + Clone, V> OrderedMap<K, V> {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn len(&self) -> usize {
        self.index.len()
    }

    pub fn is_empty(&self) -> bool {
        self.index.is_empty()
    }

    pub fn contains_key(&self, key: &K) -> bool {
        self.index.contains_key(key)
    }

    pub fn get(&self, key: &K) -> Option<&V> {
        self.index.get(key).and_then(|&i| self.entries[i].as_ref()).map(|(_, v)| v)
    }

    pub fn get_mut(&mut self, key: &K) -> Option<&mut V> {
        match self.index.get(key) {
            Some(&i) => self.entries[i].as_mut().map(|(_, v)| v),
            None => None,
        }
    }

    /// `d[key] = value`.
    pub fn insert(&mut self, key: K, value: V) {
        match self.index.get(&key) {
            Some(&i) => self.entries[i] = Some((key, value)),
            None => {
                self.index.insert(key.clone(), self.entries.len());
                self.entries.push(Some((key, value)));
            }
        }
    }

    /// `d.pop(key, None)`.
    pub fn remove(&mut self, key: &K) -> Option<V> {
        let i = self.index.remove(key)?;
        let value = self.entries[i].take().map(|(_, v)| v);
        if self.entries.len() > 32 && self.index.len() * 2 < self.entries.len() {
            self.compact();
        }
        value
    }

    fn compact(&mut self) {
        self.entries.retain(Option::is_some);
        for (i, entry) in self.entries.iter().enumerate() {
            if let Some((k, _)) = entry {
                *self.index.get_mut(k).expect("indexed") = i;
            }
        }
    }

    /// `{k: v for k, v in d.items() if keep(k, v)}` (order preserved).
    pub fn retain(&mut self, mut keep: impl FnMut(&K, &mut V) -> bool) {
        for slot in self.entries.iter_mut() {
            if let Some((k, v)) = slot {
                if !keep(k, v) {
                    self.index.remove(k);
                    *slot = None;
                }
            }
        }
        self.compact();
    }

    pub fn clear(&mut self) {
        self.entries.clear();
        self.index.clear();
    }

    pub fn iter(&self) -> impl Iterator<Item = (&K, &V)> {
        self.entries.iter().filter_map(|e| e.as_ref().map(|(k, v)| (k, v)))
    }

    pub fn iter_mut(&mut self) -> impl Iterator<Item = (&K, &mut V)> {
        self.entries.iter_mut().filter_map(|e| e.as_mut().map(|(k, v)| (&*k, v)))
    }

    pub fn keys(&self) -> impl Iterator<Item = &K> {
        self.iter().map(|(k, _)| k)
    }

    pub fn values(&self) -> impl Iterator<Item = &V> {
        self.iter().map(|(_, v)| v)
    }

    pub fn values_mut(&mut self) -> impl Iterator<Item = &mut V> {
        self.iter_mut().map(|(_, v)| v)
    }

    pub fn first(&self) -> Option<(&K, &V)> {
        self.iter().next()
    }
}

#[cfg(test)]
mod tests {
    use super::OrderedMap;

    #[test]
    fn python_dict_ordering() {
        let mut m = OrderedMap::new();
        m.insert("a", 1);
        m.insert("b", 2);
        m.insert("a", 3); // keeps position
        assert_eq!(m.iter().map(|(k, v)| (*k, *v)).collect::<Vec<_>>(), [("a", 3), ("b", 2)]);
        m.remove(&"a");
        m.insert("a", 4); // moves to the end
        assert_eq!(m.keys().copied().collect::<Vec<_>>(), ["b", "a"]);
        for i in 0..100 {
            m.insert(if i % 2 == 0 { "x" } else { "y" }, i);
        }
        m.retain(|k, _| *k != "b");
        assert_eq!(m.keys().copied().collect::<Vec<_>>(), ["a", "x", "y"]);
        assert_eq!(m.get(&"y"), Some(&99));
    }
}
