# Pending source coverage

**Status**: Accepted

A partial renderer build declares its exact pending cpp set through `xmake/port.lua`, the same
family list the build excludes. The Render target exports its schema version, that file's SHA-256
and exact expanded paths. Shared validation rejects stale, malformed, duplicate, escaping,
nonexistent or unexpectedly compiled entries. Only this validated set may have zero target or
compilation-database membership; every file still has a unit owner, and every retained file keeps
full coverage. Policy reports both checked and pending counts.

Every Source header still compiles standalone with its owning target's defines. Concrete pending
APIs are mechanically guarded while shared types and pure builders remain visible. Each family
removes its block and guards together; removing `port.lua` restores unconditional full coverage.
No failure allowlist is involved.

An inactive unit may defer include-walk roots only when it owns a nonempty compiled-source set
that is wholly pending and has no active target, membership or compilation context. Ownership and
alias checks still cover its files; active include traversal reaching it still fails. The checker
reports the deferred root and inactive unit counts.

The [module contract](modules.md#ownership-precedence) keeps every other ownership and coverage
rule in force.
