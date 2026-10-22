// Test adapter only. All transcript work calls the exact pinned Rust library.
use aien_replay::{compare_line, subsystem_name, verdict_line, verify, Verdict};
use std::io::{self, Read, Write};
fn payload(r: &mut impl Read) -> io::Result<Vec<u8>> {
    let mut n = [0; 8]; r.read_exact(&mut n)?;
    let n = usize::try_from(u64::from_le_bytes(n)).map_err(|_| io::ErrorKind::InvalidData)?;
    let mut b = vec![0; n]; r.read_exact(&mut b)?; Ok(b)
}
fn run() -> io::Result<()> {
    let mut r = io::stdin().lock(); let mut w = io::stdout().lock();
    loop {
        let mut op = [0];
        match r.read(&mut op)? { 0 => return Ok(()), _ => {} }
        match op[0] {
            b'V' | b'D' => {
                let v = verify(&payload(&mut r)?);
                writeln!(w, "{}", verdict_line(&v))?;
                if op[0] == b'D' {
                    if let Verdict::Accept(a) = v {
                        for (d, s) in a.compared {
                            let h: String = d.iter().map(|b| format!("{b:02x}")).collect();
                            writeln!(w, "digest {h} subsystem={s}")?;
                        }
                    }
                }
            }
            b'C' => {
                let e = verify(&payload(&mut r)?); let a = verify(&payload(&mut r)?);
                writeln!(w, "{}", compare_line(&e, &a))?;
            }
            b'S' => {
                let mut b = [0; 2]; r.read_exact(&mut b)?;
                writeln!(w, "{}", subsystem_name(u16::from_le_bytes(b)).unwrap_or("unknown"))?;
            }
            _ => return Err(io::ErrorKind::InvalidData.into()),
        }
        w.flush()?;
    }
}
fn main() {
    if run().is_err() { eprintln!("TRN1 transport error"); std::process::exit(2); }
}
