# Documentation Conventions

**Status**: Accepted

Documentation has one owner for each kind of fact. A newer lower-precedence document does not
silently override an accepted higher-precedence decision.

## Types and precedence

1. **ADR** (`docs/decisions/`): one durable architectural decision. Accepted ADRs are immutable;
   change them with a superseding ADR.
2. **Convention** (`docs/conventions/`): enforceable project-wide behavior. Update it with the code
   or tooling that makes the rule true.
3. **Architecture or guide** (`docs/architecture/`, `docs/guides/`, `docs/architecture/frame-pipeline.md`): current
   system shape and operator workflow. These describe the repository as it exists now.
4. **Roadmap** (`docs/roadmap.md` and its parts under `docs/roadmap/`): accepted ordering,
   outcomes, gates, and explicit deferrals. Together they are the sole owner of current milestone
   identifiers and boundaries, not an implementation transcript. The entry owns direction, shared
   delivery rules, the current baseline and the cross-part execution sequence; Rendering Foundations owns M4–M6.5 and interface gate B;
   GPU-Driven Hybrid Rendering owns M7–M11 and independent research; Codebase Refactoring owns
   the R-series structural milestones, R1 in its first file and R2–R4 in its after-M7 file;
   Editor Experience owns UX1 and its placement before M7.1, and UX2 and its placement before N1;
   Neural and Learned Rendering owns N1–N4, the post-M7 delivery order and the learned-technique
   and hardware policies.
   Define each boundary once and link to its owner from summaries and dependency tables.
5. **Active plan** (`docs/plans/`): an accepted change currently being executed. It may contain
   sequencing and exit criteria but does not become a permanent dependency of source comments.
6. **Milestone record** (`docs/milestones/<series>/`): compact shipped behavior, evidence, known
   limits, and durable deviations at a boundary. A new design is written today as the `Proposed`
   milestone record in its series folder; before implementation, it may summarize the intended
   design and verification. Older milestones instead retain a design as `<id>-design.md` in the
   same series folder, a historical form no longer used for new work; such a design changes only
   in path references once its slice is implemented, and anything beyond that needs the owner's
   approval as an explicit exception. The founding design at
   `docs/decisions/0000-founding-design.md` predates the ADR series and is not itself an ADR. At
   closure, update the record with actual behavior and evidence. The roadmap continues to own the
   boundary and completion gate.
7. **Postmortem** (`docs/milestones/<series>/`, not a folder of its own): closed symptom, evidence,
   root cause, correction, prevention.
8. **Research** (`docs/research/`): dated evidence and synthesis. It is frozen and non-normative;
   milestone sequences inside it are historical proposals, and decisions derived from it must be
   restated in an ADR or roadmap.

README and `AGENTS.md` are navigation and operation surfaces. They summarize; they do not introduce
new architecture decisions. Implemented retained designs change only in path references; anything
beyond that needs the owner's approval as an explicit exception. They may remain as design context.
Current documents cannot depend on deleted executor plans.

Public-facing surfaces such as README, GitHub About, release text, and gallery captions describe
what the renderer does now before naming a short set of future feature themes. They do not expose
internal milestone numbers, plan/task status, or present an unimplemented backend as a current
capability. Detailed sequencing and gates belong in the roadmap and internal records.

## Status lifecycle

Use one explicit `**Status**:` field near the top of every ADR, convention, roadmap, active plan,
milestone, postmortem, research note, and retained design.

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
  architecture, guides, milestones, postmortems, the active plan, and each roadmap file at most
  300 lines. The roadmap uses a short entry and named parts instead of a larger single-file
  exception. These are line budgets, not word limits. Frozen research and retained designs are exempt.
- Keep raw captures, temporary measurements, and recovery bundles outside the published source tree.
