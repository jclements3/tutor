# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Status

This repository is currently empty — a freshly initialized git repo with no commits, source files, README, or build configuration. There is no codebase to document yet.

When code is added, this file should be updated to describe:

- Build, test, and lint commands (including how to run a single test).
- High-level architecture that spans multiple files.
- Project-specific conventions that aren't obvious from reading the code.

## Workspace Context

This project lives under `/home/james.clements/projects/`, a multi-project workspace documented in `../CLAUDE.md`. The parent file describes sibling projects (odin, spectre, morpheus, valkyriemsa, c-taos, irsg, hwil, etc.) focused on ballistic missile defense simulation, trajectory analysis, and IR scene generation. Conventions enforced across most of those projects:

- **7-bit ASCII only** in TAOS-related projects — no Unicode in generated files.
- **Units** — TAOS internals use English (feet, seconds, pounds); Parquet outputs use metric.
- **No `git push`** — VPN issues may prevent pushing to remote.

Whether those conventions apply here depends on what `tutor/` becomes; revisit when the project's purpose is established.
