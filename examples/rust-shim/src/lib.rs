use std::ffi::{c_int, c_void};
use std::panic::{AssertUnwindSafe, catch_unwind};
use unicode_segmentation::UnicodeSegmentation;

pub const KOFUN_UNICODE_OK: c_int = 0;
pub const KOFUN_UNICODE_INVALID_UTF8: c_int = 1;
pub const KOFUN_UNICODE_PANIC: c_int = 2;
pub const KOFUN_UNICODE_NULL_BUFFER: c_int = 3;

#[repr(C)]
pub struct KofunGraphemeResult {
    pub status: c_int,
    pub count: usize,
    pub error_offset: usize,
}

impl KofunGraphemeResult {
    const fn new(status: c_int, count: usize, error_offset: usize) -> Self {
        Self {
            status,
            count,
            error_offset,
        }
    }
}

fn count_graphemes(bytes: *const c_void, length: usize) -> KofunGraphemeResult {
    if bytes.is_null() {
        return if length == 0 {
            KofunGraphemeResult::new(KOFUN_UNICODE_OK, 0, 0)
        } else {
            KofunGraphemeResult::new(KOFUN_UNICODE_NULL_BUFFER, 0, 0)
        };
    }

    // SAFETY: the C ABI contract requires a readable buffer of exactly
    // `length` bytes for this call. The slice never escapes this function.
    let input = unsafe { std::slice::from_raw_parts(bytes.cast::<u8>(), length) };
    match std::str::from_utf8(input) {
        Ok(text) => KofunGraphemeResult::new(
            KOFUN_UNICODE_OK,
            UnicodeSegmentation::graphemes(text, true).count(),
            0,
        ),
        Err(error) => KofunGraphemeResult::new(KOFUN_UNICODE_INVALID_UTF8, 0, error.valid_up_to()),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn kofun_unicode_grapheme_count(
    bytes: *const c_void,
    length: usize,
) -> KofunGraphemeResult {
    catch_unwind(AssertUnwindSafe(|| count_graphemes(bytes, length)))
        .unwrap_or_else(|_| KofunGraphemeResult::new(KOFUN_UNICODE_PANIC, 0, 0))
}

#[unsafe(no_mangle)]
pub extern "C" fn kofun_unicode_panic_probe() -> c_int {
    match catch_unwind(|| panic!("intentional Kofun C ABI panic probe")) {
        Ok(()) => KOFUN_UNICODE_OK,
        Err(_) => KOFUN_UNICODE_PANIC,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{align_of, offset_of, size_of};

    #[test]
    fn repr_c_layout_is_lp64_compatible() {
        assert_eq!(size_of::<KofunGraphemeResult>(), 24);
        assert_eq!(align_of::<KofunGraphemeResult>(), 8);
        assert_eq!(offset_of!(KofunGraphemeResult, status), 0);
        assert_eq!(offset_of!(KofunGraphemeResult, count), 8);
        assert_eq!(offset_of!(KofunGraphemeResult, error_offset), 16);
    }

    #[test]
    fn counts_a_grapheme_in_two_codepoints() {
        let input = b"\x65\xCC\x81";
        assert_eq!(std::str::from_utf8(input).unwrap().chars().count(), 2);
        let result = kofun_unicode_grapheme_count(input.as_ptr().cast::<c_void>(), input.len());
        assert_eq!(result.status, KOFUN_UNICODE_OK);
        assert_eq!(result.count, 1);
    }

    #[test]
    fn maps_invalid_utf8_and_null_without_panicking() {
        let invalid = [0xFF_u8];
        let result = kofun_unicode_grapheme_count(invalid.as_ptr().cast::<c_void>(), invalid.len());
        assert_eq!(result.status, KOFUN_UNICODE_INVALID_UTF8);
        assert_eq!(result.error_offset, 0);

        let null = kofun_unicode_grapheme_count(std::ptr::null(), 1);
        assert_eq!(null.status, KOFUN_UNICODE_NULL_BUFFER);
    }

    #[test]
    fn catches_panics_before_the_abi_boundary() {
        assert_eq!(kofun_unicode_panic_probe(), KOFUN_UNICODE_PANIC);
    }
}
