//! Comparison for corpus item 2: a parser returning `Result` through three
//! fallible steps.
//!
//! Language: Rust. Chosen because #624 cites Rust's `Result` propagation
//! model, and DD-036 (spec/result-propagation-v1.md) adopts postfix `?`
//! explicitly modelled on it. This file is what corpus item 2 would look
//! like if DD-036 were implemented.
//!
//! Compiled and run by the gate: `rustc -O 02_parser_result.rs && ./binary`
//! prints `420` then `-2`, the same two observations as the Kofun file.
//!
//! What to compare:
//!
//!   * `parse` is four statements with no nesting. The Kofun version needs
//!     three nested `match` blocks, and repeats the error arm verbatim at
//!     each level, because `?` does not exist yet.
//!   * `Result<i64, ParseError>` is the standard library's generic type.
//!     Kofun cannot declare it — generic ADTs are refused (E2S45) — so
//!     corpus item 2 defines a monomorphic `ParseResult` that is usable for
//!     exactly one payload type.
//!   * The error type is named once per signature. Nothing else annotates.

#[derive(Debug, PartialEq)]
enum ParseError {
    Negative,
    OutOfRange,
    BadChecksum,
}

impl ParseError {
    /// Matches the negative codes the Kofun file prints, so the two programs
    /// produce identical output.
    fn code(&self) -> i64 {
        match self {
            ParseError::Negative => -1,
            ParseError::OutOfRange => -2,
            ParseError::BadChecksum => -3,
        }
    }
}

fn step_digits(source: i64) -> Result<i64, ParseError> {
    if source < 0 {
        return Err(ParseError::Negative);
    }
    Ok(source)
}

fn step_range(value: i64) -> Result<i64, ParseError> {
    if value > 999 {
        return Err(ParseError::OutOfRange);
    }
    Ok(value)
}

fn step_checksum(value: i64) -> Result<i64, ParseError> {
    if value % 2 == 1 {
        return Err(ParseError::BadChecksum);
    }
    Ok(value * 10)
}

/// Three fallible steps, flat. This is the whole comparison.
fn parse(source: i64) -> Result<i64, ParseError> {
    let digits = step_digits(source)?;
    let ranged = step_range(digits)?;
    let checked = step_checksum(ranged)?;
    Ok(checked)
}

fn main() {
    for source in [42, 1000] {
        match parse(source) {
            Ok(value) => println!("{value}"),
            Err(error) => println!("{}", error.code()),
        }
    }
}
