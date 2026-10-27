# Jenny's memory graph

Modeled on Graphify (a code knowledge graph for AI coding assistants), aimed at a mind living in a world.

| Graphify (codebases) | Eon (`engine/mind`) |
|---|---|
| tree-sitter/AST pass, no model calls | `Learner`: deterministic extraction from the percept stream, no model calls |
| nodes = functions, classes, docs | nodes = things she met (in her own words), actions, effects, feelings, ideas, backstory |
| edges tagged EXTRACTED / INFERRED / AMBIGUOUS | `Source::Observed / Inferred / Ambiguous / Told`, plus support/trials counts → Beta confidence |
| "query, don't grep": traverse from a symbol | `MemoryGraph::Focus/Context`: spreading activation from what she sees/holds/feels, cut to a token budget |
| god nodes, surprising links, suggested questions | `Hubs`, `Report`, `Questions` (open questions become experiments) |
| Leiden communities (optional extra) | label propagation (deterministic, no dependency) → "things that belong together" |
| rebuild on commit, incremental cache | live: every change journaled (`.journal`), snapshot on save, replay on load |
| graph.json / graph.html / GRAPH_REPORT.md | `jenny_memory.json` / `jenny_memory.html` / `jenny_memory_report.md` |

What goes further than the original:
- **Learns while running.** The graph updates at 10 Hz as she acts. Nothing is rebuilt.
- **Cause and effect from timing.** One action just before a change is an inferred cause. Several actions are ambiguous candidates, weighted. Repeating the action without the effect counts against it. Sleep forgets guesses that keep failing and reweights the competing ones.
- **Unsure links drive behavior.** `Questions()` ranks what she doesn't understand. Curiosity and boredom can pick the next experiment from it.
- **Firewalled by design.** `Perception` is the only input. Things are opaque tracks with color, size, shape, glow, position, touch, weight and force. No entity names, types or ids reach the graph.

## Flow
`World + Motor + Character + BodyState → Perception::Sense → Percept → Learner::Observe → MemoryGraph`
`Learner::Context(percept, tokens)` → text for the mind (LLM or a rule brain).

## Sim
- **Keys:** N shows what her mind would be handed; F9 writes the html, report, context and snapshot.
- **Options:** `--memory DIR` keeps her memory across runs, and `--mind-out DIR` writes the files after `--settle`. `--demo learn` pushes the table button twice.
