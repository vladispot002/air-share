
---
name: implementation
description: Implements a scoped task with minimal changes, tests, and compatibility checks.
---

# Implementation Protocol

1. Read the assigned task and its dependencies.
2. Inspect relevant source files and existing tests.
3. Confirm which files are in scope.
4. Implement the smallest coherent change.
5. Add or update tests.
6. Run targeted checks.
7. Inspect the diff for accidental changes.
8. Report changed files, test commands, actual results,
   and any remaining risks.

Do not silently change public interfaces or architecture.
If a contract must change, report it to the orchestrator.

