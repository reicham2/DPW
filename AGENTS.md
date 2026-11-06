# Contributor instructions

These instructions apply to the whole repository, including every AI contributor.
Use this file as the shared source of contributor guidance.

## Documentation is part of the change

- Read [README.md](README.md) and the relevant sections of
  [the operations reference](docs/operations.md) before changing behavior.
- For each change, review documentation impact: installation, commands,
  dependencies, configuration/defaults, authentication, user-facing behavior,
  database/schema compatibility, retention, background jobs, CI and deployment.
  Update affected documentation in the **same PR** as the implementation.
- Verify commands and claims against the actual source and, where practical,
  execute the documented steps in a disposable environment. Do not invent
  flags, scripts, settings, permissions or guarantees. Never use production
  data for destructive test or reset commands.
- Keep the README focused on orientation, setup and common tasks. Keep detailed
  operational behavior in the linked reference; prefer source links over
  duplicating inventories that the code already maintains.
- The README's marked repository-reference block is generated. After changing
  its sources, run `node scripts/check-docs.mjs --write`, inspect the diff, and
  review the surrounding prose too. Run `node scripts/check-docs.mjs` and
  `node --test scripts/check-docs.test.mjs` before handing off a change.
  If an intentional source-format change affects the extractor, update it and
  its regression tests rather than disabling or weakening the check.
- In every PR, state which documentation changed and which commands/sources
  were verified. If no documentation change is warranted, explain why the
  existing README and operations reference remain correct for this change.
  A cosmetic README edit is not evidence of documentation correctness.

The automated contract covers selected source-derived facts, documented command
names and repository links, not all prose or real deployment behavior. AI review
and the existing application/upgrade tests remain necessary; no check can promise
that documentation will never become stale.

## Repository essentials

- Production uses `docker-compose.yml`; Make targets use
  `docker-compose-dev.yml`, including the targets ending in `-prod`.
- Schema changes belong in [db/init.sql](db/init.sql). Preserve existing data
  and keep startup schema synchronization idempotent; there is no separate
  migration-file system. Update upgrade fixtures/assertions for intentional
  transformations, not to mask data loss.
- Debug authentication is compile-time gated. Never enable it in production
  images. Keep secrets out of source, documentation, logs and PR descriptions.
- Use the existing focused frontend, backend, schema and API tests for the
  changed behavior. See the README and operations reference for commands and
  destructive-test warnings.
