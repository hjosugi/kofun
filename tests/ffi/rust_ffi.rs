use std::ffi::{c_int, c_long};

#[repr(C)]
pub struct Pair {
    pub left: i64,
    pub right: i64,
    pub tag: i64,
}

#[no_mangle]
pub extern "C" fn rust_add(left: c_int, right: c_int) -> c_int {
    left + right
}

#[no_mangle]
pub extern "C" fn rust_stack_sum(
    one: c_long,
    two: c_long,
    three: c_long,
    four: c_long,
    five: c_long,
    six: c_long,
    seven: c_long,
    eight: c_long,
) -> c_long {
    one + two + three + four + five + six + seven + eight
}

#[no_mangle]
pub extern "C" fn rust_transform(value: Pair) -> Pair {
    Pair {
        left: value.left + 1,
        right: value.right + 2,
        tag: value.tag + 3,
    }
}
