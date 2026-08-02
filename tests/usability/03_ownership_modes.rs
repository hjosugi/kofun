//! Comparison for corpus item 3: a resource function showing read / edit /
//! take, with no use-after-move.
//!
//! Language: Rust. Chosen because it is the only one of the four comparison
//! languages #624 names that has ownership at all — Go, Gleam, and Kotlin
//! are all garbage-collected, so `take` has no counterpart in them and the
//! comparison would be vacuous.
//!
//! Compiled and run by the gate: `rustc -O 03_ownership_modes.rs && ./binary`
//! prints `14`, matching `call|function=lifecycle|result=14` from the Kofun
//! file's record-frontend run artifact.
//!
//! What to compare:
//!
//!   * All three modes exist here. Kofun has two: `edit` on a record is
//!     refused by DD-021 (E2S121), so corpus item 3 cannot show the middle
//!     row at all.
//!   * Rust spells the modes in the *type* (`&T`, `&mut T`, `T`); Kofun
//!     spells them as a *parameter mode keyword* (`read`, `edit`, `take`).
//!     The Kofun spelling is the more readable of the two and #624's
//!     ownership goal is stated in exactly those words — but it is
//!     understood by one bounded frontend and silently reparsed as an
//!     identifier by the production path. Rust's is understood everywhere.
//!   * Rust needs no `take session` statement inside the body: passing by
//!     value *is* the move. Kofun's `take` parameter mode does not by itself
//!     consume the binding; `release` needs the mode on the parameter and a
//!     `take session` statement in the body.
//!   * The use-after-move is rejected at compile time in both languages.
//!     Rust's message names the move site; Kofun's E2S123 names only the
//!     use, which is what open issue #922 is for.

struct Session {
    handle: u32,
    label: String,
}

/// `read`: a borrowed view. The caller keeps the session.
fn handle_of(session: &Session) -> u32 {
    session.handle
}

/// `edit`: a unique borrow. The caller keeps the session, and the callee may
/// change it. This is the row Kofun v1 does not have.
fn relabel(session: &mut Session, label: &str) {
    session.label = label.to_string();
}

/// `take`: a move. The session is consumed; the caller cannot use it again.
fn release(session: Session) -> String {
    session.label
}

fn lifecycle() -> u32 {
    let mut session = Session {
        handle: 7,
        label: String::from("primary"),
    };

    // read, twice. Neither call consumes the binding.
    let first = handle_of(&session);
    let second = handle_of(&session);

    // edit. Still not consumed.
    relabel(&mut session, "secondary");

    // take. Consumed here, and deliberately used no further.
    let final_label = release(session);
    assert_eq!(final_label, "secondary");

    // Uncommenting the next line is the use-after-move, and it does not
    // compile:
    //
    //   error[E0382]: borrow of moved value: `session`
    //     --> 03_ownership_modes.rs
    //      |
    //      |     let final_label = release(session);
    //      |                               ------- value moved here
    //      |     handle_of(&session);
    //      |               ^^^^^^^^ value borrowed here after move
    //
    // handle_of(&session);

    first + second
}

fn main() {
    println!("{}", lifecycle());
}
