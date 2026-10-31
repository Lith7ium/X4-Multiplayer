//! Operating-system randomness for session leases (Python: `secrets.token_hex(16)`),
//! without a third-party crate.

#[cfg(windows)]
pub fn random16() -> [u8; 16] {
    #[link(name = "bcrypt")]
    extern "system" {
        fn BCryptGenRandom(algorithm: *mut core::ffi::c_void, buffer: *mut u8, size: u32, flags: u32) -> i32;
    }
    const BCRYPT_USE_SYSTEM_PREFERRED_RNG: u32 = 2;
    let mut out = [0u8; 16];
    // SAFETY: a valid 16-byte buffer and the documented system-RNG flag.
    let status =
        unsafe { BCryptGenRandom(std::ptr::null_mut(), out.as_mut_ptr(), 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) };
    assert!(status >= 0, "BCryptGenRandom failed: {status:#x}");
    out
}

#[cfg(not(windows))]
pub fn random16() -> [u8; 16] {
    use std::io::Read;
    let mut out = [0u8; 16];
    std::fs::File::open("/dev/urandom").and_then(|mut f| f.read_exact(&mut out)).expect("read /dev/urandom");
    out
}

#[cfg(test)]
mod tests {
    #[test]
    fn leases_differ() {
        assert_ne!(super::random16(), super::random16());
    }
}
