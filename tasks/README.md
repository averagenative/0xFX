# Task Coordination System

File-based task tracking for autonomous and coordinated work on 0xFX.

## Structure

```
tasks/
  engine.md        — Engine API, DSP, amp models, effects, cab IR (Layers 1-2)
  frontend.md      — GUI, ImGui, knobs, pedalboard, visual assets (Layer 3)
  infra.md         — Build system, CI, packaging, tooling, plugin wrappers (Layer 4 + cross-cutting)
  testing.md       — Test infrastructure, test cases, quality gates
```

## Task Format

Each task file uses this format:

```markdown
## TASK-NNN: Short description
- **Status**: queued | in_progress | blocked | done
- **Phase**: 1-11 (from openspec)
- **Priority**: P0 | P1 | P2 | P3
- **Depends**: TASK-NNN (if any)
- **Notes**: Implementation notes, decisions, blockers
```

## Workflow

1. Pick next `queued` task (lowest phase number, highest priority)
2. Set status to `in_progress`
3. Implement + write tests
4. Run tests — all must pass
5. Set status to `done`
6. Pick next task

## Adding Tasks

```bash
# Use the distribute script to sync from openspec/changes/fx-engine/tasks.md
./tasks/distribute.sh

# Or manually add a task to the appropriate file
```

## Rules

- Never skip tests. If tests don't exist yet, write them first.
- A task is only `done` when tests pass.
- `blocked` tasks must note what they're blocked on.
- Engine tasks must respect the API boundary — no internal struct exposure.
