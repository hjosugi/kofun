//! Comparison for corpus item 6: a Monad instance plus stated left
//! identity, right identity, and associativity.
//!
//! Language: Rust.
//!
//! READ THIS FIRST — the comparison is not like-for-like, and pretending it
//! were would be the strawman #909 forbids. None of the four languages #624
//! names can state a Monad instance together with its laws:
//!
//!   * Go and Gleam have no type classes or traits with laws at all.
//!   * Kotlin and Rust have traits/interfaces but no higher-kinded types, so
//!     `Monad` cannot be declared over a type constructor. Rust encodings
//!     using GATs exist, do not resemble anything a working Rust programmer
//!     writes, and would misrepresent the language.
//!
//! So this file does what an idiomatic Rust programmer actually does when
//! the laws matter: it fixes one concrete monad and states each law as a
//! `#[test]` that checks it exhaustively over the whole finite domain. That
//! is a fair comparison, because it is also what Kofun's accepted design
//! does — docs/LAW_SYSTEM.md grounds `A` and `MA` at concrete types and
//! checks equations over declared finite domains, with no higher-kinded
//! types in v1.
//!
//! Compiled and run by the gate: `rustc --test 06_monad_laws.rs && ./binary`
//! runs three law tests. The domain is `Option<bool>`, matching the `Bool?`
//! instance in 06_monad_laws.kofun.
//!
//! What to compare — the difference is not syntax, it is what the compiler
//! knows:
//!
//!   * Here the laws are three test functions. Nothing connects them to
//!     `bind` and `pure`: delete `left_identity` and the code still builds,
//!     ships, and claims nothing. In the Kofun design the equations are
//!     members of the `law` declaration, the `impl` names the family, and
//!     `check laws` produces evidence with a stated assurance level. The
//!     law is attached to the instance rather than sitting beside it.
//!   * The domain enumeration below is hand-written and its completeness is
//!     a comment. `all_functions(values, monads)` in the Kofun file is a
//!     declared domain the checker is responsible for.
//!   * Neither compiles today. Rust's version runs; the Kofun version is
//!     refused with `error[E2S02]: expected top-level `fn`, `type`, or `let``, which
//!     does not tell the reader that `law` is a known future form. Corpus
//!     item 6 is blocked-on #31.

/// The concrete monad: `Option<bool>`, matching the Kofun instance's
/// `Monad[Bool, Bool?]`.
fn pure(value: bool) -> Option<bool> {
    Some(value)
}

fn bind<F>(value: Option<bool>, next: F) -> Option<bool>
where
    F: Fn(bool) -> Option<bool>,
{
    match value {
        None => None,
        Some(inner) => next(inner),
    }
}

/// Every function `bool -> Option<bool>`. There are exactly nine: three
/// results (`None`, `Some(false)`, `Some(true)`) for each of two inputs.
/// This enumeration is hand-written, and its completeness is asserted by
/// `functions_domain_is_complete` below rather than left as a claim.
fn all_functions() -> Vec<Box<dyn Fn(bool) -> Option<bool>>> {
    let results = [None, Some(false), Some(true)];
    let mut functions: Vec<Box<dyn Fn(bool) -> Option<bool>>> = Vec::new();
    for on_false in results {
        for on_true in results {
            functions.push(Box::new(move |input| if input { on_true } else { on_false }));
        }
    }
    functions
}

/// Every value of `Option<bool>`.
fn all_monads() -> [Option<bool>; 3] {
    [None, Some(false), Some(true)]
}

/// Every value of `bool`.
fn all_values() -> [bool; 2] {
    [false, true]
}

fn main() {
    println!("run the laws with: rustc --test 06_monad_laws.rs");
}

#[cfg(test)]
mod laws {
    use super::*;

    #[test]
    fn functions_domain_is_complete() {
        assert_eq!(all_functions().len(), 9);
    }

    /// bind(pure(value), next) == next(value)
    #[test]
    fn left_identity() {
        for value in all_values() {
            for next in all_functions() {
                assert_eq!(bind(pure(value), &next), next(value));
            }
        }
    }

    /// bind(value, pure) == value
    #[test]
    fn right_identity() {
        for value in all_monads() {
            assert_eq!(bind(value, pure), value);
        }
    }

    /// bind(bind(value, first), second) == bind(value, |x| bind(first(x), second))
    #[test]
    fn associativity() {
        for value in all_monads() {
            for first in all_functions() {
                for second in all_functions() {
                    let left = bind(bind(value, &first), &second);
                    let right = bind(value, |inner| bind(first(inner), &second));
                    assert_eq!(left, right);
                }
            }
        }
    }
}
