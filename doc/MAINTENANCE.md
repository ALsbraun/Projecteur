# CI, dependencies and releases

This repository is `ALsbraun/Projecteur`, with `main` as its default branch.
The files in this change prepare the automation. They do not create remote
repository settings, install a GitHub App, push commits or publish a release.

## First push and required repository settings

Before the first commit, inspect `git status`, `git diff --cached --stat` and
`git ls-files -ci --exclude-standard`. The last command lists already tracked
files that now match `.gitignore`; review those individually. Do not commit
`build/`, `build-rpm/`, `dist/`, RPMs, local IDE state or credentials. The initial
repository setup does not automatically unstage or delete existing files.

After pushing the reviewed source to `main`:

1. Enable GitHub Actions and permit the SHA-pinned `actions/checkout`,
   `actions/upload-artifact` and `actions/download-artifact` actions. Leave the
   default token read-only; individual jobs request only their needed rights.
   Repository/organization policy must permit the release and automerge jobs'
   explicit writes.
2. Let **Fedora 45 RPM** run once. Inspect the native RPM and GNOME evidence.
   Only after the first check exists can it reliably be selected in protection
   settings.
3. Add a **classic branch protection rule for `main`**: require pull requests,
   require the **Fedora 45 RPM** status check from GitHub Actions and require
   branches to be up to date before merging. Disable force pushes and deletion.
   The Dependabot gate checks these fields through GitHub's API and refuses
   automerge if they are absent. A ruleset alone is not currently recognized by
   that gate. Private repositories may need a GitHub plan supporting protection.
4. Enable **Allow auto-merge** and squash merging. If a required human review or
   CODEOWNERS rule applies, bots still wait for that review; this automation
   never supplies approvals or bypasses protection. Configure any intended bot
   exception in the repository's review policy explicitly.
5. Confirm that Dependabot version updates recognize `.github/dependabot.yml`.
   Install the [Mend Renovate GitHub App](https://github.com/apps/renovate) for
   this repository and complete its onboarding if requested. Renovate's
   dashboard and failed CI runs should be included in maintainer notifications.

No personal access token is required for the supplied workflows. The GitHub
token performs release publication and queues eligible Dependabot merges;
Renovate uses its own installed App. No RPM signing key is bundled. Public
distribution with a trusted package signature can be added separately without
changing the unsigned local-test workflow.

## Build and publication events

| Event | Checks | Publication after success |
| --- | --- | --- |
| Pull request to `main` | Native RPM, unit/integration tests, policy fixtures, real GNOME compositor | Downloadable CI artifact only |
| Push to `main` | Same checks, current Fedora 45 packages | Unique pre-release snapshot |
| Weekly Monday 04:23 UTC schedule | Fresh Fedora 45 dependencies and same checks | Unique pre-release snapshot |
| Push `vX.Y.Z` tag | Same checks; tag must match `VERSION` and be on `main` history | Stable release |
| Manual workflow run | Same checks | None by default; `publish=true` publishes only on `main` |
| Merge queue | Same checks | None; normal `main` push publishes after merging |

Build jobs have only read access to repository contents, including on fork PRs.
The separate publication job has `contents: write`, downloads only the successful
artifact from its own run, checks checksums and executes no project source or
binary. Releases contain separate GNOME and KDE Plasma packages for Fedora 45,
Ubuntu/Kubuntu 26.10, and openSUSE Tumbleweed. The release job waits for the
native packages and dependency checks on all three distributions. It publishes
the binary packages, source and debug RPMs, a shared checksum file, and the
Fedora build dependency inventory. Existing release assets
are never replaced. Workflow reruns of an existing stable release leave its
assets intact.

The GNOME smoke test creates separate private session and system buses, temporary
configuration and a software-rendered virtual monitor. Its system bus has no
activatable services: a minimal container may advertise `login1` even though
systemd cannot start it. This keeps the test independent of host services and
lets GNOME handle their absence. A failure blocks publication. Screenshot and
log evidence is kept as CI artifacts for 14 days. Hardware acceptance remains
separate; a green CI run does not prove physical presenter or screen-sharing
behavior.

GitHub schedules run from the default branch, may be delayed and can be disabled
after prolonged inactivity in public repositories. Review the Actions page if a
weekly build stops appearing. No external scheduler is configured here.

## RPM ordering and release procedure

`VERSION` contains the application version, currently `1.1.0`. A local build and
a stable tag build use RPM release `1`. The binary packages are
`projecteur-gnome-1.1.0-1.fc45.x86_64.rpm` and
`projecteur-kde-1.1.0-1.fc45.x86_64.rpm`.

Development builds use release
`0.UTC_TIMESTAMP.RUN_ID.ATTEMPT.gitCOMMIT`, and a matching unique
`snapshot-VERSION-TIMESTAMP-RUN_ID-ATTEMPT` GitHub tag. Within one development
version these builds sort monotonically; the final release `1` sorts above all
its snapshots. Run IDs and attempts also prevent asset-name collisions on reruns.

To publish a stable release, merge the intended changes and hardware test notes,
set `VERSION` and the spec's fallback version to the intended version, then push
a matching `vX.Y.Z` tag on that reviewed commit. The tag workflow builds and
publishes the package automatically after all checks pass.

**Immediately after a stable release, advance `VERSION` and the spec fallback to
the next development patch version on `main`.** For example, after `v1.1.0`,
develop against `1.1.1`. Otherwise weekly `1.1.0-0.…` snapshots intentionally
sort below installed `1.1.0-1`; they are not automatic upgrades of that stable
RPM. Do not force a downgrade merely to follow a nightly build.

The weekly workflow performs `dnf --refresh upgrade` and resolves the spec's
build requirements each time. It therefore finds Fedora dependency regressions
without pretending OS dependencies are a language lockfile. The base image is
pinned by digest; Renovate can update that digest within Fedora 45. Moving to
Fedora 46 or GNOME 52 requires a reviewed compatibility change.

## Division between dependency bots

- **Dependabot:** only the allowed `actions/*` references in GitHub workflows.
  Patch updates are grouped. Minor/major updates receive PRs but do not merge
  automatically.
- **Renovate:** only `registry.fedoraproject.org/fedora:45` container digests.
  Action references are disabled in Renovate, and container references are
  outside Dependabot's allowlist. There are no duplicate updater PRs.

For Dependabot, the trusted `pull_request_target` workflow never checks out PR
code. It checks the same-repository bot origin, signed head commit, exact head
SHA and complete diff. Every changed line must update a SHA-pinned official
action already allowed by policy; the new SHA must resolve from that action's
declared upstream release tag. Only digest changes and patches within the same
major/minor version are eligible. Code changes, permission changes, additional
actions, downgrades and truncated diffs all require manual review.

Eligible PRs are queued with GitHub's native `--auto` mode and a head-SHA guard,
after verifying the strict required check on protected `main`. GitHub then waits
for the current revision's required checks and any review requirements. Renovate
also waits for passing checks and an up-to-date branch before merging image
digest updates (`ignoreTests: false`, `platformAutomerge: false`). The mandatory
**Fedora 45 RPM** check is enforced by the main-branch protection configured
above; the Renovate configuration does not create branch protection.

A merge performed using `GITHUB_TOKEN` can suppress ordinary push workflows.
The Dependabot workflow therefore waits up to roughly 33 minutes for the queued
merge, checks whether a push build exists for its merge commit, and explicitly
dispatches a publishing build on `main` if necessary. Dispatch events are
allowed to trigger workflows with this token. If human review takes longer,
the weekly rebuild remains the fallback; a maintainer can also manually run
**Fedora 45 RPM** with `publish=true`. Renovate App merges normally generate a
push event.

## Local automation checks

```sh
actionlint .github/workflows/*.yml
shellcheck packaging/fedora/build-rpm.sh .github/scripts/*.sh
python3 .github/tests/test-dependabot-policy.py
```

The policy fixtures execute the actual Python gate embedded in the workflow
against mocked GitHub API responses. They test real list-style and named-step
action lines as well as rejected code, permissions, major versions, invalid
SHA/tag pairs and changed/forked/unverified PRs. Renovate and Dependabot configs
should also validate against their current JSON schemas when edited.

Full SHA pins were resolved against the official action release tag references:
checkout `v7.0.1`, upload-artifact `v7.0.2`, download-artifact `v8.0.2`. The
Fedora 45 OCI index digest was retrieved from the official Fedora registry.

## Reference documentation

- [GitHub workflow syntax and permissions](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)
- [GitHub token event and dispatch behavior](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow)
- [GitHub Dependabot automation and required checks](https://docs.github.com/en/code-security/tutorials/secure-your-dependencies/automate-dependabot-with-actions)
- [GitHub CLI release creation](https://cli.github.com/manual/gh_release_create)
- [Renovate automerge requirements](https://docs.renovatebot.com/key-concepts/automerge/)
- [Renovate GitHub Actions/container manager](https://docs.renovatebot.com/modules/manager/github-actions/)
