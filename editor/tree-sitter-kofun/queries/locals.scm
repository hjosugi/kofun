(source_file) @local.scope
(block) @local.scope
(lambda_expression) @local.scope

(function_declaration
  name: (identifier) @local.definition)
(meta_function_declaration
  name: (identifier) @local.definition)
(parameter
  name: (identifier) @local.definition)
(let_statement
  name: (identifier) @local.definition)
(for_statement
  name: (identifier) @local.definition)

(identifier) @local.reference
