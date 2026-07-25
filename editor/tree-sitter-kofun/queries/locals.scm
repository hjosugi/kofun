(source_file) @local.scope
(block) @local.scope
(lambda_expression) @local.scope

(function_declaration
  name: (identifier) @local.definition)
(meta_function_declaration
  name: (identifier) @local.definition)
(parameter
  name: (identifier) @local.definition)
; `x => x * 2` has no parenthesised list, so the identifier is the parameter.
(lambda_expression
  parameter: (identifier) @local.definition)
(let_statement
  name: (identifier) @local.definition)
(for_statement
  name: (identifier) @local.definition)

(identifier) @local.reference
