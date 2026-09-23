# Commit Conventions

**Status**: Accepted

- Use `<scope>: <imperative outcome>` in English. Keep the subject at 60 characters when practical,
  never over 72, and omit the trailing period. Scopes include `rhi`, `metal`, `render`, `shader`,
  `scene`, `asset`, `engine`, `editor`, `app`, `core`, `tool`, `build`, `ci`, `docs`, and `test`.
- On a development branch, make one explainable behavior or constraint one commit. Keep its implementation,
  tests, and nearby documentation together. Do not split work to manufacture commit count or combine unrelated files
  into a cleanup wave.
- For a milestone integration, add its identifier to the subject, for example
  `render: add native temporal reconstruction (M6.2)`. Keep independent fixes and CI changes separate.
- Write the final message from the resulting behavior. Do not concatenate development commit logs
  or PR checklists. Use concrete verbs and plain technical prose; remove repeated summaries and
  promotional claims. Retain the facts, failed gates and limits that explain the result.
- A body, when useful, records the problem, decision, tradeoff, and durable reference. Validation
  details belong in the pull request unless they are essential to understanding the decision.
- Commit messages describe engineering outcomes, never execution bookkeeping. Do not mention work
  items, review rounds, backlogs, prompts, agents, models, plans, or checkbox completion.
- The commit author's configured personal identity may appear only in Git author metadata. It must
  not appear in the subject, body, trailers, repository content, URLs, examples, or home-directory
  paths. Do not add AI co-author trailers or any other tool identity.
- Use local `fixup!` commits while iterating and autosquash them before review. Published `main`
  contains no WIP or fixup commits. Routine integration never rewrites published history; the
  two owner-authorized consolidations on 2026-09-23 are recorded in the
  [history recovery guide](../guides/history-consolidation.md). They do not authorize further rewrites.
- Source changes pass formatting, the relevant tests, and a build. Pull requests pass the full build,
  test, format, and policy suite; renderer/RHI/shader changes also provide appropriate GPU validation,
  capture, image, or performance evidence.

## Branches and integration

- `main` is protected and always buildable. Routine direct pushes, force pushes, merge commits and
  rebase merges are disabled. GitHub permits squash merge only; restore protection immediately
  after any separately authorized recovery operation.
- Use one short-lived outcome branch: `codex/<outcome>`, `feat/<outcome>`, `fix/<outcome>`,
  `docs/<outcome>`, or `spike/<question>`. Do not keep `develop` or release branches without
  multiple supported release lines.
- Default to one squash commit per milestone. Keep design, implementation, validation and closure
  together in the final integration PR where practical. Fine-grained development commits and
  per-change gates remain useful on the branch; they do not require rebase merge onto `main`.
- For a milestone developed in stages, prefer one final integration PR. Intermediate reviews and
  evidence may use temporary branches or draft PRs without merging each stage into `main`.
  If independent fixes or separately shipped stages need earlier integration, separate squash PRs
  are acceptable; do not delay a necessary fix or routinely rewrite `main` to force an exact count.
- Refresh the branch against current `main` before final review and rerun affected gates. Merge
  with `gh pr merge --squash` (add `--auto` when waiting for CI), never `--rebase` or `--merge`.
  Explicitly choose a policy-compliant squash subject/body; GitHub's defaults use the PR title
  with an empty body. PR titles follow the commit subject convention above.
- Preserve named validation revisions before deleting development branches: keep an immutable
  evidence tag or a verified archived bundle and link its recovery location from the validation
  record. Record the final squash SHA and verify its file tree against the validated branch tip;
  rebasing or squashing does not turn failed gates or scoped exceptions into passing results.
- A spike records its result, then a clean implementation branch carries accepted production work.
