/// One MXFP4 GEMV's facts (omega `OmegaGpuW4Info`): kernel shape, bytes streamed, chip time.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct W4Info {
    pub n: u32,
    pub k: u32,
    pub lane_bytes: u32,
    pub rows_per_warp: u32,
    pub warps_per_cta: u32,
    pub coal: u32,
    pub sched: u32,
    pub grid_x: u32,
    pub steps: u32,
    pub weight_bytes: u64,
    pub scale_bytes: u64,
    pub elapsed_ns: u64,
    pub call_ns: u64,
    pub kernel_cache_hit: bool,
}

impl W4Tensor {
    /// `y = W x` on the chip, one launch (omega `omega_gpu_w4_gemv_f32`: f32 accumulate, output
    /// poison-checked). `x` is the plain activation row; omega packs it. Used by the dense Qwen3
    /// engine (aien-rig, one launch per projection).
    pub fn gemv_f32(&self, x: &[f32], y: &mut [f32]) -> Result<W4Info, OmegaGpuError> {
        check_len("w4 gemv x", self.k, x.len())?;
        check_len("w4 gemv y", self.n, y.len())?;
        #[cfg(has_omega_q38_w4)]
        {
            let mut info = W4Info::default();
            // SAFETY: ptr is live; x and y hold k and n floats (checked above).
            let rc = unsafe {
                ffi_w4::omega_gpu_w4_gemv_f32(self.ptr, x.as_ptr(), y.as_mut_ptr(), &mut info)
            };
            if rc != 0 {
                return Err(w4_err("w4 gemv", rc));
            }
            Ok(info)
        }
        #[cfg(not(has_omega_q38_w4))]
        Err(OmegaGpuError::Unavailable)
    }
}

/// omega's host oracle for a packed MXFP4 `[n][k]` (`omega_w4_gemv_oracle`: every weight decoded
/// exactly, sums in double, columns in order). Host only; the reference for the chip GEMV.
pub fn w4_gemv_oracle(
    n: usize,
    k: usize,
    w4: &[u8],
    e8: &[u8],
    x: &[f32],
) -> Result<Vec<f64>, OmegaGpuError> {
    check_len("w4 oracle nibbles", n * k / 2, w4.len())?;
    check_len("w4 oracle scales", n * k / 32, e8.len())?;
    check_len("w4 oracle x", k, x.len())?;
    #[cfg(has_omega_q38_w4)]
    {
        let mut y = vec![0f64; n];
        // SAFETY: the slices hold the lengths checked above.
        unsafe {
            ffi_w4::omega_w4_gemv_oracle(
                w4.as_ptr(),
                e8.as_ptr(),
                n as u32,
                k as u32,
                x.as_ptr(),
                y.as_mut_ptr(),
            )
        };
        Ok(y)
    }
    #[cfg(not(has_omega_q38_w4))]
    Err(OmegaGpuError::Unavailable)
}

