; Comments and literals
(comment) @comment
(string) @string
(integer) @number
(float) @number.float
[
  (true)
  (false)
] @boolean
(null) @constant.builtin
(break_statement) @keyword
(continue_statement) @keyword

; Declarations
(function_declaration
  name: (identifier) @function)
(meta_function_declaration
  name: (identifier) @function.macro)
(extern_function_declaration
  name: (identifier) @function)
(law_declaration
  name: (identifier) @type)
(repr_struct_declaration
  name: (identifier) @type)
(command_declaration
  name: (identifier) @function)

; Types and bindings
(type
  name: (identifier) @type)
(parameter
  name: (identifier) @variable.parameter)
(struct_field
  name: (identifier) @property)
(law_entry
  name: (identifier) @property)
(member_expression
  property: (identifier) @property)

; Calls are represented as postfix parts, so the primary identifier is the
; callable when a call immediately follows it.
(postfix_expression
  (identifier) @function.call
  (call_expression))

; Module names
(module_path
  (identifier) @module)

; Language words
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
  "repr"
  "struct"
  "extern"
  "cli"
  "command"
  "position"
  "option"
  "action"
  "default"
] @keyword

[
  "name"
  "version"
  "about"
] @keyword.directive

[
  "="
  "=>"
  "->"
  "|>"
  "??"
  "||"
  "&&"
  "=="
  "!="
  "<"
  "<="
  ">"
  ">="
  ".."
  "+"
  "-"
  "*"
  "/"
  "//"
  "%"
  "**"
  "!"
] @operator

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  ","
  "."
  ":"
  ";"
  "?"
] @punctuation.delimiter
