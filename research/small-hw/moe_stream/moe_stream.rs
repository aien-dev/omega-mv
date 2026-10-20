//! L273 small-hardware probe: Qwen3-30B-A3B with its experts streamed from disk at a capped bandwidth.
//!
//! Built inside a read-only export of aien-sovereign-core (see build.sh), so it reuses the existing CPU
//! reference (`qwen3_coder`: loader, attention, router, tokenizer) instead of a second forward.
//!
//! Modes, run in one process on the same prompts:
//!   ref                  the unmodified `forward_token` + `compute_logits` path (the yardstick)
//!   res                  this file's forward, experts read from the resident capsule (must equal ref)
//!   stream:CAP:CACHE     this file's forward, experts read from the checkpoint with O_DIRECT through a
//!                        paced reader capped at CAP MB/s (0 = uncapped) into an LRU slot cache of CACHE GB
//!                        (must equal ref token for token and logit bit for bit)
//! Every streamed expert is compared byte for byte with the resident copy after it lands.
//!
//! Subcommands:
//!   run  --checkpoint DIR --prompts FILE --new N --out DIR --modes ref,res,stream:400:4.5,...
//!   sim  --trace FILE --cache-gb LIST [--slot-bytes B]   LRU and Belady hit rates from a routing trace
//!   layout --checkpoint DIR                              expert byte spans in the safetensors files

use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Write};
use std::os::unix::fs::{FileExt, OpenOptionsExt};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use aien_inference_abi::tokenizer::ChatTokenizer as Tokenizer;
use aien_inference_abi::{
    apply_rope, bf16_to_f32, compute_logits, fp8_e4m3fn_to_f32, forward_token, load_qwen3_coder,
    reference_router_logits, rmsnorm, sample_argmax, scaled_dot_product_attention_single,
    MoeBatchPlan, Qwen3CoderState, Qwen3CoderWeights, QWEN3_CODER_HEAD_DIM, QWEN3_CODER_HIDDEN,
    QWEN3_CODER_KV_DIM, QWEN3_CODER_KV_HEADS, QWEN3_CODER_Q_DIM, QWEN3_CODER_Q_HEADS,
    QWEN3_CODER_RMS_EPS, QWEN3_CODER_ROPE_THETA, QWEN3_CODER_VOCAB,
};
use rayon::prelude::*;
use sha2::{Digest, Sha256};

const H: usize = 2048;
const I: usize = 768;
const BLOCK: usize = 128;
const LAYERS: usize = 48;
const EXPERTS: usize = 128;
const TOPK: usize = 8;
const GU_SCALES: usize = (2 * I / BLOCK) * (H / BLOCK);
const DOWN_SCALES: usize = (H / BLOCK) * (I / BLOCK);
/// Bytes of one expert as a slot: gate||up FP8, down FP8, both BF16 scale tables.
const SLOT_BYTES: usize = 2 * I * H + H * I + 2 * (GU_SCALES + DOWN_SCALES);
const ALIGN: u64 = 4096;
#[cfg(target_arch = "aarch64")]
const O_DIRECT: i32 = 0o200000;
#[cfg(not(target_arch = "aarch64"))]
const O_DIRECT: i32 = 0o40000;

fn die(msg: &str) -> ! {
    eprintln!("moe_stream: {msg}");
    std::process::exit(2)
}

// ---------------------------------------------------------------- safetensors expert map

#[derive(Clone, Copy, Debug)]
struct Part {
    file: usize,
    off: u64,
    len: u64,
}

/// Six tensors of one expert, in slot order: gate w, up w, down w, gate s, up s, down s.
struct ExpertMap {
    files: Vec<PathBuf>,
    parts: Vec<[Part; 6]>, // index layer * EXPERTS + expert
}

fn read_header(path: &Path) -> (u64, serde_json::Value) {
    let mut f = File::open(path).unwrap_or_else(|e| die(&format!("open {}: {e}", path.display())));
    let mut n = [0u8; 8];
    f.read_exact(&mut n).unwrap_or_else(|e| die(&format!("header len: {e}")));
    let n = u64::from_le_bytes(n);
    let mut buf = vec![0u8; n as usize];
    f.read_exact(&mut buf).unwrap_or_else(|e| die(&format!("header: {e}")));
    let v: serde_json::Value = serde_json::from_slice(&buf).unwrap_or_else(|e| die(&format!("json: {e}")));
    (8 + n, v)
}

fn expert_map(dir: &Path) -> ExpertMap {
    let mut files: Vec<PathBuf> = std::fs::read_dir(dir)
        .unwrap_or_else(|e| die(&format!("read_dir: {e}")))
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.extension().map(|x| x == "safetensors").unwrap_or(false))
        .collect();
    files.sort();
    let mut table: HashMap<String, Part> = HashMap::new();
    for (fi, p) in files.iter().enumerate() {
        let (base, hdr) = read_header(p);
        for (name, meta) in hdr.as_object().unwrap() {
            if name == "__metadata__" || !name.contains(".mlp.experts.") {
                continue;
            }
            let off = meta["data_offsets"].as_array().unwrap();
            let (a, b) = (off[0].as_u64().unwrap(), off[1].as_u64().unwrap());
            table.insert(name.clone(), Part { file: fi, off: base + a, len: b - a });
        }
    }
    let mut parts = Vec::with_capacity(LAYERS * EXPERTS);
    for l in 0..LAYERS {
        for e in 0..EXPERTS {
            let get = |s: &str| {
                let k = format!("model.layers.{l}.mlp.experts.{e}.{s}");
                *table.get(&k).unwrap_or_else(|| die(&format!("missing {k}")))
            };
            parts.push([
                get("gate_proj.weight"),
                get("up_proj.weight"),
                get("down_proj.weight"),
                get("gate_proj.weight_scale_inv"),
                get("up_proj.weight_scale_inv"),
                get("down_proj.weight_scale_inv"),
            ]);
        }
    }
    let want = [I * H, I * H, H * I, 2 * GU_SCALES / 2, 2 * GU_SCALES / 2, 2 * DOWN_SCALES];
    for p in &parts {
        for (k, w) in p.iter().zip(want) {
            if k.len as usize != w {
                die(&format!("tensor size {} != {w}", k.len));
            }
        }
    }
    ExpertMap { files, parts }
}

// ---------------------------------------------------------------- paced O_DIRECT slot cache

struct AlignedBuf {
    ptr: *mut u8,
    cap: usize,
}

impl AlignedBuf {
    fn new(cap: usize) -> Self {
        let layout = std::alloc::Layout::from_size_align(cap, ALIGN as usize).unwrap();
        // SAFETY: non-zero size, valid alignment; freed in Drop with the same layout.
        let ptr = unsafe { std::alloc::alloc(layout) };
        assert!(!ptr.is_null());
        Self { ptr, cap }
    }
    fn slice(&mut self, n: usize) -> &mut [u8] {
        assert!(n <= self.cap);
        // SAFETY: ptr is valid for cap bytes and exclusively borrowed.
        unsafe { std::slice::from_raw_parts_mut(self.ptr, n) }
    }
}

impl Drop for AlignedBuf {
    fn drop(&mut self) {
        let layout = std::alloc::Layout::from_size_align(self.cap, ALIGN as usize).unwrap();
        // SAFETY: allocated in new() with this layout.
        unsafe { std::alloc::dealloc(self.ptr, layout) }
    }
}

#[derive(Default, Clone, Copy)]
struct IoStats {
    hits: u64,
    misses: u64,
    disk_bytes: u64, // aligned bytes actually read
    reads: u64,
    stall: Duration, // read + pacing sleep
    sleep: Duration,
    mismatched_slots: u64,
}

struct SlotCache {
    map: ExpertMap,
    fds: Vec<File>,
    cap_bps: f64, // 0 = uncapped
    slots: Vec<Vec<u8>>,
    owner: Vec<Option<(u16, u16)>>,
    last_use: Vec<u64>,
    index: HashMap<(u16, u16), usize>,
    clock: u64,
    buf: AlignedBuf,
    stats: IoStats,
}

impl SlotCache {
    fn new(map: ExpertMap, cap_mbps: f64, cache_bytes: f64) -> Self {
        let n = ((cache_bytes / SLOT_BYTES as f64).floor() as usize).max(TOPK);
        let fds = map
            .files
            .iter()
            .map(|p| {
                std::fs::OpenOptions::new()
                    .read(true)
                    .custom_flags(O_DIRECT)
                    .open(p)
                    .unwrap_or_else(|e| die(&format!("O_DIRECT open {}: {e}", p.display())))
            })
            .collect();
        Self {
            map,
            fds,
            cap_bps: cap_mbps * 1e6,
            slots: (0..n).map(|_| vec![0u8; SLOT_BYTES]).collect(),
            owner: vec![None; n],
            last_use: vec![0; n],
            index: HashMap::new(),
            clock: 0,
            buf: AlignedBuf::new(16 << 20),
            stats: IoStats::default(),
        }
    }

    fn n_slots(&self) -> usize {
        self.slots.len()
    }

    /// Aligned O_DIRECT read of [off, off+len) into the scratch buffer; returns (start in buf, aligned bytes).
    fn read_span(&mut self, file: usize, off: u64, len: u64) -> (usize, u64) {
        let a = off / ALIGN * ALIGN;
        let b = (off + len).div_ceil(ALIGN) * ALIGN;
        let n = (b - a) as usize;
        let fd = &self.fds[file];
        let buf = self.buf.slice(n);
        let mut done = 0usize;
        while done < n {
            let r = fd
                .read_at(&mut buf[done..], a + done as u64)
                .unwrap_or_else(|e| die(&format!("pread: {e}")));
            if r == 0 {
                // End of file inside the last aligned block: the tail past EOF is not needed.
                break;
            }
            done += r;
        }
        assert!(done as u64 >= off + len - a, "short read");
        self.stats.reads += 1;
        self.stats.disk_bytes += b - a;
        ((off - a) as usize, b - a)
    }

    fn load(&mut self, layer: usize, e: usize, slot: usize) {
        let t0 = Instant::now();
        let parts = self.map.parts[layer * EXPERTS + e];
        // Destination offsets inside the slot, in slot order (see SLOT_BYTES).
        let dst = [0, I * H, 2 * I * H, 3 * I * H, 3 * I * H + GU_SCALES, 3 * I * H + 2 * GU_SCALES];
        // Group the six tensors into runs that sit together in one file (gap under 64 KiB) and read each
        // run once: in this checkpoint that is one run for the three weight matrices and one for the scales.
        let mut order: Vec<usize> = (0..6).collect();
        order.sort_by_key(|&k| (parts[k].file, parts[k].off));
        let mut runs: Vec<Vec<usize>> = Vec::new();
        for k in order {
            match runs.last_mut() {
                Some(r) if {
                    let p = parts[*r.last().unwrap()];
                    p.file == parts[k].file && parts[k].off <= p.off + p.len + (64 << 10)
                } => r.push(k),
                _ => runs.push(vec![k]),
            }
        }
        let mut bytes = 0u64;
        let mut slot_buf = std::mem::take(&mut self.slots[slot]);
        for r in &runs {
            let lo = parts[r[0]].off;
            let hi = r.iter().map(|&k| parts[k].off + parts[k].len).max().unwrap();
            let (start, n) = self.read_span(parts[r[0]].file, lo, hi - lo);
            bytes += n;
            let buf = self.buf.slice(start + (hi - lo) as usize);
            for &k in r {
                let (p, d) = (parts[k], dst[k]);
                let s = start + (p.off - lo) as usize;
                slot_buf[d..d + p.len as usize].copy_from_slice(&buf[s..s + p.len as usize]);
            }
        }
        self.slots[slot] = slot_buf;
        if self.cap_bps > 0.0 {
            let need = Duration::from_secs_f64(bytes as f64 / self.cap_bps);
            let took = t0.elapsed();
            if need > took {
                std::thread::sleep(need - took);
                self.stats.sleep += need - took;
            }
        }
        self.stats.stall += t0.elapsed();
        self.stats.misses += 1;
    }

    /// Makes the expert resident in a slot and returns the slot index.
    fn get(&mut self, layer: usize, e: usize) -> usize {
        self.clock += 1;
        let key = (layer as u16, e as u16);
        if let Some(&s) = self.index.get(&key) {
            self.stats.hits += 1;
            self.last_use[s] = self.clock;
            return s;
        }
        let s = (0..self.n_slots()).min_by_key(|&s| self.last_use[s]).unwrap();
        if let Some(old) = self.owner[s].take() {
            self.index.remove(&old);
        }
        self.load(layer, e, s);
        self.owner[s] = Some(key);
        self.index.insert(key, s);
        self.last_use[s] = self.clock;
        s
    }
}

// ---------------------------------------------------------------- forward (copy of qwen3_coder::forward_token)

/// Same arithmetic, same order as qwen3_coder::matvec_f32_parallel (private there).
fn matvec_f32_parallel(x: &[f32], w: &[f32], out: &mut [f32], in_dim: usize, _out_dim: usize) {
    out.par_chunks_exact_mut(1).enumerate().for_each(|(j, slot)| {
        let row = &w[j * in_dim..(j + 1) * in_dim];
        let mut sum = 0.0f32;
        for i in 0..in_dim {
            sum += x[i] * row[i];
        }
        slot[0] = sum;
    });
}

fn rmsnorm_per_head(x: &[f32], weight: &[f32], eps: f32, heads: usize, hd: usize, out: &mut [f32]) {
    for h in 0..heads {
        rmsnorm(&x[h * hd..(h + 1) * hd], weight, eps, &mut out[h * hd..(h + 1) * hd]);
    }
}

/// Same arithmetic, same order as qwen3_moe::dequant_matvec (crate-private there).
fn dequant_matvec(weights: &[u8], scales: &[u16], rows: usize, cols: usize, v: &[f32], lut: &[f32; 256]) -> Vec<f32> {
    let k_blocks = cols / BLOCK;
    (0..rows)
        .map(|r| {
            let row = &weights[r * cols..(r + 1) * cols];
            (0..k_blocks)
                .map(|b| {
                    let dot: f32 = row[b * BLOCK..(b + 1) * BLOCK]
                        .iter()
                        .zip(&v[b * BLOCK..(b + 1) * BLOCK])
                        .map(|(&w, &x)| lut[w as usize] * x)
                        .sum();
                    dot * bf16_to_f32(scales[(r / BLOCK) * k_blocks + b])
                })
                .sum()
        })
        .collect()
}

fn as_u16(b: &[u8]) -> Vec<u16> {
    b.chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect()
}

/// One expert: gate||up matvec, SiLU(gate)*up, down matvec. Mirrors reference_moe_forward_with (Fp32).
fn expert_out(gu: &[u8], gus: &[u16], down: &[u8], ds: &[u16], x: &[f32], lut: &[f32; 256]) -> Vec<f32> {
    let projected = dequant_matvec(gu, gus, 2 * I, H, x, lut);
    let hidden: Vec<f32> = (0..I)
        .map(|j| {
            let gate = projected[j];
            gate / (1.0 + (-gate).exp()) * projected[I + j]
        })
        .collect();
    dequant_matvec(down, ds, H, I, &hidden, lut)
}

enum Experts<'a> {
    Resident,
    Stream(&'a mut SlotCache),
}

struct StepOut {
    hidden: Vec<f32>,
    route: Vec<[u8; TOPK]>,
    compute: Duration,
}

fn forward(w: &Qwen3CoderWeights, src: &mut Experts, tok: u32, pos: usize, st: &mut Qwen3CoderState, lut: &[f32; 256]) -> StepOut {
    let (h, qd, kvd, hd, eps) = (QWEN3_CODER_HIDDEN, QWEN3_CODER_Q_DIM, QWEN3_CODER_KV_DIM, QWEN3_CODER_HEAD_DIM, QWEN3_CODER_RMS_EPS);
    let t_start = Instant::now();
    let stall0 = match src {
        Experts::Stream(c) => c.stats.stall,
        Experts::Resident => Duration::ZERO,
    };
    let idx = (tok as usize) % QWEN3_CODER_VOCAB;
    let mut x = w.embed[idx * h..(idx + 1) * h].to_vec();
    let mut route = Vec::with_capacity(LAYERS);
    for (li, (attn, moe)) in w.attns.iter().zip(w.moes.iter()).enumerate() {
        let kv = &mut st.layers[li];
        let mut xn = vec![0.0f32; h];
        rmsnorm(&x, &attn.input_norm, eps, &mut xn);
        let (mut q, mut k, mut v) = (vec![0.0f32; qd], vec![0.0f32; kvd], vec![0.0f32; kvd]);
        matvec_f32_parallel(&xn, &attn.q_proj, &mut q, h, qd);
        matvec_f32_parallel(&xn, &attn.k_proj, &mut k, h, kvd);
        matvec_f32_parallel(&xn, &attn.v_proj, &mut v, h, kvd);
        let (mut qn, mut kn) = (vec![0.0f32; qd], vec![0.0f32; kvd]);
        rmsnorm_per_head(&q, &attn.q_norm, eps, QWEN3_CODER_Q_HEADS, hd, &mut qn);
        rmsnorm_per_head(&k, &attn.k_norm, eps, QWEN3_CODER_KV_HEADS, hd, &mut kn);
        apply_rope(&mut qn, &mut kn, pos, QWEN3_CODER_Q_HEADS, QWEN3_CODER_KV_HEADS, hd, QWEN3_CODER_ROPE_THETA);
        kv.keys.push(kn);
        kv.values.push(v);
        let mut ao = vec![0.0f32; qd];
        scaled_dot_product_attention_single(&qn, &kv.keys, &kv.values, QWEN3_CODER_Q_HEADS, QWEN3_CODER_KV_HEADS, hd, &mut ao);
        let mut ap = vec![0.0f32; h];
        matvec_f32_parallel(&ao, &attn.o_proj, &mut ap, qd, h);
        for i in 0..h {
            x[i] += ap[i];
        }
        let mut post = vec![0.0f32; h];
        rmsnorm(&x, &attn.post_norm, eps, &mut post);
        let router = reference_router_logits(moe, &post, 1);
        let plan = MoeBatchPlan::qwen3_coder_a3b(&router, 1).expect("routing plan must build");
        let mut ids = [0u8; TOPK];
        for s in 0..TOPK {
            ids[s] = plan.topk_experts[s] as u8;
        }
        route.push(ids);
        // Each expert's output is computed independently (in parallel), then summed in slot order,
        // exactly as the reference's serial loop does: out = 0; out += w_s * y_s for s = 0..8.
        let outs: Vec<Vec<f32>> = match src {
            Experts::Resident => {
                let (gu, gus, dn, dns) = (moe.gate_up_fp8(), moe.gate_up_scale_inv_bf16(), moe.down_fp8(), moe.down_scale_inv_bf16());
                (0..TOPK)
                    .into_par_iter()
                    .map(|s| {
                        let e = ids[s] as usize;
                        expert_out(
                            &gu[e * 2 * I * H..(e + 1) * 2 * I * H],
                            &gus[e * GU_SCALES..(e + 1) * GU_SCALES],
                            &dn[e * H * I..(e + 1) * H * I],
                            &dns[e * DOWN_SCALES..(e + 1) * DOWN_SCALES],
                            &post,
                            lut,
                        )
                    })
                    .collect()
            }
            Experts::Stream(cache) => {
                let slots: Vec<usize> = (0..TOPK).map(|s| cache.get(li, ids[s] as usize)).collect();
                // Byte-for-byte check of every slot against the resident copy (outside the timed stall).
                for &s in &slots {
                    let e = cache.owner[s].unwrap().1 as usize;
                    let b = &cache.slots[s];
                    let ok = b[..2 * I * H] == moe.gate_up_fp8()[e * 2 * I * H..(e + 1) * 2 * I * H]
                        && b[2 * I * H..3 * I * H] == moe.down_fp8()[e * H * I..(e + 1) * H * I]
                        && as_u16(&b[3 * I * H..3 * I * H + 2 * GU_SCALES]) == moe.gate_up_scale_inv_bf16()[e * GU_SCALES..(e + 1) * GU_SCALES]
                        && as_u16(&b[3 * I * H + 2 * GU_SCALES..]) == moe.down_scale_inv_bf16()[e * DOWN_SCALES..(e + 1) * DOWN_SCALES];
                    if !ok {
                        cache.stats.mismatched_slots += 1;
                    }
                }
                let slot_bufs: Vec<&Vec<u8>> = slots.iter().map(|&s| &cache.slots[s]).collect();
                slot_bufs
                    .par_iter()
                    .map(|b| {
                        let gus = as_u16(&b[3 * I * H..3 * I * H + 2 * GU_SCALES]);
                        let dns = as_u16(&b[3 * I * H + 2 * GU_SCALES..]);
                        expert_out(&b[..2 * I * H], &gus, &b[2 * I * H..3 * I * H], &dns, &post, lut)
                    })
                    .collect()
            }
        };
        let mut mo = vec![0.0f32; h];
        for s in 0..TOPK {
            let wgt = plan.topk_weights[s];
            for (o, v) in mo.iter_mut().zip(&outs[s]) {
                *o += wgt * v;
            }
        }
        for i in 0..h {
            x[i] += mo[i];
        }
    }
    let mut xf = vec![0.0f32; h];
    rmsnorm(&x, &w.final_norm, eps, &mut xf);
    let stall = match src {
        Experts::Stream(c) => c.stats.stall - stall0,
        Experts::Resident => Duration::ZERO,
    };
    StepOut { hidden: xf, route, compute: t_start.elapsed() - stall }
}

// ---------------------------------------------------------------- run

fn arg(args: &[String], key: &str) -> Option<String> {
    args.iter().position(|a| a == key).and_then(|i| args.get(i + 1).cloned())
}

fn need(args: &[String], key: &str) -> String {
    arg(args, key).unwrap_or_else(|| die(&format!("missing {key}")))
}

fn sha_f32(v: &[f32]) -> String {
    let mut h = Sha256::new();
    for x in v {
        h.update(x.to_bits().to_le_bytes());
    }
    h.finalize().iter().map(|b| format!("{b:02x}")).collect()
}

struct ModeResult {
    name: String,
    tokens: Vec<Vec<u32>>,
    logit_sha: Vec<Vec<String>>,
}

fn cmd_run(args: &[String]) {
    let ckpt = PathBuf::from(need(args, "--checkpoint"));
    let prompts_file = need(args, "--prompts");
    let new: usize = need(args, "--new").parse().unwrap();
    let out = PathBuf::from(need(args, "--out"));
    let modes: Vec<String> = need(args, "--modes").split(',').map(String::from).collect();
    std::fs::create_dir_all(&out).unwrap();
    let prompts: Vec<String> = std::fs::read_to_string(&prompts_file)
        .unwrap()
        .lines()
        .filter(|l| !l.trim().is_empty())
        .map(|l| l.replace("\\n", "\n"))
        .collect();
    let tk = Tokenizer::from_file(ckpt.join("tokenizer.json")).expect("tokenizer");
    let enc: Vec<Vec<u32>> = prompts.iter().map(|p| tk.encode(p).expect("encode")).collect();
    let t = Instant::now();
    let w = load_qwen3_coder(&ckpt).expect("load CPU reference");
    let load_s = t.elapsed().as_secs_f64();
    let lut: [f32; 256] = std::array::from_fn(|b| fp8_e4m3fn_to_f32(b as u8));
    let mut summary = std::fs::File::create(out.join("summary.txt")).unwrap();
    let mut log = |s: String| {
        println!("{s}");
        writeln!(summary, "{s}").unwrap();
        summary.flush().unwrap();
    };
    log(format!(
        "load_s {load_s:.1} prompts {} prompt_tokens {:?} new {new} slot_bytes {SLOT_BYTES} threads {}",
        prompts.len(),
        enc.iter().map(|e| e.len()).collect::<Vec<_>>(),
        rayon::current_num_threads()
    ));
    let mut slot_fail = false;
    let mut results: Vec<ModeResult> = Vec::new();
    for mode in &modes {
        let mut cache = None;
        if let Some(rest) = mode.strip_prefix("stream:") {
            let mut it = rest.split(':');
            let cap: f64 = it.next().unwrap().parse().unwrap();
            let gb: f64 = it.next().unwrap().parse().unwrap();
            cache = Some(SlotCache::new(expert_map(&ckpt), cap, gb * 1e9));
        }
        let mut steps = std::fs::File::create(out.join(format!("steps-{}.tsv", mode.replace(':', "_")))).unwrap();
        writeln!(steps, "prompt\tpos\tdecode\ttoken\twall_ms\tcompute_ms\tstall_ms\tmisses\tdisk_bytes").unwrap();
        let mut trace = std::fs::File::create(out.join(format!("trace-{}.txt", mode.replace(':', "_")))).unwrap();
        let mut res = ModeResult { name: mode.clone(), tokens: vec![], logit_sha: vec![] };
        let (mut dec_wall, mut dec_stall, mut dec_n, mut dec_disk, mut dec_miss) = (0f64, 0f64, 0usize, 0u64, 0u64);
        let t_mode = Instant::now();
        for (pi, prompt) in enc.iter().enumerate() {
            let mut st = Qwen3CoderState::new();
            let mut toks = Vec::new();
            let mut shas = Vec::new();
            let total = prompt.len() + new;
            let mut cur = prompt[0];
            for pos in 0..total - 1 {
                let decode = pos + 1 >= prompt.len();
                let before = cache.as_ref().map(|c: &SlotCache| c.stats).unwrap_or_default();
                let t0 = Instant::now();
                let (hidden, route, compute) = if mode == "ref" {
                    (forward_token(&w, cur, pos, &mut st), vec![], t0.elapsed())
                } else {
                    let mut src = match cache.as_mut() {
                        Some(c) => Experts::Stream(c),
                        None => Experts::Resident,
                    };
                    let o = forward(&w, &mut src, cur, pos, &mut st, &lut);
                    (o.hidden, o.route, o.compute)
                };
                let mut next = None;
                if decode {
                    let logits = compute_logits(&w, &hidden);
                    let (n, _) = sample_argmax(&logits);
                    shas.push(sha_f32(&logits));
                    toks.push(n);
                    next = Some(n);
                }
                let wall = t0.elapsed();
                let after = cache.as_ref().map(|c| c.stats).unwrap_or_default();
                let stall = after.stall - before.stall;
                let (miss, disk) = (after.misses - before.misses, after.disk_bytes - before.disk_bytes);
                writeln!(
                    steps,
                    "{pi}\t{pos}\t{}\t{}\t{:.3}\t{:.3}\t{:.3}\t{miss}\t{disk}",
                    decode as u8,
                    next.map(|n| n as i64).unwrap_or(-1),
                    wall.as_secs_f64() * 1e3,
                    compute.as_secs_f64() * 1e3,
                    stall.as_secs_f64() * 1e3
                )
                .unwrap();
                for (l, ids) in route.iter().enumerate() {
                    let s: Vec<String> = ids.iter().map(|e| e.to_string()).collect();
                    writeln!(trace, "{pi} {pos} {} {l} {}", decode as u8, s.join(" ")).unwrap();
                }
                if decode {
                    dec_wall += wall.as_secs_f64();
                    dec_stall += stall.as_secs_f64();
                    dec_n += 1;
                    dec_disk += disk;
                    dec_miss += miss;
                }
                cur = if decode { next.unwrap() } else { prompt[pos + 1] };
            }
            res.tokens.push(toks);
            res.logit_sha.push(shas);
        }
        let mut line = format!(
            "mode {mode} positions_s {:.1} decode_tokens {dec_n} decode_tok_s {:.4} decode_ms {:.1} decode_stall_ms {:.1}",
            t_mode.elapsed().as_secs_f64(),
            dec_n as f64 / dec_wall,
            dec_wall / dec_n as f64 * 1e3,
            dec_stall / dec_n as f64 * 1e3
        );
        if let Some(c) = &cache {
            let s = c.stats;
            line += &format!(
                " slots {} hit_rate {:.4} misses {} reads {} disk_bytes {} decode_miss_per_tok {:.2} decode_disk_MB_per_tok {:.1} sleep_s {:.1} mismatched_slots {}",
                c.n_slots(),
                s.hits as f64 / (s.hits + s.misses) as f64,
                s.misses,
                s.reads,
                s.disk_bytes,
                dec_miss as f64 / dec_n as f64,
                dec_disk as f64 / dec_n as f64 / 1e6,
                s.sleep.as_secs_f64(),
                s.mismatched_slots
            );
            if s.mismatched_slots != 0 {
                line += " SLOT-MISMATCH";
                slot_fail = true;
            }
        }
        log(line);
        results.push(res);
    }
    // Identity gate: every mode against the first one (ref).
    // Identity gate: every streamed mode against ref. A run without ref (trace recording) has no gate and says so.
    let mut fail = slot_fail;
    if slot_fail {
        log("gate slots: a streamed expert differed from the resident copy => FAIL".to_string());
    }
    if modes[0] != "ref" {
        log(format!("gate: none (first mode is {}, not ref; trace or timing only)", modes[0]));
    }
    let base = &results[0];
    for r in results.iter().skip(1).filter(|_| modes[0] == "ref") {
        let tok_same = r.tokens == base.tokens;
        let logit_same = r.logit_sha == base.logit_sha;
        let n: usize = r.tokens.iter().map(|t| t.len()).sum();
        log(format!(
            "gate {} vs {}: tokens {} logits_bit_identical {} ({} decode steps) => {}",
            r.name,
            base.name,
            if tok_same { "identical" } else { "DIFFER" },
            logit_same,
            n,
            if tok_same && logit_same { "PASS" } else { "FAIL" }
        ));
        fail |= !(tok_same && logit_same);
    }
    for (pi, toks) in base.tokens.iter().enumerate() {
        log(format!("text {pi}: {:?}", tk.decode(toks).unwrap_or_default()));
    }
    std::process::exit(if fail { 1 } else { 0 });
}

// ---------------------------------------------------------------- sim

fn cmd_sim(args: &[String]) {
    let trace = std::fs::read_to_string(need(args, "--trace")).unwrap();
    let decode_only = args.iter().any(|a| a == "--decode-only");
    let slot_bytes: f64 = arg(args, "--slot-bytes").map(|s| s.parse().unwrap()).unwrap_or(SLOT_BYTES as f64);
    let gbs: Vec<f64> = need(args, "--cache-gb").split(',').map(|s| s.parse().unwrap()).collect();
    // Access sequence in execution order, plus a per-access flag for decode positions.
    let mut seq: Vec<(u32, bool)> = Vec::new();
    for line in trace.lines() {
        let f: Vec<u32> = line.split(' ').map(|x| x.parse().unwrap()).collect();
        let (dec, layer) = (f[2] == 1, f[3]);
        for e in &f[4..] {
            seq.push((layer * EXPERTS as u32 + e, dec));
        }
    }
    let tokens = seq.len() / (LAYERS * TOPK);
    let distinct = {
        let mut v: Vec<u32> = seq.iter().map(|s| s.0).collect();
        v.sort();
        v.dedup();
        v.len()
    };
    println!("accesses {} tokens {tokens} distinct_experts {distinct} of {} slot_bytes {slot_bytes}", seq.len(), LAYERS * EXPERTS);
    // Next-use index for Belady.
    let mut next_use = vec![usize::MAX; seq.len()];
    let mut last: HashMap<u32, usize> = HashMap::new();
    for i in (0..seq.len()).rev() {
        next_use[i] = *last.get(&seq[i].0).unwrap_or(&usize::MAX);
        last.insert(seq[i].0, i);
    }
    println!("cache_gb\tslots\tlru_hit\tbelady_hit\tlru_miss_per_tok\tbelady_miss_per_tok\t(counted over {} accesses)", if decode_only { "decode" } else { "all" });
    for gb in gbs {
        let n = ((gb * 1e9 / slot_bytes).floor() as usize).max(TOPK);
        // LRU
        let mut lru: HashMap<u32, usize> = HashMap::new();
        let (mut lh, mut lm) = (0u64, 0u64);
        for (i, &(k, dec)) in seq.iter().enumerate() {
            let hit = lru.contains_key(&k);
            if !hit && lru.len() >= n {
                let victim = *lru.iter().min_by_key(|(_, &t)| t).unwrap().0;
                lru.remove(&victim);
            }
            lru.insert(k, i);
            if !decode_only || dec {
                if hit { lh += 1 } else { lm += 1 }
            }
        }
        // Belady (evict the resident expert used farthest in the future)
        let mut res: HashMap<u32, usize> = HashMap::new();
        let (mut bh, mut bm) = (0u64, 0u64);
        for (i, &(k, dec)) in seq.iter().enumerate() {
            let hit = res.contains_key(&k);
            if !hit && res.len() >= n {
                let victim = *res.iter().max_by_key(|(_, &nu)| nu).unwrap().0;
                res.remove(&victim);
            }
            res.insert(k, next_use[i]);
            if !decode_only || dec {
                if hit { bh += 1 } else { bm += 1 }
            }
        }
        let counted_tok = (lh + lm) as f64 / TOPK as f64 / LAYERS as f64;
        println!(
            "{gb}\t{n}\t{:.4}\t{:.4}\t{:.1}\t{:.1}",
            lh as f64 / (lh + lm) as f64,
            bh as f64 / (bh + bm) as f64,
            lm as f64 / counted_tok,
            bm as f64 / counted_tok
        );
    }
}

fn cmd_layout(args: &[String]) {
    let m = expert_map(Path::new(&need(args, "--checkpoint")));
    let (mut contiguous, mut total) = (0usize, 0usize);
    for p in &m.parts {
        let lo = p.iter().map(|x| x.off).min().unwrap();
        let hi = p.iter().map(|x| x.off + x.len).max().unwrap();
        let sum: u64 = p.iter().map(|x| x.len).sum();
        total += 1;
        if p.iter().all(|x| x.file == p[0].file) && hi - lo <= sum + (64 << 10) {
            contiguous += 1;
        }
    }
    let p = &m.parts[0];
    println!("experts {total} single_span {contiguous} slot_bytes {SLOT_BYTES} layer0_expert0 {:?}", p);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    match args.get(1).map(String::as_str) {
        Some("run") => cmd_run(&args),
        Some("sim") => cmd_sim(&args),
        Some("layout") => cmd_layout(&args),
        _ => die("usage: moe_stream run|sim|layout ..."),
    }
}
