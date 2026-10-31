//! Input/output helpers: transparent gzip detection and gzipped output.

use std::fs::File;
use std::io::{self, BufRead, BufReader, BufWriter, Read, Write};
use std::path::Path;

use flate2::read::MultiGzDecoder;
use flate2::write::GzEncoder;
use flate2::Compression;

const BUF: usize = 1 << 20;

/// Opens a save for streaming. Gzip (`1f 8b`) is detected from the content, so plain
/// `.xml` fixtures work too.
pub fn open_input(path: &Path) -> io::Result<Box<dyn BufRead>> {
    let mut f = BufReader::with_capacity(BUF, File::open(path)?);
    let magic = f.fill_buf()?;
    if magic.len() >= 2 && magic[0] == 0x1f && magic[1] == 0x8b {
        Ok(Box::new(BufReader::with_capacity(BUF, MultiGzDecoder::new(f))))
    } else {
        Ok(Box::new(f))
    }
}

/// Wraps any reader (used by tests with in-memory fixtures).
pub fn from_bytes(bytes: &[u8]) -> Box<dyn BufRead + '_> {
    if bytes.len() >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b {
        Box::new(BufReader::new(MultiGzDecoder::new(bytes)))
    } else {
        Box::new(bytes)
    }
}

/// Creates a gzipped output file (X4 reads `.xml.gz` saves).
pub fn create_gz_output(path: &Path) -> io::Result<GzEncoder<BufWriter<File>>> {
    let f = BufWriter::with_capacity(BUF, File::create(path)?);
    Ok(GzEncoder::new(f, Compression::new(6)))
}

/// Gzip writer that compresses on a background thread, so parsing/filtering and
/// deflate overlap. Call [`ThreadedGzWriter::finish`] to flush and join.
pub struct ThreadedGzWriter {
    buf: Vec<u8>,
    tx: Option<std::sync::mpsc::SyncSender<Vec<u8>>>,
    handle: Option<std::thread::JoinHandle<io::Result<()>>>,
}

impl ThreadedGzWriter {
    pub fn create(path: &Path) -> io::Result<Self> {
        let mut enc = create_gz_output(path)?;
        let (tx, rx) = std::sync::mpsc::sync_channel::<Vec<u8>>(8);
        let handle = std::thread::spawn(move || -> io::Result<()> {
            for chunk in rx {
                enc.write_all(&chunk)?;
            }
            enc.finish()?.flush()
        });
        Ok(ThreadedGzWriter { buf: Vec::with_capacity(BUF), tx: Some(tx), handle: Some(handle) })
    }

    fn send(&mut self) -> io::Result<()> {
        if self.buf.is_empty() {
            return Ok(());
        }
        let chunk = std::mem::replace(&mut self.buf, Vec::with_capacity(BUF));
        if self.tx.as_ref().expect("writer finished").send(chunk).is_err() {
            return self.join();
        }
        Ok(())
    }

    fn join(&mut self) -> io::Result<()> {
        self.tx = None;
        match self.handle.take() {
            Some(h) => h.join().map_err(|_| io::Error::other("gzip thread panicked"))?,
            None => Ok(()),
        }
    }

    pub fn finish(mut self) -> io::Result<()> {
        self.send()?;
        self.join()
    }
}

impl Write for ThreadedGzWriter {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        self.buf.extend_from_slice(data);
        if self.buf.len() >= BUF {
            self.send()?;
        }
        Ok(data.len())
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

/// Counts bytes passing through a reader (uncompressed size).
pub struct CountingReader<R> {
    pub inner: R,
    pub count: u64,
}

impl<R: Read> Read for CountingReader<R> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let n = self.inner.read(buf)?;
        self.count += n as u64;
        Ok(n)
    }
}

impl<R: BufRead> BufRead for CountingReader<R> {
    fn fill_buf(&mut self) -> io::Result<&[u8]> {
        self.inner.fill_buf()
    }
    fn consume(&mut self, amt: usize) {
        self.count += amt as u64;
        self.inner.consume(amt)
    }
}

/// Counts bytes written (uncompressed output size).
pub struct CountingWriter<W> {
    pub inner: W,
    pub count: u64,
}

impl<W: Write> Write for CountingWriter<W> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let n = self.inner.write(buf)?;
        self.count += n as u64;
        Ok(n)
    }
    fn flush(&mut self) -> io::Result<()> {
        self.inner.flush()
    }
}

/// Parses `[0x1a2b]` into its numeric value.
pub fn parse_hex_id(v: &[u8]) -> Option<u64> {
    let inner = v.strip_prefix(b"[0x")?.strip_suffix(b"]")?;
    if inner.is_empty() || inner.len() > 16 {
        return None;
    }
    let mut n: u64 = 0;
    for &c in inner {
        let d = match c {
            b'0'..=b'9' => c - b'0',
            b'a'..=b'f' => c - b'a' + 10,
            b'A'..=b'F' => c - b'A' + 10,
            _ => return None,
        };
        n = (n << 4) | d as u64;
    }
    Some(n)
}

/// Parses a non-negative decimal integer.
pub fn parse_dec(v: &[u8]) -> Option<u64> {
    if v.is_empty() || v.len() > 19 {
        return None;
    }
    let mut n: u64 = 0;
    for &c in v {
        if !c.is_ascii_digit() {
            return None;
        }
        n = n * 10 + (c - b'0') as u64;
    }
    Some(n)
}

pub fn fmt_id(id: u64) -> String {
    format!("[0x{id:x}]")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ids() {
        assert_eq!(parse_hex_id(b"[0x1a]"), Some(0x1a));
        assert_eq!(parse_hex_id(b"[0x]"), None);
        assert_eq!(parse_hex_id(b"0x1a"), None);
        assert_eq!(parse_dec(b"7149"), Some(7149));
        assert_eq!(parse_dec(b"-1"), None);
        assert_eq!(fmt_id(0x17), "[0x17]");
    }
}
