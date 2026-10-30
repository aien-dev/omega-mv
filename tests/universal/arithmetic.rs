// Independent Rust development oracle; explicit wrapping regardless of build mode.
use std::io::{self, Read};
fn main() {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input).unwrap();
    for line in input.lines() {
        let v: Vec<u64> = line.split_whitespace().map(|s| s.parse().unwrap()).collect();
        assert_eq!(v.len(), 2);
        let (a, b) = (v[0], v[1]);
        assert_ne!(b, 0);
        println!("{} {} {} {}", a.wrapping_add(b), a.wrapping_sub(b), a.wrapping_mul(b), a / b);
    }
}
