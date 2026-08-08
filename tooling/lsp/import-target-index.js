'use strict';

// A selective import names one declaration target but may bind it under more
// than one local alias. Indexing that pair once keeps cross-document completion
// proportional to the imports plus declarations it inspects; nesting every
// declaration against every import made the documented 10k + 10k bounds
// quadratic.

function targetKey(modulePath, importedName) {
  // The length prefix makes the pair injective without reserving a separator
  // that a future module-path grammar might admit.
  return `${modulePath.length}:${modulePath}${importedName}`;
}

function buildSelectiveImportTargetIndex(imports, work = null) {
  const targets = new Map();
  for (const binding of imports) {
    if (work) work.indexedImports = (work.indexedImports || 0) + 1;
    const key = targetKey(binding.modulePath, binding.importedName);
    const aliases = targets.get(key);
    if (aliases) aliases.push(binding);
    else targets.set(key, [binding]);
  }
  return targets;
}

function forEachImportedDeclaration(
  targets,
  modulePath,
  declarations,
  visit,
  work = null
) {
  for (const declaration of declarations) {
    if (work) work.declarationProbes = (work.declarationProbes || 0) + 1;
    const aliases = targets.get(targetKey(modulePath, declaration.name));
    if (!aliases) continue;
    if (work) work.matchedBindings =
      (work.matchedBindings || 0) + aliases.length;
    visit(declaration, aliases);
  }
}

module.exports = {
  buildSelectiveImportTargetIndex,
  forEachImportedDeclaration
};
