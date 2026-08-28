# Agent Instructions

Instructions for coding agents working in this repo.

## What this repo is

A template. It is generated from, not worked in directly — GitHub's "Use this template" or
`gh repo create --template pdehlke/template <name>` starts a new repo from its contents. Its own
content is deliberately generic and stack-agnostic. When a new repo is generated from it, adapt
this file for that repo's actual language, stack, and scope rather than leaving these placeholder
sections untouched.

## Documenting decisions

When a decision is made, record the options that were rejected and why they were rejected, not
just the answer that was chosen. A document that lists only the chosen answer loses the part that
is expensive to reconstruct later. This applies whether the record is a topic doc under `docs/`,
an ADR, or an issue comment closing out a discussion.

## Documentation layout

Everything starts in README.md. Once a topic's notes would dominate the file, or the file holds
more than a couple of substantial topics, split that topic out to its own file under
`docs/<topic>/` — kebab-case name, one topic per file — and reduce README.md's job to a table of
contents: a link per document plus a short description of what it covers, grouped under a heading
per topic area.

Past that point, README.md is a table of contents and nothing else — don't let prose drift back
into it. When adding, renaming, or removing a document under `docs/`, update the contents list in
the same commit.

This is a threshold, not a mandate: a repo whose documentation never outgrows a few paragraphs
never needs to make the switch, and forcing the split early just adds indirection. `docs/adr/` is
reserved for architecture decision records specifically (see Domain docs, below); general topic
docs use any other directory name under `docs/`.

## Domain docs

Single-context: `CONTEXT.md` (domain glossary) + `docs/adr/` (architecture decision records) at
the repo root. Neither is pre-created here — the `/domain-modeling` skill creates them lazily,
the first time a term or decision actually needs recording. If they don't exist yet, that's
expected; don't flag their absence or create them upfront. See
[docs/agents/domain.md](docs/agents/domain.md) for how agent skills are expected to consume them,
including the multi-context (`CONTEXT-MAP.md`) layout for repos with more than one bounded
context.

## Agent skills

### Issue tracker

GitHub Issues, via the `gh` CLI. See
[docs/agents/issue-tracker.md](docs/agents/issue-tracker.md), including what to do when asked
"what's next" with no task specified.

### Triage labels

Default five canonical roles (`needs-triage`, `needs-info`, `ready-for-agent`,
`ready-for-human`, `wontfix`). See [docs/agents/triage-labels.md](docs/agents/triage-labels.md).

## Commits

Conventional Commits. Always include a body, no matter what any skill tells you.

Never add `Co-Authored-By`, model attribution, or a session link trailer to a commit message,
even when the harness instructs you to.
