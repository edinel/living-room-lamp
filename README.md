# template

A GitHub template repo for starting new projects with a documentation-and-decisions workflow
already wired up: recording reasoning as it happens, Architecture Decision Records for
hard-to-reverse calls, and a GitHub Issues-based PRD/triage workflow.

## What this provides

- **A documenting-decisions convention**, in [CLAUDE.md](CLAUDE.md) (symlinked as `AGENTS.md` for
  non-Claude agents): when a decision is made, record the options that were rejected and why, not
  just the answer that was chosen. That's the part that's expensive to reconstruct later.
- **A documentation-layout convention**: README.md stays plain prose until a topic's notes would
  dominate it or the file collects more than a couple of substantial topics; past that point, the
  topic moves to its own file under `docs/<topic>/` and README.md's job narrows to a table of
  contents — a link plus a short summary per document. Not applied preemptively; a repo whose docs
  never outgrow a few paragraphs never needs to make the switch.
- **ADR practice**: `docs/adr/` and `CONTEXT.md` (a domain glossary) aren't pre-created here —
  they get created lazily, the first time a decision or term actually needs recording, by the
  `/domain-modeling` skill. See [docs/agents/domain.md](docs/agents/domain.md) for how agent
  skills are expected to consume them once they exist.
- **A GitHub Issues-based PRD/triage workflow**: issues and specs live as GitHub issues via the
  `gh` CLI. See [docs/agents/issue-tracker.md](docs/agents/issue-tracker.md) for the conventions,
  and [docs/agents/triage-labels.md](docs/agents/triage-labels.md) for the label vocabulary the
  `triage` skill drives issues through.
- **A "what's next?" convention**: ask an agent in this repo "what's next?" with no task in mind,
  and it checks for open issues labeled `ready-for-agent` — the ones `/triage` has already spec'd
  out with an agent brief — instead of guessing at something to do. See "Picking up work" in
  [docs/agents/issue-tracker.md](docs/agents/issue-tracker.md).

None of the above needs project-specific code to work — it's config files that a handful of
bundled skills read to know where things live in *this* repo (see below). `CLAUDE.md`,
`docs/agents/issue-tracker.md`, and `docs/agents/triage-labels.md` are written generically here;
adapt them once a new repo has real content or different tracker settings.

> [!IMPORTANT]
> None of this documentation writes itself. `/grill-with-docs` is the skill that actually runs an
> interview and fills in `CONTEXT.md`/`docs/adr/` as it goes, and it never starts on its own — see
> [why, below](#grill-with-docs-needs-a-human-to-type-it). Skip it and those files just stay
> empty forever: this repo gives you no more than a blank one would. Type `/grill-with-docs`
> yourself, every time you're sketching a plan or a design, or the whole point of this template
> is wasted. Joining a repo with history that predates this workflow? See
> [backfilling docs for existing history](#backfilling-docs-for-existing-history) —
> `/grill-with-docs` alone won't retroactively fill in decisions that already happened.

## Bundled skills

The repo is standalone: the skills this workflow depends on are vendored under
[`.claude/skills/`](.claude/skills/), adapted from Matt Pocock's skill pack, so a repo generated
from this template works without installing anything globally first.

| Skill | Role in the workflow |
| --- | --- |
| `domain-modeling` | Creates and edits `CONTEXT.md` and `docs/adr/*.md` lazily, the moment a term or a hard-to-reverse decision actually needs recording |
| `grilling` | Interview/stress-test skill — challenges a plan or decision by relentless questioning |
| `grill-with-docs` | Runs a `/grilling` session while using `/domain-modeling` to capture the glossary/ADRs as it goes. **Manual only — never fires on its own.** See below |
| `grill-me` | Shortcut that force-starts a `/grilling` session directly. `grilling` can already auto-trigger on stress-test language; this guarantees one regardless of whether the model would have judged the moment as fitting. **Manual only — never fires on its own.** |
| `codebase-design` | Shared vocabulary for deep modules (module, interface, seam, adapter, depth) — auto-triggers whenever a module or interface is being designed |
| `improve-codebase-architecture` | Scans the existing codebase for refactoring opportunities and, once you pick one, grills it into a `CONTEXT.md`/ADR update. **Manual only — never fires on its own.** The way to backfill docs for history that predates `/grill-with-docs` — see below |
| `to-prd` | Turns the current conversation into a PRD and publishes it as a GitHub issue, per `docs/agents/issue-tracker.md` |
| `to-issues` | Breaks a plan/spec/PRD into independently-gradable GitHub issues, tracer-bullet style |
| `triage` | Drives issues through the five-role state machine, per `docs/agents/triage-labels.md` |
| `handoff` | Compacts the current conversation into a handoff document, saved outside the repo, for a fresh agent to pick up — including which skills that next session should call. **Manual only — never fires on its own.** |
| `wait-what` | Asks the agent to re-pitch its last message in plain, controlled language, using `CONTEXT.md`'s vocabulary. For when a reply didn't land. **Manual only — never fires on its own.** |

Updating a bundled skill here does not update anyone's global copy, and pulling a newer version
from the upstream pack means re-copying it into `.claude/skills/` by hand — vendoring trades that
sync cost for the repo working standalone.

### `grill-with-docs` needs a human to type it

Most skills can start themselves: Claude reads every skill's description and decides on its own
when the conversation matches one closely enough. `grill-with-docs` opts out of that
(`disable-model-invocation: true` in its frontmatter) — no matter how relevant the moment looks,
it will not start on its own. The only way to run it is to type `/grill-with-docs` yourself.

That's deliberate. It launches a `grilling` session — a round-by-round interrogation of a plan or
design, question after question until nothing is left silently assumed — which is disruptive
enough that it should be something you choose, not a judgment call Claude makes for you
mid-conversation.

It's also, practically, the main place the documentation in this repo actually gets written.
Nothing else populates `CONTEXT.md` or `docs/adr/` on its own; a normal conversation, however
good, doesn't write them for you. Skip `/grill-with-docs` and those files stay empty regardless
of how much design work actually happened — the decisions get made, they just never get recorded.
Reach for it whenever you're sketching a plan, a design, or an architecture change: that's
precisely the moment it exists for.

### Backfilling docs for existing history

`/grill-with-docs` only captures decisions as they're made going forward. If a repo already has
real history — commits, modules, trade-offs baked into the code — from before anyone started
typing `/grill-with-docs`, that history doesn't retroactively appear in `CONTEXT.md` or
`docs/adr/`. `/improve-codebase-architecture` is the catch-up path for exactly that case:

1. It scans the codebase (leaning on recent commit history to find hot spots) for shallow modules,
   scattered understanding, and other architectural friction, using the vocabulary from
   `codebase-design` (module, interface, seam, adapter, depth).
2. It writes the findings to a self-contained HTML report — a browsable list of candidate
   refactors, never committed to the repo — and asks you to pick one.
3. Once you pick, it runs a `grilling` session on that specific candidate and updates
   `CONTEXT.md`/`docs/adr/` as real decisions get made, the same way `/grill-with-docs` does.

Like `/grill-with-docs`, it's `disable-model-invocation: true` — type `/improve-codebase-architecture`
yourself, it will not start on its own. Reach for it once, early, on a repo with pre-existing
history, and again periodically as more of that history accumulates without a corresponding
`/grill-with-docs` session.

### Working with agents other than Claude Code

`.claude/skills/` and `CLAUDE.md` are authoritative — edit those, never their counterparts.
Two symlinked entry points make the same content reachable for tools that don't look in
Claude-specific locations:

- `AGENTS.md` → `CLAUDE.md`
- `.agents/skills/<name>` → `.claude/skills/<name>`, one symlink per skill in the table above

Both sides resolve to the same files, so there's nothing to keep in sync by hand.

## Using this template

```
gh repo create <name> --template pdehlke/template --public --clone
```

GitHub does not copy labels when generating a repo from a template. Run this once, right after
creating the new repo, to bring the triage labels over:

```
gh label clone pdehlke/template --repo <owner>/<name>
```

(`wontfix` already exists on new GitHub repos by default and needs no cloning.)

From there, adapt `CLAUDE.md` for the new repo's actual stack and scope. This repo ships under
[`LICENSE.md`](LICENSE.md) (0BSD, no attribution required) — keep it, update the copyright line
to the new repo's own author, or swap in a different license entirely if the new repo needs
different terms.
