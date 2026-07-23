(function_declaration) @local.scope
(meta_function_declaration) @local.scope
(lambda_expression) @local.scope
(for_statement) @local.scope

(function_declaration name: (identifier) @local.definition)
(meta_function_declaration name: (identifier) @local.definition)
(extern_function_declaration name: (identifier) @local.definition)
(parameter name: (identifier) @local.definition)
(let_statement name: (identifier) @local.definition)
(for_statement binding: (identifier) @local.definition)

(type_reference (identifier) @ignore)
(module_path (identifier) @ignore)
(member_expression property: (identifier) @ignore)
(field_declaration name: (identifier) @ignore)
(law_entry name: (identifier) @ignore)

(identifier) @local.reference
