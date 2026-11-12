# Coding conventions for powwow

These rules apply to new code, and to old code that a change rewrites anyway.
Much of the older code predates them; do not restyle code that a change does
not otherwise touch.

## Responsibility

Code written with the help of AI agents or other tools is held to the same
standard as any other code. A human must review every such change before it is
committed, understand it, and take responsibility for it as its author.

## Language and platforms

- **C11:** the code must compile with a C11-compliant compiler. Do not
  depend on features from later versions.
- **Platforms:** the supported platforms are listed under COMPILING in
  `README`; the code must work on all of them. Use POSIX interfaces, and keep
  system-specific code behind the checks of `configure`.
- **Encoding:** source files are UTF-8. Non-ASCII characters may appear in
  comments and character literals, such as `U'ß'`; identifiers are ASCII.

## Testing

- **Build:** the code must build without compiler warnings.
- **Tests:** run `make check` before committing; all the tests must pass.
  `make check TESTS=test_commands.sh` runs one of them, and the results are in
  `src/test-suite.log` and `src/<test>.log`.
- **Static checks:** `make tidy` (needs clang-tidy) must report nothing; so
  must `make lint` (needs mandoc) when the man page changes.
  `make -C src foo.tidy` checks only `foo.c`, and `.clang-tidy` lists the
  checks.
- **New tests:**
  - Add a test with every bug fix that fails without the fix, and tests for
    new features.
  - Test commands, key bindings and what powwow prints in
    `src/test_commands.sh`, which runs powwow on the commands and keys of each
    test (`run`) and checks the lines it prints (`has`, `lacks`).
  - Test self-contained modules, such as `ptr.c` or `charset.c`, with a
    program `src/test_<module>.c`, listed in `check_PROGRAMS` and `TESTS` in
    `src/Makefile.am`.
- **Documentation:** update the documentation that a change affects:
  `doc/powwow.md.in`, `doc/powwow.help`, `man/powwow.6.in` and `README`.
  `test_commands.sh` checks that every command is described in
  `doc/powwow.md.in`.

## Commits and the ChangeLog

- **One change per commit:** keep each commit to one logical change, and
  keep bug fixes and changes of behavior apart from restyling.
- **Commit messages:** the first line is a short, self-contained description of
  the change. The body gives the important facts about the commit that cannot
  be inferred from the first line. No line is wider than 75 columns.
- **ChangeLog:** start each entry with a word that deserves to be highlighted,
  never with "A", "The" or similar.
- **Release tags:** annotated, named `vX.Y.Z`, on the commit that bumps the
  version. The message is plain text: `powwow X.Y.Z`, a blank line, and the
  release's ChangeLog section without its indentation, in lines of at most
  72 columns. Create them with `--cleanup=whitespace`, as the default
  cleanup drops lines that start with `#`.

## User interface and documentation

- **Key bindings:** default key bindings follow readline, as in bash and
  other shells.
- **The name:** write "powwow" in lowercase, also in documentation and
  messages, except "Powwow" at the start of a sentence. The program and file
  names are always lowercase.
- **Messages:** what powwow prints has no tabs; align columns with spaces.
  Wrapping measures a tab as 0 columns, where the terminal moves up to 8.

## Layout

- **Indentation:** indent each level by 4 columns.
- **Tabs:** do not add tabs, for indentation or anywhere else; use spaces,
  also in older files that indent with tabs, each 8 columns wide.
- **Line width:** new code keeps lines below 80 columns, counting a tab as 8
  columns. Break long lines before an operator, not after it. Split a long
  string literal into adjacent literals after a `\n` if it has one, or else
  before a space, so that the next literal starts with the spaces:
  `"no such"` then `" variable"`.
- **Braces:** put the opening brace at the end of the `if`, `else`, `for`,
  `while`, `do` or `switch` line, as in `} else {`. The opening brace of a
  function definition goes on its own line.
  - Code blocks always have a line break after the opening brace and before
    the closing one: never `if (x) { y(); }`.
- **Labels:** indent labels by at least 1 column.
- **Operators:**
  - Put spaces around binary and assignment operators: `a = b + 1`.
  - Wrap an expression that spans several lines in parentheses; the
    parentheses of an `if`, `while` or `switch` condition count.
  - When a `?:` expression is split across lines, wrap it in its own
    parentheses, and put `?` and `:` at the start of their lines, aligned
    with the column after the opening parenthesis.
  - Do not put a space between a keyword operator and its parenthesis:
    `sizeof(int)`.

## Naming

- Functions and variables: `snake_case`.
- Constants and macros: `UPPER_CASE`.

## Comments

- Comments describe the code as it is, not its history or how it came to be.
- Do not add comments that describe what can trivially be inferred
  from the code.

## Modules

- **Headers:** declare everything that a `.c` file exports in its `.h` file,
  and use a symbol of another file by including its `.h` file, not by a local
  `extern` declaration.
- **Linkage:** make everything `static` that can be, but first make sure that
  no plugin (see `#module`) needs it.

## Declarations and initialization

- **Function declarations:** name the parameters in function declarations and
  function types: `int f(int count);`, not `int f(int);`.
- **Scope:** declare variables, constants (`enum`s and `static const` tables)
  and types where they are first needed, in the smallest enclosing scope.
  Types used in function signatures or by several functions belong at file
  scope or in a header.
- **Initialization:** initialize variables in their declaration where
  possible, and declare loop variables in the `for` statement:
  `for (int i = 0; i < n; ++i)`.
- **Structs, unions and arrays:** initialize them with designated
  initializers, `struct foo f = { .a = x };` or `int v[N] = { [i] = x };`, and
  reset them with compound literals, `*f = (struct foo){ .a = x };`. Write an
  all-zero value as `{ 0 }`.
- **Null pointers:** write them as `NULL`, not `0` or `(ptr *)0`.
- **Function pointer types:** define them as pointers, with the parameter
  names: `typedef void (*function_str)(char *arg);`. Do not declare pointers
  to them, such as `function_str *f`, as the `*` hidden in the type makes it
  easy to misread.

## Expressions

- **Assignments:** do not nest assignments in other expressions, unless
  splitting them out would complicate the code, such as by repeating an
  assignment in a loop condition.
- **Increments:** prefer prefix `++` and `--` to the postfix forms when the
  value is not used.
- **Casts:** do not add unnecessary casts, such as of a `void *` assigned to
  another pointer: `char *s = malloc(n);`.
  Do not cast a value to `(void)` to discard it, such as the result of a
  function call; only a macro whose expansion is not meant to be used as a
  value may cast it to `(void)`.

## Truth values

- **`bool`:** use `bool`, not `int`, `char` or another type, for arguments,
  return values and variables that hold a truth value.
- **Boolean contexts:** do not use integers or pointers where a truth value is
  expected: as an operand of `!`, `&&` or `||`, as a value converted to `bool`
  (by assignment, initialization, argument passing or `return`), or as the
  controlling expression of `if`, `while`, `for`, `do`/`while` or `?:`.
  Compare them explicitly instead: `p != NULL`, `n != 0`, `*s != '\0'`.
  - The result of a bitwise AND may be used directly:
    `if (flags & FLAG_FOO)`.
  - Functions defined outside this repository whose `int` result is a truth
    value (zero false, non-zero true) are used directly:
    `if (isdigit(c))`, not `if (isdigit(c) != 0)`.
