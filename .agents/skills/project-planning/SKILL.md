
---
name: project-planning
description: Converts feature requests into architecture decisions, dependency-aware tasks, and measurable acceptance criteria.
---

# Project Planning Protocol

1. Read AGENTS.md and inspect the existing repository.
2. Identify the requested outcome and current behavior.
3. List requirements, constraints, assumptions, and risks.
4. Identify affected modules and external dependencies.
5. Split work into tasks that can be independently verified.
6. Define task IDs, dependencies, owners, and acceptance criteria.
7. Mark tasks that can run in parallel without overlapping edits.
8. Identify which decisions are reversible and which are not.
9. Update docs/SPEC.md and tasks/backlog.md.
10. Return the execution plan to the orchestrator.

Do not create unnecessary tasks for work that is already complete.
Do not start implementation before defining acceptance criteria
for non-trivial changes.

