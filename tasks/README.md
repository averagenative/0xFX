# Task Coordination System

File-based task tracking for autonomous and coordinated work on 0xFX.

## Structure

```
tasks/
  queue.json       — Machine-readable task queue (source of truth for agents)
  engine.md        — Engine API, DSP, amp models, effects, cab IR (Layers 1-2)
  frontend.md      — GUI, ImGui, knobs, pedalboard, visual assets (Layer 3)
  infra.md         — Build system, CI, packaging, tooling, plugin wrappers (Layer 4 + cross-cutting)
  testing.md       — Test infrastructure, test cases, quality gates
  distribute.sh    — Status report script
```

## Task Queue (queue.json)

The `queue.json` file is the **machine-readable source of truth** that agents read to find work. Each task has:

```json
{
  "id": "TASK-010",
  "priority": "CRITICAL",
  "title": "Implement preamp gain stages",
  "domain": "engine",
  "phase": 2,
  "status": "done",
  "claimedBy": null,
  "depends": ["TASK-009"],
  "estimatedTokens": 30000,
  "acceptanceCriteria": [
    "Cascaded waveshaping (1-4 stages) per amp model",
    "tanh, asym, atan, hard clip waveshapers implemented",
    "DC blocking highpass per stage",
    "Test: sine through amp at gain=1.0 has added harmonics"
  ],
  "files": ["src/engine/internal/amp.c"]
}
```

### Priority Levels

| Level | Meaning | Examples |
|-------|---------|---------|
| **CRITICAL** | Blocks other work. Must be done first. | Engine API, core DSP, build system |
| **HIGH** | Needed for current milestone. | Effects, amp models, signal chain wiring |
| **MEDIUM** | Needed for next milestone. | Cab IR, presets, plugin wrappers |
| **LOW** | Nice to have, no dependencies. | Polish, experimental effects, asset gen |

### Status Flow

```
queued → claimed → in_progress → done
                → blocked (with reason)
```

### Claim System

Prevents duplicate work across parallel agents:

1. Agent reads `queue.json`, finds highest-priority `queued` task with deps met
2. Agent sets `status: "claimed"` and `claimedBy: "agent-name"` in queue.json
3. Agent sets `status: "in_progress"` when implementation starts
4. Agent sets `status: "done"` when tests pass
5. If an agent crashes or stalls, a `claimed` task with no progress after 10 minutes can be reclaimed

### Token Estimates

Budget planning for each task. Rough guidelines:
- **5K tokens**: Simple wiring, config changes
- **15K tokens**: Port existing DSP from 0x808, write tests
- **30K tokens**: New DSP implementation + tests
- **50K tokens**: Complex feature (cab IR convolution, preset system, GUI panel)
- **80K+ tokens**: Multi-file feature (plugin wrapper, multi-chain routing, tone matching)

## Workflow

### For Agents

```
1. Read queue.json → find next task (highest priority, queued, deps met)
2. CLAIM: set status="claimed", claimedBy="your-name"
3. Read acceptance criteria — these are your definition of done
4. Implement the code
5. Write tests that verify acceptance criteria
6. Run tests: cmake --build build && ./build/fx_api_test
7. DONE: set status="done" in queue.json + task file
8. Repeat
```

### For Humans

```bash
./tasks/distribute.sh              # Full status report
./tasks/distribute.sh queued       # What's available
./tasks/distribute.sh in_progress  # What's active
./tasks/distribute.sh done         # What's finished
cat tasks/queue.json | jq '.[] | select(.status=="queued")' # Machine-readable
```

## Rules

- **Never skip tests.** If tests don't exist yet, write them first.
- **A task is only `done` when acceptance criteria are met and tests pass.**
- **Claim before coding.** Write your name into queue.json before touching source files.
- **Domain separation.** Prefer picking tasks from different domains when running parallel agents.
- **Don't modify files another agent owns.** Check `claimedBy` and `files` fields.
- **Engine tasks must respect the API boundary** — no internal struct exposure.
- **Blocked tasks** must note what they're blocked on in the `blockedReason` field.
