# Documentation Conventions

**Status**: Accepted

Documentation has one owner for each kind of fact. A newer lower-precedence document does not
silently override an accepted higher-precedence decision.

## Types and precedence

1. **ADR** (`docs/decisions/`): one durable architectural decision. Accepted ADRs are immutable;
   change them with a superseding ADR.
2. **Convention** (`docs/conventions/`): enforceable project-wide behavior. Update it with the code
   or tooling that makes the rule true.
3. **Architecture or guide** (`docs/architecture/`, `docs/guides/`, `docs/frame-pipeline.md`): current
   system shape and operator workflow. These describe the repository as it exists now.
4. **Roadmap** (`docs/roadmap.md`): accepted ordering, outcomes, gates, and explicit deferrals. It is
   the sole owner of current milestone identifiers and boundaries, not an implementation transcript.
5. **Active plan** (`docs/plans/`): an accepted change currently being executed. It may contain
   sequencing and exit criteria but does not become a permanent dependency of source comments.
6. **Milestone record** (`docs/milestones/`): compact shipped behavior, evidence, known limits, and
   durable deviations at a boundary.
7. **Postmortem** (`docs/postmortems/`): closed symptom, evidence, root cause, correction, prevention.
8. **Research** (`docs/research/`): dated evidence and synthesis. It is frozen and non-normative;
   milestone sequences inside it are historical proposals, and decisions derived from it must be
   restated in an ADR or roadmap.

README and `AGENTS.md` are navigation and operation surfaces. They summarize; they do not introduce
new architecture decisions. Implemented historical specs may remain as frozen design context, but
current documents cannot depend on deleted executor plans.

Public-facing surfaces such as README, GitHub About, release text, and gallery captions describe
what the renderer does now before naming a short set of future feature themes. They do not expose
internal milestone numbers, plan/task status, or present an unimplemented backend as a current
capability. Detailed sequencing and gates belong in the roadmap and internal records.

## Status lifecycle

Use one explicit `**Status**:` field near the top of every ADR, convention, roadmap, active plan,
milestone, postmortem, research note, and retained spec.

- `Proposed`: open for decision; not binding.
- `Accepted`: binding and current.
- `In progress`: the single accepted plan being executed.
- `Implemented`: shipped current behavior; update when behavior changes.
- `Frozen — non-normative`: retained evidence or historical design context; never a current rule.
- `Superseded by <link>`: replaced; retained only for provenance.
- `Closed`: a resolved postmortem.

At most one file under `docs/plans/` may be `In progress`; zero means no milestone is currently in
execution. When a plan closes, extract durable decisions to
ADRs, current behavior to architecture/guides, evidence to a milestone record or postmortem, then
remove the executor plan from the published baseline.

## Writing and links

- State the fact first, then rationale and evidence. Use present tense for current behavior and
  dated past tense for a milestone record.
- Link to symbols, documents, specifications, or stable upstream sources. Do not cite source line
  numbers, commit hashes, review rounds, or task identifiers as explanations.
- Use repository-relative paths in files and examples. Never store a personal home-directory path.
- Keep operational documents concise: README and `AGENTS.md` at most 250 lines; conventions,
  architecture, guides, roadmap, milestones, postmortems, and the active plan at most 300 lines.
  Frozen research and historical specs are exempt.
- Keep raw captures, temporary measurements, and recovery bundles outside the published source tree.
