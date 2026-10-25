# Upstream

This repository is a fork of [SatyPardus/wowdecomp](https://github.com/SatyPardus/wowdecomp),
a C++ decompilation of the World of Warcraft 3.3.5a client (build 12340).

## Fork point

```
d36a92c08630c3dd219528337e4c0ef9c0d5745b   2026-10-17
Merge pull request #21 from SatyPardus/original-335
```

Everything before that commit is upstream's work. After it, the history holds this fork's commits
and the upstream commits brought in by the merges below.

## Last merged

```
20f9120e0a8f064338ecd9f2dd6cbc01642bf695   2026-10-23
Update Version information and automated build script
```

Merged by `merge: Take upstream wowdecomp up to 20f9120`: 235 upstream commits, 216 of them not
merges, over 435 files.

## Tracking upstream

Upstream `main` is merged periodically with a real merge commit. Its commits are kept as they are,
with their authors, dates and hashes, so every upstream commit can be found here by its own hash.

```sh
git remote add upstream https://github.com/SatyPardus/wowdecomp.git
git fetch upstream
git log --oneline HEAD..upstream/main
git merge upstream/main
```

When a merge conflicts, upstream's code wins for the decompilation itself and this fork's additions
are applied again on top: each `wxl::seam::...` call stays exactly where it was relative to the
client's logic, and `WarcraftXL/`, the build files and this fork's documents are kept. After the
merge, `git diff <fork point> <previous head>` and `git diff upstream/main HEAD` list the same
changes, and core's derived tables (`tools/offsets`, `tools/layouts`, `tools/fields`) and the seam
checks are run again on the new tree.

Nothing this fork adds goes back upstream through these merges.

## Credits

Upstream's contributors in the merged range, from `git shortlog -sn d36a92c..20f9120e` (merge
commits counted):

| Contributor | Commits |
|---|---|
| T1ti | 202 |
| Saty (SatyPardus) | 24 |
| Shard-MW | 4 |
| Alyst3r | 2 |
| UncleJackalope | 2 |
| Chris-Bfx | 1 |

## Upstream branches

| Branch | What it is |
|---|---|
| `main` | the decompilation, x64 primary target |
| `original-335` | reference material from the original client |
| `vulkan` | upstream's own Vulkan work |
| `mapeditor` | an editor built on the port |
