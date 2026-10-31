//! Number parsing and formatting with exactly CPython's semantics for the inputs the
//! relay sees (ASCII fields split on `|`). Wire compatibility depends on these: the
//! Python relay accepts `float(" 1_0e1 ")` and re-emits poses with `format(x, '.9g')`.

/// The whitespace `float()`/`int()` strip from ASCII text.
fn is_py_space(byte: u8) -> bool {
    matches!(byte, b' ' | b'\t' | b'\n' | 0x0b | 0x0c | b'\r')
}

fn trim(text: &str) -> &str {
    let bytes = text.as_bytes();
    let start = bytes.iter().position(|&b| !is_py_space(b)).unwrap_or(bytes.len());
    let end = bytes.iter().rposition(|&b| !is_py_space(b)).map_or(start, |i| i + 1);
    &text[start..end]
}

/// Consume `digit (_? digit)*`, appending digits to `out`. Returns the bytes used
/// (0 when no digit is present) or None on a misplaced underscore.
fn digit_part(bytes: &[u8], out: &mut String) -> Option<usize> {
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'0'..=b'9' => {
                out.push(bytes[i] as char);
                i += 1;
            }
            b'_' if i > 0 && bytes.get(i + 1).is_some_and(u8::is_ascii_digit) => i += 1,
            b'_' => return None,
            _ => break,
        }
    }
    Some(i)
}

/// `float(text)` for ASCII text. `None` where CPython raises ValueError.
pub fn py_float(text: &str) -> Option<f64> {
    let s = trim(text);
    let (negative, body) = match s.as_bytes().first() {
        Some(b'-') => (true, &s[1..]),
        Some(b'+') => (false, &s[1..]),
        _ => (false, s),
    };
    let lower = body.to_ascii_lowercase();
    let special = match lower.as_str() {
        "inf" | "infinity" => Some(f64::INFINITY),
        "nan" => Some(f64::NAN),
        _ => None,
    };
    if let Some(value) = special {
        return Some(if negative { -value } else { value });
    }
    let bytes = body.as_bytes();
    let mut clean = String::with_capacity(bytes.len() + 1);
    if negative {
        clean.push('-');
    }
    let mut i = digit_part(bytes, &mut clean)?;
    let mut digits = i > 0;
    if bytes.get(i) == Some(&b'.') {
        clean.push('.');
        i += 1;
        let used = digit_part(&bytes[i..], &mut clean)?;
        digits |= used > 0;
        i += used;
    }
    if !digits {
        return None;
    }
    if matches!(bytes.get(i), Some(b'e' | b'E')) {
        clean.push('e');
        i += 1;
        if let Some(&sign @ (b'+' | b'-')) = bytes.get(i) {
            clean.push(sign as char);
            i += 1;
        }
        let used = digit_part(&bytes[i..], &mut clean)?;
        if used == 0 {
            return None;
        }
        i += used;
    }
    if i != bytes.len() {
        return None;
    }
    // Rust's parser is correctly rounded like CPython's, and overflows to infinity.
    clean.parse::<f64>().ok()
}

/// `int(text)` (base 10) for ASCII text, saturating far outside any relay bound.
pub fn py_int(text: &str) -> Option<i128> {
    let s = trim(text);
    let (negative, body) = match s.as_bytes().first() {
        Some(b'-') => (true, &s[1..]),
        Some(b'+') => (false, &s[1..]),
        _ => (false, s),
    };
    let mut digits = String::new();
    let used = digit_part(body.as_bytes(), &mut digits)?;
    if used == 0 || used != body.len() {
        return None;
    }
    let magnitude = digits.bytes().fold(0i128, |acc, d| acc.saturating_mul(10).saturating_add(i128::from(d - b'0')));
    Some(if negative { -magnitude } else { magnitude })
}

/// `format(x, '.{precision}g')` / `'%.{precision}g' % x`.
pub fn fmt_g(x: f64, precision: usize) -> String {
    if x.is_nan() {
        return "nan".into();
    }
    if x.is_infinite() {
        return if x > 0.0 { "inf".into() } else { "-inf".into() };
    }
    let p = precision.max(1);
    // Rust's exact formatting rounds the binary value half-to-even, as CPython does.
    let scientific = format!("{:.*e}", p - 1, x);
    let (mantissa, exponent) = scientific.split_once('e').expect("exponent form");
    let exponent: i32 = exponent.parse().expect("exponent digits");
    if -4 <= exponent && exponent < p as i32 {
        let fixed = format!("{:.*}", (p as i32 - 1 - exponent) as usize, x);
        strip_zeros(&fixed).to_string()
    } else {
        let sign = if exponent < 0 { '-' } else { '+' };
        format!("{}e{}{:02}", strip_zeros(mantissa), sign, exponent.abs())
    }
}

fn strip_zeros(text: &str) -> &str {
    if text.contains('.') {
        text.trim_end_matches('0').trim_end_matches('.')
    } else {
        text
    }
}

/// `f'{x:.{decimals}f}'`.
pub fn fmt_f(x: f64, decimals: usize) -> String {
    if x.is_nan() {
        return "nan".into();
    }
    if x.is_infinite() {
        return if x > 0.0 { "inf".into() } else { "-inf".into() };
    }
    format!("{:.*}", decimals, x)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn float_grammar_matches_cpython() {
        // Expectations recorded from CPython 3.12.
        let ok = [
            (" 1 ", 1.0),
            ("\x0b1", 1.0),
            ("1_0", 10.0),
            ("1.5_0", 1.5),
            ("1e1_0", 1e10),
            (".5", 0.5),
            ("5.", 5.0),
            ("+.5e-3", 0.0005),
            ("1E+3", 1000.0),
            ("-0", -0.0),
            ("1e400", f64::INFINITY),
        ];
        for (text, value) in ok {
            assert_eq!(py_float(text), Some(value), "{text:?}");
        }
        assert!(py_float("-nan").unwrap().is_nan());
        assert_eq!(py_float("iNfInItY"), Some(f64::INFINITY));
        assert_eq!(py_float("+inf"), Some(f64::INFINITY));
        for bad in
            ["\x1c1", "1\x1f", "1__0", "_1", "1_", "1_.5", "1._5", "1e_1", ".", "infinit", "1e", "e5", "0x10", "", " "]
        {
            assert_eq!(py_float(bad), None, "{bad:?}");
        }
    }

    #[test]
    fn int_grammar_matches_cpython() {
        for (text, value) in [(" 5", 5), ("+5", 5), ("-0", 0), ("5_0", 50), ("05", 5), ("0_0", 0)] {
            assert_eq!(py_int(text), Some(value));
        }
        for bad in ["", " ", "1 2", "\x1c5", "_5", "5_", "1.0"] {
            assert_eq!(py_int(bad), None, "{bad:?}");
        }
    }

    #[test]
    fn formatting_matches_cpython() {
        let g9 = [
            (0.125, "0.125"),
            (1e16, "1e+16"),
            (123456789.5, "123456790"),
            (1234567885.0, "1.23456788e+09"),
            (2.5, "2.5"),
            (1e-5, "1e-05"),
            (0.0001, "0.0001"),
            (-0.0, "-0"),
            (1e100, "1e+100"),
            (0.1, "0.1"),
            (100000000.0, "100000000"),
            (999999999.5, "1e+09"),
        ];
        for (x, text) in g9 {
            assert_eq!(fmt_g(x, 9), text, "{x}");
        }
        let g17 = [
            (0.1, "0.10000000000000001"),
            (1e-5, "1.0000000000000001e-05"),
            (1e22, "1e+22"),
            (5e-324, "4.9406564584124654e-324"),
            (-0.0, "-0"),
            (1.0, "1"),
            (123.456, "123.456"),
        ];
        for (x, text) in g17 {
            assert_eq!(fmt_g(x, 17), text, "{x}");
        }
        for (x, text) in [(0.0005, "0.001"), (0.0015, "0.002"), (0.0025, "0.003"), (1.0005, "1.000"), (-0.0, "-0.000")]
        {
            assert_eq!(fmt_f(x, 3), text, "{x}");
        }
    }
}
