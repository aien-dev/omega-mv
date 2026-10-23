// ---------------------------------------------------------------------------
// MXFP4 (4-bit) projections: the aien-rig 12 GiB path.
// ---------------------------------------------------------------------------

/// One `[n][k]` projection converted at load from Qwen's FP8 (e4m3, 128x128 block scales) to OCP
/// MXFP4 (e2m1 nibbles, one E8M0 scale per 32 weights along k) and held on the chip; decode runs
/// omega's W4 GEMV (`omega_gpu_w4_api.h`, cascade/w4-gemv), one launch per projection. Prefill runs
/// the same GEMV once per row (no 4-bit GEMM yet), so prompt and decode share one kernel.
#[derive(Debug)]
pub struct OmegaW4DenseGemv(Mutex<aien_omega_q38::W4Tensor>, usize, usize);

impl DenseGemv for OmegaW4DenseGemv {
    fn n(&self) -> usize {
        self.1
    }
    fn k(&self) -> usize {
        self.2
    }
    fn gemv_f32(&self, x: &[f32], y: &mut [f32]) -> Result<u64, String> {
        let t = self.0.lock().map_err(|_| "w4 tensor lock poisoned")?;
        Ok(t.gemv_f32(x, y).map_err(|e| e.to_string())?.elapsed_ns)
    }
}

/// The host twin of [`OmegaW4DenseGemv`]: the SAME packed MXFP4 bytes, multiplied by omega's host
/// oracle (`omega_w4_gemv_oracle`: exact weight decode, double sums in column order), 128-row
/// stripes in parallel (each row's sum is sequential, so the thread count does not change it).
/// Oracle for the 4-bit chip run: tokens and layers are compared against the same rounding.
#[derive(Debug)]
pub struct HostW4DenseGemv {
    n: usize,
    k: usize,
    w4: Vec<u8>,
    e8: Vec<u8>,
}

impl DenseGemv for HostW4DenseGemv {
    fn n(&self) -> usize {
        self.n
    }
    fn k(&self) -> usize {
        self.k
    }
    fn gemv_f32(&self, x: &[f32], y: &mut [f32]) -> Result<u64, String> {
        let (n, k) = (self.n, self.k);
        if x.len() != k || y.len() != n {
            return Err(format!("w4 host gemv [{n}][{k}] given x {} y {}", x.len(), y.len()));
        }
        const STRIPE: usize = 128;
        y.par_chunks_mut(STRIPE)
            .enumerate()
            .try_for_each(|(i, out)| {
                let r0 = i * STRIPE;
                let rows = out.len();
                let w = &self.w4[r0 * k / 2..(r0 + rows) * k / 2];
                let e = &self.e8[r0 * k / 32..(r0 + rows) * k / 32];
                let got = aien_omega_q38::w4_gemv_oracle(rows, k, w, e, x).map_err(|e| e.to_string())?;
                out.iter_mut().zip(got).for_each(|(o, v)| *o = v as f32);
                Ok::<(), String>(())
            })?;
        Ok(0)
    }
}

/// Converter error tally across one load (omega `OmegaW4Stats`), for the load line and receipts.
#[derive(Debug, Clone, Copy, Default)]
pub struct W4LoadStats {
    pub tensors: u64,
    pub stats: aien_omega_q38::W4Stats,
}

/// Uploads a Qwen FP8 checkpoint with every FP8 projection converted to MXFP4 (the group scale rule
/// from `AIEN_Q38_W4_RULE`: floor, the OCP rule and default; ceil; mse). bf16 tensors (the lm_head,
/// a tied head) go to `inner` unchanged. `host` = keep the packed bytes on the host
/// ([`HostW4DenseGemv`], the oracle); otherwise upload them to the chip ([`OmegaW4DenseGemv`]).
pub struct W4DenseUploader<'a> {
    inner: &'a dyn DenseUploader,
    host: bool,
    tally: Mutex<W4LoadStats>,
}

impl<'a> W4DenseUploader<'a> {
    pub fn chip(inner: &'a dyn DenseUploader) -> Self {
        Self { inner, host: false, tally: Mutex::new(W4LoadStats::default()) }
    }
    pub fn host(inner: &'a dyn DenseUploader) -> Self {
        Self { inner, host: true, tally: Mutex::new(W4LoadStats::default()) }
    }
    /// What the converter did so far (relative RMS weight error, clipped groups).
    pub fn stats(&self) -> W4LoadStats {
        self.tally.lock().map(|t| *t).unwrap_or_default()
    }
    /// Whether this binary links omega's MXFP4 converter and GEMV.
    pub fn native() -> bool {
        aien_omega_q38::w4_native()
    }
}

impl DenseUploader for W4DenseUploader<'_> {
    fn upload_bf16(
        &self,
        layer: Option<usize>,
        what: DenseTensor,
        n: usize,
        k: usize,
        bits: &[u16],
    ) -> Result<Arc<dyn DenseGemv>, String> {
        self.inner.upload_bf16(layer, what, n, k, bits)
    }
    fn upload_fp8(
        &self,
        _layer: Option<usize>,
        what: DenseTensor,
        n: usize,
        k: usize,
        w: &[u8],
        scales: &[u16],
    ) -> Result<Arc<dyn DenseGemv>, String> {
        if !Self::native() {
            return Err("this binary carries no omega MXFP4 converter/GEMV (archive without omega_gpu_w4_api)".into());
        }
        check_fp8_upload(n, k, w, scales)?;
        let threads = std::thread::available_parallelism().map_or(8, |p| p.get()).min(20);
        let (w4, e8, st) = aien_omega_q38::w4_from_fp8(n, k, w, scales, threads)
            .map_err(|e| format!("{} [{n}][{k}] to MXFP4: {e}", what.name()))?;
        if let Ok(mut t) = self.tally.lock() {
            t.tensors += 1;
            t.stats.add(&st);
        }
        if self.host {
            return Ok(Arc::new(HostW4DenseGemv { n, k, w4, e8 }));
        }
        let t = aien_omega_q38::W4Tensor::upload(n, k, &w4, &e8)
            .map_err(|e| format!("{} [{n}][{k}] MXFP4 upload: {e}", what.name()))?;
        Ok(Arc::new(OmegaW4DenseGemv(Mutex::new(t), n, k)))
    }
}

