# Releasing

How a change reaches `main` and a release reaches people (moved here
from `CLAUDE.md` on 2026-11-08; the rules themselves are in `CLAUDE.md`).

## CI gates every merge and release — `main` must stay clean

The mandatory order: open the PR → wait for CI green on the PR (all four
platform jobs; local MSVC green is NOT cross-platform green) → merge →
wait for CI green on `main` → only then push the release tag.  Fixes
discovered along the way go through the feature branch, never directly
onto `main`.

## A release is finished, not started

Pushing the tag is the middle of the job, not the end:
`.github/workflows/release.yml` deliberately uploads a **draft** with the
archives and no notes, because the auto-generated "What's Changed" got
confused by an unpublished draft.  So the tail is always the same four
steps, in order, and none of them is optional or someone else's:

1. bump `src/VERSION` through a PR of its own, merge it, main green;
2. push the `v*` tag and wait for the release build;
3. **write the notes by hand on the draft** - what changed and why it
   matters to whoever runs the thing, in the register of the releases
   before it (`gh release view v1.6.0 --json body`), ending with the
   browser link;
4. **publish it** (`gh release edit vX.Y.Z --notes-file ... --draft=false`).

The web build needs no step of its own: the tag deploys Pages by itself.
Do NOT `gh workflow run pages.yml` after tagging - both runs share the
`pages` concurrency group, so the manual one cancels the tag's and
deploys `main` instead of the tag.  Dispatch it by hand only to
re-deploy between releases.

Do not leave a draft standing and hand it back: unless the user says
otherwise, "выкладывай" means through step 4.
