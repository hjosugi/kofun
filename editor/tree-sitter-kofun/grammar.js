/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  ASSIGNMENT: 1,
  PIPELINE: 2,
  COALESCE: 3,
  OR: 4,
  AND: 5,
  EQUALITY: 6,
  COMPARISON: 7,
  RANGE: 8,
  ADDITIVE: 9,
  MULTIPLICATIVE: 10,
  POWER: 11,
  UNARY: 12,
  POSTFIX: 13,
};

module.exports = grammar({
  name: "kofun",

  externals: ($) => [
    $._newline,
    $._soft_newline,
    $._leading_pipeline,
    $._leading_else,
    $._left_paren,
    $._right_paren,
    $._left_bracket,
    $._right_bracket,
  ],

  extras: ($) => [/[ \t\f\v]/, $.comment, $._soft_newline],

  word: ($) => $.identifier,

  supertypes: ($) => [$.declaration, $.statement, $.expression],

  rules: {
    source_file: ($) => terminatedList($, $.declaration),

    _terminator: ($) => choice($._newline, ";"),

    declaration: ($) =>
      choice(
        $.import_declaration,
        $.law_declaration,
        $.function_declaration,
        $.meta_function_declaration,
        $.foreign_struct_declaration,
        $.extern_function_declaration,
        $.cli_declaration,
        $.statement,
      ),

    import_declaration: ($) =>
      choice(
        seq(
          optional(field("visibility", $.visibility_modifier)),
          "import",
          field("module", $.module_path),
          optional(seq("as", field("alias", $.identifier))),
        ),
        seq(
          optional(field("visibility", $.visibility_modifier)),
          "from",
          field("module", $.module_path),
          "import",
          commaSep1($.identifier),
        ),
      ),

    module_path: ($) =>
      seq(
        field("component", $.identifier),
        repeat(seq(".", field("component", $.identifier))),
      ),

    visibility_modifier: (_) => choice("pub", "internal", "private"),

    law_declaration: ($) =>
      seq(
        "law",
        "monad",
        field("name", $.identifier),
        "{",
        separatedBody($, $.law_entry),
        "}",
      ),

    law_entry: ($) =>
      seq(
        field("name", $.identifier),
        "=",
        field("value", $.expression),
      ),

    function_declaration: ($) =>
      seq(
        optional(field("visibility", $.visibility_modifier)),
        "fn",
        functionTail($),
      ),

    meta_function_declaration: ($) =>
      seq(
        optional(field("visibility", $.visibility_modifier)),
        "meta",
        "fn",
        functionTail($),
      ),

    parameter_list: ($) =>
      seq(
        $._left_paren,
        optional(commaSep1($.parameter)),
        optional(","),
        $._right_paren,
      ),

    parameter: ($) =>
      seq(
        optional(field("mode", $.ownership_mode)),
        field("name", $.identifier),
        optional(seq(":", field("type", $.type_reference))),
      ),

    ownership_mode: (_) => choice("read", "edit", "take"),

    return_type: ($) => seq("->", $.type_reference),

    type_reference: ($) =>
      seq(
        field("name", $.identifier),
        optional(
          seq(
            $._left_bracket,
            commaSep1($.type_reference),
            optional(","),
            $._right_bracket,
          ),
        ),
        optional("?"),
      ),

    block: ($) =>
      seq("{", terminatedList($, $.declaration_in_block), "}"),

    declaration_in_block: ($) =>
      choice(
        $.function_declaration,
        $.meta_function_declaration,
        $.statement,
      ),

    statement: ($) =>
      choice(
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

    let_statement: ($) =>
      seq(
        "let",
        optional("own"),
        optional("mut"),
        optional("own"),
        field("name", $.identifier),
        optional(seq(":", field("type", $.type_reference))),
        "=",
        field("value", $.expression),
      ),

    return_statement: ($) => seq("return", optional($.expression)),

    while_statement: ($) =>
      seq(
        "while",
        field("condition", $.expression),
        field("body", $.block),
      ),

    for_statement: ($) =>
      seq(
        "for",
        field("binding", $.identifier),
        "in",
        field("iterable", $.expression),
        field("body", $.block),
      ),

    take_statement: ($) => seq("take", field("value", $.identifier)),

    break_statement: (_) => "break",

    continue_statement: (_) => "continue",

    assignment_statement: ($) =>
      prec.right(
        PREC.ASSIGNMENT,
        seq(
          field("left", $.expression),
          "=",
          field("right", $.expression),
        ),
      ),

    expression_statement: ($) => $.expression,

    expression: ($) =>
      choice(
        $.identifier,
        $.integer_literal,
        $.float_literal,
        $.string_literal,
        $.boolean_literal,
        $.null_literal,
        $.list_expression,
        $.tuple_expression,
        $.parenthesized_expression,
        $.if_expression,
        $.lambda_expression,
        $.unary_expression,
        $.binary_expression,
        $.call_expression,
        $.member_expression,
        $.index_expression,
      ),

    unary_expression: ($) =>
      prec(
        PREC.UNARY,
        seq(
          field("operator", choice("+", "-", "!")),
          field("operand", $.expression),
        ),
      ),

    binary_expression: ($) =>
      choice(
        binaryLeft(
          PREC.PIPELINE,
          $,
          choice("|>", alias($._leading_pipeline, "|>")),
        ),
        binaryLeft(PREC.COALESCE, $, "??"),
        binaryLeft(PREC.OR, $, "||"),
        binaryLeft(PREC.AND, $, "&&"),
        binaryLeft(PREC.EQUALITY, $, choice("==", "!=")),
        binaryLeft(PREC.COMPARISON, $, choice("<", "<=", ">", ">=")),
        binaryLeft(PREC.RANGE, $, ".."),
        binaryLeft(PREC.ADDITIVE, $, choice("+", "-")),
        binaryLeft(PREC.MULTIPLICATIVE, $, choice("*", "/", "//", "%")),
        prec.right(
          PREC.POWER,
          seq(
            field("left", $.expression),
            field("operator", "**"),
            field("right", $.expression),
          ),
        ),
      ),

    call_expression: ($) =>
      prec(
        PREC.POSTFIX,
        seq(
          field("function", $.expression),
          field("arguments", $.argument_list),
        ),
      ),

    argument_list: ($) =>
      seq(
        $._left_paren,
        optional(commaSep1($.expression)),
        optional(","),
        $._right_paren,
      ),

    member_expression: ($) =>
      prec(
        PREC.POSTFIX,
        seq(
          field("object", $.expression),
          ".",
          field("property", $.identifier),
        ),
      ),

    index_expression: ($) =>
      prec(
        PREC.POSTFIX,
        seq(
          field("value", $.expression),
          $._left_bracket,
          field("index", $.expression),
          $._right_bracket,
        ),
      ),

    list_expression: ($) =>
      seq(
        $._left_bracket,
        optional(commaSep1($.expression)),
        optional(","),
        $._right_bracket,
      ),

    tuple_expression: ($) =>
      seq(
        $._left_paren,
        field("element", $.expression),
        ",",
        optional(commaSep1(field("element", $.expression))),
        optional(","),
        $._right_paren,
      ),

    parenthesized_expression: ($) =>
      seq($._left_paren, $.expression, $._right_paren),

    if_expression: ($) =>
      prec.right(
        seq(
          "if",
          field("condition", $.expression),
          field("consequence", $.block),
          optional(
            seq(
              choice("else", alias($._leading_else, "else")),
              field("alternative", choice($.if_expression, $.block)),
            ),
          ),
        ),
      ),

    lambda_expression: ($) =>
      seq(
        "fn",
        field("parameters", $.parameter_list),
        choice(
          seq("=>", field("body", $.expression)),
          field("body", $.block),
        ),
      ),

    boolean_literal: (_) => choice("true", "false"),

    null_literal: (_) => "null",

    integer_literal: (_) =>
      token(
        prec(
          1,
          choice(
            /0[xX][0-9A-Fa-f](?:_?[0-9A-Fa-f])*/,
            /0[bB][01](?:_?[01])*/,
            /[0-9](?:_?[0-9])*/,
          ),
        ),
      ),

    float_literal: (_) =>
      token(
        prec(
          2,
          choice(
            /[0-9](?:_?[0-9])*\.[0-9](?:_?[0-9])*(?:[eE][+-]?[0-9](?:_?[0-9])*)?/,
            /[0-9](?:_?[0-9])*[eE][+-]?[0-9](?:_?[0-9])*/,
          ),
        ),
      ),

    string_literal: (_) =>
      token(
        seq(
          '"',
          repeat(
            choice(
              /[^"\\\r\n]/,
              /\\(?:[nrt"\\]|x[0-9A-Fa-f]{2}|u\{[0-9A-Fa-f]{1,6}\})/,
            ),
          ),
          '"',
        ),
      ),

    identifier: (_) =>
      token(new RustRegex("[_\\p{XID_Start}][_\\p{XID_Continue}]*")),

    comment: (_) => token(seq("#", /[^\r\n]*/)),

    foreign_struct_declaration: ($) =>
      seq(
        optional(field("visibility", $.visibility_modifier)),
        "repr",
        $._left_paren,
        field("representation", $.identifier),
        $._right_paren,
        "struct",
        field("name", $.identifier),
        "{",
        separatedBody($, $.field_declaration),
        "}",
      ),

    field_declaration: ($) =>
      seq(
        field("name", $.identifier),
        ":",
        field("type", $.type_reference),
      ),

    extern_function_declaration: ($) =>
      seq(
        optional(field("visibility", $.visibility_modifier)),
        "extern",
        field("abi", $.string_literal),
        "fn",
        field("name", $.identifier),
        field("parameters", $.parameter_list),
        optional(field("return_type", $.return_type)),
      ),

    cli_declaration: ($) =>
      seq(
        "cli",
        field("name", $.identifier),
        "{",
        terminatedList($, $.cli_member),
        "}",
      ),

    cli_member: ($) => choice($.cli_property, $.cli_command),

    cli_property: ($) =>
      choice(
        seq(field("key", "name"), $.string_literal),
        seq(field("key", "version"), $.string_literal),
        seq(field("key", "about"), $.string_literal),
      ),

    cli_command: ($) =>
      seq(
        "command",
        field("name", $.identifier),
        "{",
        terminatedList($, $.cli_command_member),
        "}",
      ),

    cli_command_member: ($) =>
      choice(
        seq("about", field("description", $.string_literal)),
        seq(
          "position",
          field("name", $.identifier),
          field("description", $.string_literal),
        ),
        seq(
          "option",
          field("name", $.identifier),
          field("long_name", $.string_literal),
          field("kind", choice("bool", "text")),
          field("description", $.string_literal),
          optional(seq("default", field("default", $.string_literal))),
        ),
        seq("action", field("name", $.identifier)),
      ),
  },
});

function functionTail($) {
  return seq(
    field("name", $.identifier),
    field("parameters", $.parameter_list),
    optional(field("return_type", $.return_type)),
    choice(
      seq("=", field("body", $.expression)),
      field("body", $.block),
    ),
  );
}

function binaryLeft(precedence, $, operator) {
  return prec.left(
    precedence,
    seq(
      field("left", $.expression),
      field("operator", operator),
      field("right", $.expression),
    ),
  );
}

function commaSep1(rule) {
  return seq(rule, repeat(seq(",", rule)));
}

function terminatedList($, rule) {
  return seq(
    repeat($._terminator),
    repeat(seq(rule, repeat1($._terminator))),
    optional(rule),
  );
}

function separatedBody($, rule) {
  return seq(
    repeat($._terminator),
    repeat(
      seq(
        rule,
        choice(
          seq(",", repeat($._terminator)),
          repeat1($._terminator),
        ),
      ),
    ),
    optional(rule),
  );
}
