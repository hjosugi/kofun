use std::panic::catch_unwind;
use unicode_segmentation::UnicodeSegmentation;

fn direct_count(bytes: &[u8]) -> (i32, usize, usize) {
    match std::str::from_utf8(bytes) {
        Ok(text) => (0, text.graphemes(true).count(), 0),
        Err(error) => (1, 0, error.valid_up_to()),
    }
}

fn main() {
    assert_eq!(std::str::from_utf8(b"\x65\xCC\x81").unwrap().chars().count(), 2);
    let valid = direct_count(b"\x65\xCC\x81");
    let invalid = direct_count(&[0xFF]);
    let repeated = direct_count(b"\x65\xCC\x81");
    let panic_status = if catch_unwind(
        || panic!("intentional direct Rust panic probe"),
    )
    .is_err()
    {
        2
    } else {
        0
    };

    println!("{}", valid.0);
    println!("{}", valid.1);
    println!("{}", invalid.0);
    println!("{}", invalid.2);
    println!("{}", repeated.1);
    println!("{panic_status}");
}
