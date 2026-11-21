---
name: work-doc
description: Write or update the technical.md + overview.md pair in docs/fix/ or docs/feature/ for a finished fix or feature, and its row in docs/README.md. Use when a fix or feature is done, before opening or updating its PR, or after it merges to fill in the commit hash. Triggered by /work-doc [slug].
---

# Document a fix or feature

Every fix or feature has a folder `docs/fix/<slug>/` or `docs/feature/<slug>/` holding two files, plus a row in `docs/README.md`. This skill creates or updates them.

## 1. Work out what is being documented

- **Fix or feature?** A fix corrects wrong behaviour (a crash, a glitch, a wrong value). A feature adds or changes behaviour on purpose. Use the branch prefix (`fix/` or `feature/`) as the default; ask if it is unclear.
- **Slug:** lowercase, underscores, named after what the doc is about rather than the branch, e.g. `alt_tab_script_crash`, `mouse_aim`. Use the argument if one was given.
- **Search before creating.** Look through `docs/README.md` and `docs/fix/`, `docs/feature/` for a folder on the same topic. If one exists, update it instead of creating a duplicate.
- Collect the facts: `git log master..HEAD`, `git diff master...HEAD --stat`, the PR number if there is one, the test results from before and after the change, and what was checked in the game.

## 2. Header (first lines of both files)

```markdown
# <Title>: technical notes        (or ": overview")

**Status:** merged | open PR #N | no commit yet · **Commits:** <short hash> (#PR), … · **Last checked against:** <master short hash>
```

- Before merge: `open PR #N` (or `no commit yet`) and the branch's commits.
- After merge: `merged` and the squash commit on master with its PR number. Run the skill again after merging to fill this in.

## 3. `technical.md` (for developers)

Sections, in this order. Leave out a section only if it really doesn't apply.

1. **Symptom** (fix) or **Goal** (feature): what happened or what was wanted, and how to reproduce it.
2. **Root cause** (fix) or **Design** (feature): function names and addresses, hooks, overrides, cvars, config keys. Refer to functions by their manifest names (`Cloth_Step_8219ADD8`), not `sub_` names or raw addresses. If one still has a generic name, name it first with `/name-functions`. Label claims as **Verified** (read in code, logs or git), **Reported** (someone said it) or **Inference** (reasoned, not tested).
3. **What changed:** with file paths, plus the functions named or renamed in the manifest (`address  name  (was …)`).
4. **Verification:** the commands run, before and after (see `/run-tests`), and what was checked in the game.
5. **Not tested:** be explicit.
6. **Follow-ups** (optional).

## 4. `overview.md` (for players and non-developers)

Plain language, no code, no addresses.

1. **What players saw** (fix) or **What it does** (feature).
2. **What caused it** (fix only): one short, plain explanation.
3. **What's different now.**
4. **What players need to do:** settings, launcher options, or "Nothing".
5. **Known issues** (optional).

## 5. Index

Add or update the row in the right table of `docs/README.md`:

```markdown
| [<slug>](fix/<slug>/overview.md) | <status> | <commits> | <one-line summary> |
```

Keep each table sorted by slug.

## 6. Checks before finishing

- Every game function the doc mentions has a real manifest name. List any that still don't, so they can be named before the PR.
- No game text, dialogue, script code or asset data pasted in. Paraphrase instead.
- No local paths (`C:\Users\…`, `/Users/…`, `D:\…`), personal names beyond GitHub handles, chat quotes, or links to private pages.
- Relative links resolve. Paths in docs are repo-relative, e.g. `docs/fix/<slug>/`.
- Both files have the same header.
- Tell the user which files were created or updated, and anything you couldn't fill in (e.g. an unknown commit hash).
