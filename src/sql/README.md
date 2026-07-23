# Embedded FTS SQL

The SQL files in this directory are the source of truth for generated FTS
schemas and macros. They are grouped by initial index construction, query
macros, and incremental maintenance. `CMakeLists.txt` lists every asset
explicitly; adding a file here also requires adding it to `FTS_SQL_ASSETS`.

CMake embeds the assets into the extension binary during configuration. SQL
uses `{{name}}` placeholders rendered by `RenderSQLTemplate`. Bind identifiers,
qualified names, string literals, and integers with their corresponding
`SQLTemplateArgument` constructors. `TrustedSQL` is reserved for
extension-controlled fragments and the output of another rendered template;
never pass user input to it.

The renderer is intentionally one-pass and does not implement conditionals,
loops, or includes. Keep feature selection and repeated-clause construction in
the owning C++ builder module.
