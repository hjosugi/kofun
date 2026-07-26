/**
 * @file Tree-sitter grammar for the Kofun programming language.
 * @author Kofun contributors
 * @license MIT OR Apache-2.0
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  ASSIGNMENT: 1,
  PIPELINE: 2,
  COALESCE: 3,
  LOGICAL_OR: 4,
  LOGICAL_AND: 5,
  EQUALITY: 6,
  COMPARISON: 7,
  RANGE: 8,
  ADDITIVE: 9,
  MULTIPLICATIVE: 10,
  POWER: 11,
  UNARY: 12,
  POSTFIX: 13,
};

export default grammar({
  name: "kofun",

  word: ($) => $.identifier,

  extras: ($) => [
    /[ \t\f\uFEFF\u2060\u200B]/,
    $.comment,
  ],

  // `(` opens a parenthesised lambda parameter list, a tuple, or a grouping,
  // and nothing decides between them until the `)` is followed (or not) by
  // `=>`. The GLR parser carries both readings until then.
  conflicts: ($) => [
    [$.parameter, $._primary_expression],
  ],

  supertypes: ($) => [
    $.declaration,
    $.statement,
    $.expression,
    $._primary_expression,
  ],

  rules: {
    source_file: ($) => separatedBody($, $.declaration),

    declaration: ($) => choice(
      $.law_declaration,
      $.function_declaration,
      $.meta_function_declaration,
      $.import_declaration,
      $.from_import_declaration,
      $.cli_declaration,
      $.repr_struct_declaration,
      $.extern_function_declaration,
      $.statement,
    ),

    law_declaration: ($) => seq(
      "law",
      "monad",
      field("name", $.identifier),
      repeat($._separator),
      field("body", $.law_body),
    ),

    law_body: ($) => seq(
      "{",
      repeat($._separator),
      optional(seq(
        $.law_entry,
        repeat(seq($._law_entry_separator, $.law_entry)),
        optional(","),
        repeat($._separator),
      )),
      "}",
    ),

    _law_entry_separator: ($) => choice(
      seq(",", repeat($._separator)),
      seq(repeat1($._separator), optional(seq(",", repeat($._separator)))),
    ),

    law_entry: ($) => seq(
      field("name", $.identifier),
      "=",
      repeat($._newline),
      field("value", $.expression),
    ),

    meta_function_declaration: ($) => seq(
      "meta",
      "fn",
      $._function_tail,
    ),

    function_declaration: ($) => seq(
      "fn",
      $._function_tail,
    ),

    _function_tail: ($) => seq(
      field("name", $.identifier),
      field("parameters", $.parameters),
      optional(seq(
        "->",
        repeat($._newline),
        field("return_type", $.type),
      )),
      choice(
        seq(
          repeat($._newline),
          "=",
          repeat($._newline),
          field("body", $.expression),
        ),
        seq(
          repeat($._newline),
          field("body", $.block),
        ),
      ),
    ),

    parameters: ($) => seq(
      "(",
      repeat($._separator),
      optional(seq(
        $.parameter,
        repeat(seq($._comma, $.parameter)),
        optional(","),
        repeat($._separator),
      )),
      ")",
    ),

    parameter: ($) => seq(
      optional(field("ownership", choice("read", "edit", "take"))),
      field("name", $.identifier),
      optional(seq(
        ":",
        repeat($._newline),
        field("type", $.type),
      )),
    ),

    // #552 settled one notation for callable types, used in both positions:
    // `A -> R` for one argument, `(A, B) -> R` for a fixed arity, `() -> R`
    // for none. `->` has the lowest precedence in a type and associates to
    // the right, so `A -> B -> R` is `A -> (B -> R)` — a callable returning a
    // callable, not a two-argument callable. There is no implicit currying,
    // so those are different types.
    type: ($) => prec.right(seq(
      choice(
        seq(
          field("name", $.identifier),
          optional(field("type_arguments", $.type_arguments)),
          optional("?"),
        ),
        seq(
          field("domain", $.callable_domain),
          optional("?"),
        ),
      ),
      // No newline may precede the `->`. Newlines are significant here, and
      // letting a type consume them while looking for an arrow makes a
      // body-less declaration such as `extern "C" fn add(...) -> CInt`
      // swallow its terminator and fail at end of input.
      optional(seq(
        "->",
        repeat($._newline),
        field("result", $.type),
      )),
    )),

    // A parenthesised domain, which is also how a parenthesised type is
    // written: `(A -> B)?` is an optional callable, while `A -> B?` is a
    // callable returning an optional.
    callable_domain: ($) => seq(
      "(",
      repeat($._separator),
      optional(seq(
        $.type,
        repeat(seq($._comma, $.type)),
        optional(","),
        repeat($._separator),
      )),
      ")",
    ),

    type_arguments: ($) => seq(
      "[",
      repeat($._separator),
      $.type,
      repeat(seq($._comma, $.type)),
      optional(","),
      repeat($._separator),
      "]",
    ),

    block: ($) => seq(
      "{",
      separatedBody($, $.declaration_in_block),
      "}",
    ),

    declaration_in_block: ($) => choice(
      $.function_declaration,
      $.meta_function_declaration,
      $.statement,
    ),

    statement: ($) => choice(
      $.let_statement,
      $.return_statement,
      $.while_statement,
      $.for_statement,
      $.take_statement,
      $.break_statement,
      $.continue_statement,
      $.assignment_statement,
      $.expression_statement,
    ),

    let_statement: ($) => seq(
      "let",
      optional("own"),
      optional("mut"),
      optional("own"),
      field("name", $.identifier),
      optional(seq(
        ":",
        repeat($._newline),
        field("type", $.type),
      )),
      repeat($._newline),
      "=",
      repeat($._newline),
      field("value", $.expression),
    ),

    return_statement: ($) => seq(
      "return",
      optional(field("value", $.expression)),
    ),

    while_statement: ($) => seq(
      "while",
      field("condition", $.expression),
      repeat($._newline),
      field("body", $.block),
    ),

    for_statement: ($) => seq(
      "for",
      field("name", $.identifier),
      "in",
      field("value", $.expression),
      repeat($._newline),
      field("body", $.block),
    ),

    take_statement: ($) => seq(
      "take",
      field("name", $.identifier),
    ),

    break_statement: (_) => "break",

    continue_statement: (_) => "continue",

    assignment_statement: ($) => prec.right(PREC.ASSIGNMENT, seq(
      field("left", $.expression),
      "=",
      repeat($._newline),
      field("right", $.expression),
    )),

    expression_statement: ($) => $.expression,

    // A lambda sits at the top of the expression tier rather than among the
    // primaries, so its body extends to the end of the expression: `x => x * 2`
    // is `x => (x * 2)`, not `(x => x) * 2`. Placing it among the primaries
    // makes the two readings ambiguous, and the tie resolves to the wrong one.
    expression: ($) => choice($.pipeline_expression, $.lambda_expression),

    pipeline_expression: ($) => binaryExpression(
      $,
      PREC.PIPELINE,
      $.coalesce_expression,
      choice(
        "|>",
        alias($._leading_pipeline_operator, "|>"),
      ),
    ),

    coalesce_expression: ($) => binaryExpression(
      $,
      PREC.COALESCE,
      $.logical_or_expression,
      "??",
    ),

    logical_or_expression: ($) => binaryExpression(
      $,
      PREC.LOGICAL_OR,
      $.logical_and_expression,
      "||",
    ),

    logical_and_expression: ($) => binaryExpression(
      $,
      PREC.LOGICAL_AND,
      $.equality_expression,
      "&&",
    ),

    equality_expression: ($) => binaryExpression(
      $,
      PREC.EQUALITY,
      $.comparison_expression,
      choice("==", "!="),
    ),

    comparison_expression: ($) => binaryExpression(
      $,
      PREC.COMPARISON,
      $.range_expression,
      choice("<", "<=", ">", ">="),
    ),

    range_expression: ($) => prec.left(PREC.RANGE, seq(
      $.additive_expression,
      optional(seq(
        field("operator", ".."),
        repeat($._newline),
        $.additive_expression,
      )),
    )),

    additive_expression: ($) => binaryExpression(
      $,
      PREC.ADDITIVE,
      $.multiplicative_expression,
      choice("+", "-"),
    ),

    multiplicative_expression: ($) => binaryExpression(
      $,
      PREC.MULTIPLICATIVE,
      $.power_expression,
      choice("*", "/", "//", "%"),
    ),

    power_expression: ($) => prec.right(PREC.POWER, seq(
      $.unary_expression,
      optional(seq(
        field("operator", "**"),
        repeat($._newline),
        $.power_expression,
      )),
    )),

    unary_expression: ($) => choice(
      $.postfix_expression,
      prec(PREC.UNARY, seq(
        field("operator", choice("+", "-", "!")),
        repeat($._newline),
        $.postfix_expression,
      )),
    ),

    postfix_expression: ($) => prec.left(PREC.POSTFIX, seq(
      $._primary_expression,
      repeat(choice(
        $.call_expression,
        $.index_expression,
        $.member_expression,
      )),
    )),

    call_expression: ($) => seq(
      field("arguments", $.arguments),
    ),

    arguments: ($) => seq(
      "(",
      repeat($._separator),
      optional(seq(
        $.expression,
        repeat(seq($._comma, $.expression)),
        optional(","),
        repeat($._separator),
      )),
      ")",
    ),

    member_expression: ($) => seq(
      ".",
      repeat($._newline),
      field("property", $.identifier),
    ),

    index_expression: ($) => seq(
      "[",
      repeat($._separator),
      field("index", $.expression),
      repeat($._separator),
      "]",
    ),

    _primary_expression: ($) => choice(
      $.integer,
      $.float,
      $.string,
      $.true,
      $.false,
      $.null,
      $.identifier,
      $.list_expression,
      $.tuple_expression,
      $.parenthesized_expression,
      $.if_expression,
    ),

    list_expression: ($) => seq(
      "[",
      repeat($._separator),
      optional(seq(
        $.expression,
        repeat(seq($._comma, $.expression)),
        optional(","),
        repeat($._separator),
      )),
      "]",
    ),

    tuple_expression: ($) => seq(
      "(",
      repeat($._separator),
      $.expression,
      repeat($._separator),
      ",",
      repeat($._separator),
      optional(seq(
        $.expression,
        repeat(seq($._comma, $.expression)),
        optional(","),
        repeat($._separator),
      )),
      ")",
    ),

    parenthesized_expression: ($) => seq(
      "(",
      repeat($._separator),
      $.expression,
      repeat($._separator),
      ")",
    ),

    if_expression: ($) => prec.right(seq(
      "if",
      field("condition", $.expression),
      repeat($._newline),
      field("consequence", $.block),
      optional(seq(
        "else",
        repeat($._separator),
        field("alternative", choice($.if_expression, $.block)),
      )),
    )),

    // Three surface forms share one node:
    //
    //   fn(x: Int) => x        the original explicit form
    //   (x, y) => x + y        a parenthesised parameter list
    //   x => x * 2             a single unannotated parameter
    //
    // The parenthesised forms carry a `parameters` field; the bare form carries
    // a `parameter` field holding the identifier itself, because there is no
    // parenthesised list to report.
    //
    // Annotating requires parentheses: `x: Int => x` is not a lambda, because
    // `:` there is the type-annotation position. The bare form admits a lone
    // identifier only, so that shape cannot arise.
    lambda_expression: ($) => prec.right(choice(
      seq(
        "fn",
        field("parameters", $.parameters),
        repeat($._newline),
        choice(
          seq(
            "=>",
            repeat($._newline),
            field("body", $.expression),
          ),
          field("body", $.block),
        ),
      ),
      seq(
        field("parameters", $.parameters),
        "=>",
        repeat($._newline),
        field("body", choice($.expression, $.block)),
      ),
      seq(
        field("parameter", $.identifier),
        "=>",
        repeat($._newline),
        field("body", choice($.expression, $.block)),
      ),
    )),

    true: (_) => "true",
    false: (_) => "false",
    null: (_) => "null",

    import_declaration: ($) => seq(
      optional("pub"),
      "import",
      field("path", $.module_path),
      optional(seq("as", field("alias", $.identifier))),
    ),

    from_import_declaration: ($) => seq(
      optional("pub"),
      "from",
      field("path", $.module_path),
      "import",
      field("names", $.import_list),
    ),

    module_path: ($) => seq(
      $.identifier,
      repeat(seq(".", $.identifier)),
    ),

    import_list: ($) => seq(
      $.identifier,
      repeat(seq(",", repeat($._newline), $.identifier)),
    ),

    repr_struct_declaration: ($) => seq(
      "repr",
      "(",
      "C",
      ")",
      "struct",
      field("name", $.identifier),
      field("body", $.struct_body),
    ),

    struct_body: ($) => seq(
      "{",
      repeat($._separator),
      optional(seq(
        $.struct_field,
        repeat(seq($._struct_field_separator, $.struct_field)),
        optional(","),
        repeat($._separator),
      )),
      "}",
    ),

    _struct_field_separator: ($) => choice(
      seq(",", repeat($._separator)),
      seq(repeat1($._separator), optional(seq(",", repeat($._separator)))),
    ),

    struct_field: ($) => seq(
      field("name", $.identifier),
      ":",
      repeat($._newline),
      field("type", $.type),
    ),

    extern_function_declaration: ($) => prec.right(seq(
      "extern",
      field("abi", $.string),
      "fn",
      field("name", $.identifier),
      field("parameters", $.parameters),
      optional(seq(
        "->",
        repeat($._newline),
        field("return_type", $.type),
      )),
    )),

    cli_declaration: ($) => seq(
      "cli",
      field("name", $.identifier),
      field("body", $.cli_body),
    ),

    cli_body: ($) => seq(
      "{",
      separatedBody($, $._cli_item),
      "}",
    ),

    _cli_item: ($) => choice(
      $.cli_name_directive,
      $.cli_version_directive,
      $.cli_about_directive,
      $.command_declaration,
    ),

    cli_name_directive: ($) => seq("name", field("value", $.string)),
    cli_version_directive: ($) => seq("version", field("value", $.string)),
    cli_about_directive: ($) => seq("about", field("value", $.string)),

    command_declaration: ($) => seq(
      "command",
      field("name", $.identifier),
      field("body", $.command_body),
    ),

    command_body: ($) => seq(
      "{",
      separatedBody($, $._command_item),
      "}",
    ),

    _command_item: ($) => choice(
      $.command_about_directive,
      $.position_directive,
      $.option_directive,
      $.action_directive,
    ),

    command_about_directive: ($) => seq("about", field("value", $.string)),

    position_directive: ($) => seq(
      "position",
      field("name", $.identifier),
      field("description", $.string),
    ),

    option_directive: ($) => seq(
      "option",
      field("name", $.identifier),
      field("flag", $.string),
      field("kind", $.identifier),
      field("description", $.string),
      optional(seq("default", field("default", $.string))),
    ),

    action_directive: ($) => seq(
      "action",
      field("function", $.identifier),
    ),

    identifier: (_) => /[_A-Za-z][_A-Za-z0-9]*/,

    integer: (_) => token(prec(1, /[0-9](?:_?[0-9])*/)),

    float: (_) => token(prec(2, choice(
      /[0-9](?:_?[0-9])*\.[0-9](?:_?[0-9])*(?:[eE][+-]?[0-9](?:_?[0-9])*)?/,
      /[0-9](?:_?[0-9])*[eE][+-]?[0-9](?:_?[0-9])*/,
    ))),

    string: (_) => token(seq(
      '"',
      repeat(choice(
        /[^"\\\r\n]/,
        /\\[^\r\n]/,
      )),
      '"',
    )),

    comment: (_) => token(seq("#", /[^\r\n]*/)),

    _comma: ($) => seq(
      ",",
      repeat($._separator),
    ),

    _leading_pipeline_operator: (_) => token(prec(
      2,
      /\r?\n[ \t]*\|>/,
    )),

    _separator: ($) => choice($._newline, ";"),
    _newline: (_) => /\r?\n/,
  },
});

/**
 * Parse a body whose entries require at least one newline or semicolon between
 * them, while permitting leading and trailing separators.
 *
 * @param {GrammarSymbols<any>} $ grammar symbols
 * @param {RuleOrLiteral} item body item
 * @returns {SeqRule}
 */
function separatedBody($, item) {
  return seq(
    repeat(choice(
      $._separator,
      seq(item, $._separator),
    )),
    optional(item),
  );
}

/**
 * Build one left-associative binary precedence layer. Newlines adjacent to an
 * operator are continuation trivia; all other newlines remain statement
 * separators.
 *
 * @param {GrammarSymbols<any>} $ grammar symbols
 * @param {number} precedence precedence value
 * @param {RuleOrLiteral} operand next tighter expression layer
 * @param {RuleOrLiteral} operator operator token or choice
 * @returns {PrecLeftRule}
 */
function binaryExpression($, precedence, operand, operator) {
  return prec.left(precedence, seq(
    operand,
    repeat(seq(
      field("operator", operator),
      repeat($._newline),
      operand,
    )),
  ));
}
