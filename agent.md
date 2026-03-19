# HULK Compiler Agent Guide

## Project Summary

HULK Compiler 2026 is a compiler project for the HULK language, implemented in C99 and organized as a staged compiler pipeline.

The repository currently contains:

- A reusable regex-to-DFA lexical analyzer generator in `generador_analizadores_lexicos/`
- A reusable LL(1) parser generator in `generador_parser_ll1/`
- A HULK compiler facade in `hulk_compiler.c`
- HULK AST construction in `hulk_ast/builder/`
- Semantic analysis in `hulk_ast/semantic/`
- Code generation modules in `hulk_ast/codegen/`
- Unit tests in `tests/`

Important: `README.md` still describes the project mainly as lexer + parser, but the codebase already includes AST, semantic, and codegen modules. Agents must treat the repository state as the source of truth.

Primary goals for any agent working here:

- Preserve compiler correctness first
- Keep module boundaries clear
- Avoid architectural drift and duplicated logic
- Prefer incremental, testable changes over broad rewrites
- Do not claim support for features that are only partially implemented

## Technology Stack

### Language and Build System

- Language: C99
- Compiler: `gcc`
- Build system: `make`
- Required compile flags: `-Wall -Wextra -std=c99 -g -D_GNU_SOURCE -MMD -MP`

### Tooling and Dependencies

- Lexer generation: `flex`
- Code generation dependencies: `llvm-config-18` or `llvm-config`
- Link dependencies from `Makefile`: `-lfl` and LLVM-provided flags

### Target Operating System

- Reference OS: Linux
- Do not hardcode distro-specific behavior unless the task explicitly requires it

### Observed Local Environment Snapshot

Use this only as context, not as a strict requirement:

- `gcc 13.3.0`
- Linux kernel available in the local development environment
- `flex` is not currently installed in this environment
- `llvm-config-18` / `llvm-config` are not currently installed in this environment

## Architecture and Module Boundaries

### Current High-Level Layout

- `main.c`: CLI entrypoint and execution flow
- `hulk_compiler.h/.c`: top-level compiler facade and orchestration
- `hulk_tokens.h/.c`: HULK token definitions
- `error_handler.h/.c`: centralized error reporting
- `generador_analizadores_lexicos/`: reusable lexer generator pipeline
- `generador_parser_ll1/`: reusable LL(1) parser infrastructure
- `hulk_ast/core/`: AST core nodes, visitor, and context
- `hulk_ast/builder/`: AST construction from token stream / parse structure
- `hulk_ast/semantic/`: scope handling, type logic, semantic checks, desugaring
- `hulk_ast/codegen/`: code generation support
- `hulk_ast/printer/`: AST pretty-printing and debug output
- `tests/`: unit and subsystem tests

### Design Rules Agents Must Respect

- Keep `hulk_compiler.*` as the orchestration boundary, not as a dumping ground for subsystem logic.
- Keep lexer concerns inside `generador_analizadores_lexicos/`.
- Keep grammar, FIRST/FOLLOW, and LL(1) table logic inside `generador_parser_ll1/`.
- Keep AST node ownership, traversal, and allocation rules inside `hulk_ast/core/`.
- Keep AST construction logic inside `hulk_ast/builder/`.
- Keep semantic checks and scope/type rules inside `hulk_ast/semantic/`.
- Keep code generation concerns inside `hulk_ast/codegen/`.
- Do not mix parsing, semantic analysis, and code generation in the same implementation unit unless there is already an established pattern requiring it.
- Prefer extending an existing subsystem over introducing parallel helper code in a new location.

### Patterns Already Present

- Facade / orchestration through `hulk_compiler.*`
- Pipeline-style construction for lexer setup
- Visitor-style traversal in AST support
- Context-based memory/resource ownership for AST structures
- Reusable subsystem separation for lexer generator and parser generator

### Roadmap vs Implemented State

When documenting or modifying behavior:

- Treat code present in the repository as implemented state
- Treat anything only described in course material or `hulk-docs.pdf` as roadmap or design context unless confirmed in code
- Do not present partially implemented modules as production-complete without tests and integrated usage proving that state

## Code Conventions

### Naming

- Use English for variables, functions, types, enums, and macros unless the project already has a fixed Spanish name for a module or directory.
- Preserve existing public names and file naming patterns.
- Use descriptive names tied to compiler concepts instead of generic names like `data`, `helper`, or `temp`.

### Comments and Documentation

- Write explanatory comments in Spanish when they add value.
- Keep comments short and intent-focused.
- Do not restate obvious C syntax.
- Update nearby comments if a behavior change makes them stale.

### Typing and Headers

- Follow C99 strictly.
- All headers must use include guards with `#ifndef / #define / #endif`.
- Prefer explicit struct types and function signatures over ambiguous macros or hidden side effects.
- Keep public headers focused on stable interfaces, not internal implementation details.

### Functions and File Organization

- Prefer small, focused functions.
- If a function grows too much, split by responsibility rather than adding more branches.
- Group related behavior by subsystem and keep files cohesive.
- Avoid new global state unless there is a strong architectural reason.

### Memory and Resource Management

- Every owned allocation must have a clear release path.
- Avoid leaks in normal execution and error paths.
- Preserve existing context-based ownership patterns when working with AST or parser structures.
- Prefer deterministic cleanup over implicit process-exit cleanup.

### Warnings and Quality Bar

- Code must build cleanly with `-Wall -Wextra -std=c99`.
- Zero warnings is the expected standard.
- Do not silence warnings without understanding the root cause.

## Workflow

### Branching Model

This project follows GitHub Flow with short-lived branches from `main`.

Branch naming convention:

- `feature/<short-description>`
- `fix/<short-description>`
- `refactor/<short-description>`
- `docs/<short-description>`
- `test/<short-description>`

Examples:

- `feature/closures`
- `fix/lexer-string-escape`
- `refactor/ast-pool`

### Commit Rules

Make atomic commits with this format:

```text
<type>: <short description>

[optional body]
```

Allowed commit types:

- `feat:`
- `fix:`
- `refactor:`
- `test:`
- `docs:`
- `build:`

Guidelines:

- One logical change per commit
- Separate refactors from behavior changes when possible
- Include tests in the same commit as the feature or fix when practical

### Pull Request Expectations

- Push the topic branch and open a PR against `main`
- Prefer reviewable changes over giant PRs
- Ensure build and relevant tests pass before asking for review
- Merge using squash merge to keep history clean

## Build and Test Commands

Use the exact project commands below.

### Install Prerequisites

Required tools for full build/test flow:

- `gcc`
- `make`
- `flex`
- `llvm-config-18` or `llvm-config`

Example for Ubuntu/Debian environments:

```bash
sudo apt-get install gcc flex make
```

LLVM packages are also required for codegen-related build targets.

### Build

```bash
make
```

### Run Compiler

```bash
./hulk_compiler test.hulk
./hulk_compiler "let x = 5;"
```

### Test Build

```bash
make test-build
```

### Full Test Suite

```bash
make test-all
```

### Individual Test Targets

```bash
make test-lexer
make test-parser
make test-ast
make test-hulk-ast
make test-ast-builder
make test-semantic
make test-codegen
```

### Utility Targets

```bash
make clean
make rebuild
make test
make test-file
```

### Execution Notes

- `make` and `make test-all` depend on `flex`.
- Codegen-related objects and tests depend on `llvm-config-18` or `llvm-config`.
- In the current local environment snapshot, `flex` and LLVM config tools are missing, so agents must not assume build/test commands will succeed without installing prerequisites first.

## Skills Registry

Agents may use modular skills from `/skills`, but only through selective loading.

Rules:

- Do not preload every skill at startup.
- First inspect whether `/skills` exists in the repository.
- Only open the specific skill relevant to the current task.
- Prefer the minimum number of skills needed to complete the task.
- If a skill references additional files, load only the files needed for that task.
- If `/skills` does not exist, continue normally without inventing skill content or fake capabilities.
- Do not duplicate stable project rules inside a skill when they already exist in this file.

Recommended loading sequence:

1. Read this `agent.md` first.
2. Inspect the current task and target subsystem.
3. Check whether `/skills` contains a directly relevant skill.
4. Load only that skill and only when it materially improves execution.

## Agent Operating Rules

- Use the repository state as the primary source of truth.
- Verify assumptions in code before changing architecture-level behavior.
- Prefer small, subsystem-local changes over cross-cutting rewrites.
- Keep tests aligned with the subsystem being changed.
- If documentation and code disagree, trust the code and update documentation if part of the task.
- When a feature appears partially implemented, say so explicitly instead of filling gaps with speculation.
- Preserve existing command names, public headers, and module boundaries unless the task explicitly requires a redesign.

## Non-Goals for Agents

- Do not rewrite the project around a different parser strategy unless explicitly requested.
- Do not introduce new external dependencies casually.
- Do not move unrelated modules just to satisfy stylistic preferences.
- Do not invent unsupported language features based only on course roadmap material.
