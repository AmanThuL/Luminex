# Commit Conventions

**Status**: Accepted

- Use `<scope>: <imperative outcome>` in English. Keep the subject at 60 characters when practical,
  never over 72, and omit the trailing period. Scopes include `rhi`, `metal`, `render`, `shader`,
  `scene`, `asset`, `engine`, `editor`, `app`, `core`, `tool`, `build`, `ci`, `docs`, and `test`.
- Make one explainable behavior or constraint one commit. Keep its implementation, tests, and nearby
  documentation together. Do not split work to manufacture commit count or combine unrelated files
  into a cleanup wave.
- A body, when useful, records the problem, decision, tradeoff, and durable reference. Validation
  details belong in the pull request unless they are essential to understanding the decision.
- Commit messages describe engineering outcomes, never execution bookkeeping. Do not mention work
  items, review rounds, backlogs, prompts, agents, models, plans, or checkbox completion.
- The commit author's configured personal identity may appear only in Git author metadata. It must
  not appear in the subject, body, trailers, repository content, URLs, examples, or home-directory
  paths. Do not add AI co-author trailers or any other tool identity.
- Use local `fixup!` commits while iterating and autosquash them before publication. Published `main`
  contains no WIP or fixup commits and is never rewritten after the M3.1 cutover.
- Source changes pass formatting, the relevant tests, and a build. Pull requests pass the full build,
  test, format, and policy suite; renderer/RHI/shader changes also provide appropriate GPU validation,
  capture, image, or performance evidence.

## Branches and integration

- `main` is protected, always buildable, and never force-pushed after the baseline cutover. Merge
  commits and direct pushes are disabled.
- Use one short-lived outcome branch: `feat/<outcome>`, `fix/<outcome>`, `docs/<outcome>`, or
  `spike/<question>`. Do not keep `develop`, milestone-wide, or release branches without multiple
  supported release lines.
- Rebase before review. Rebase-merge 2–5 independently useful green commits; squash a single logical
  change or local trial-and-error. A spike records its result, then a clean implementation branch
  carries accepted production work.
