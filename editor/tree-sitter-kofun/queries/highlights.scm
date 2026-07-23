(comment) @comment

(string_literal) @string
(integer_literal) @number
(float_literal) @number.float
(boolean_literal) @boolean
(null_literal) @constant.builtin

[
  "fn"
  "meta"
  "law"
  "monad"
  "let"
  "mut"
  "own"
  "read"
  "edit"
  "take"
  "return"
  "if"
  "else"
  "for"
  "in"
  "while"
  "import"
  "from"
  "as"
  "pub"
  "internal"
  "private"
  "extern"
  "repr"
  "struct"
  "cli"
  "command"
  "position"
  "option"
  "action"
  "default"
] @keyword

[
  "+"
  "-"
  "*"
  "/"
  "//"
  "%"
  "**"
  "=="
  "!="
  "<"
  "<="
  ">"
  ">="
  "&&"
  "||"
  "??"
  "|>"
  ".."
  "="
  "!"
  "->"
  "=>"
] @operator

[
  "{"
  "}"
] @punctuation.bracket

[
  ","
  "."
  ":"
  ";"
] @punctuation.delimiter

(function_declaration name: (identifier) @function)
(meta_function_declaration name: (identifier) @function.macro)
(extern_function_declaration name: (identifier) @function)
(lambda_expression "fn" @keyword.function)
(call_expression function: (identifier) @function.call)

(parameter name: (identifier) @variable.parameter)
(type_reference name: (identifier) @type)
(foreign_struct_declaration name: (identifier) @type)
(field_declaration name: (identifier) @property)
(member_expression property: (identifier) @property)

(law_declaration name: (identifier) @type)
(law_entry name: (identifier) @property)

(module_path (identifier) @module)

(cli_declaration name: (identifier) @type)
(cli_command name: (identifier) @function)
(cli_property
  key: [
    "name"
    "version"
    "about"
  ] @property)

[
  (break_statement)
  (continue_statement)
] @keyword
