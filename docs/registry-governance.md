# Rexy Package Registry — Governance

**Status:** v1 (gates Phase C of the rxy package manager)
**Companion doc:** `docs/prd-package-manager.md`
**Last updated:** 2026-04-30

This document defines how the public Rexy package registry is operated. Phase C of `rxy` ships with the *machinery* to publish, yank, and resolve from a registry. This document is the *policy* layer that gates opening write access to the public index repository.

`rxy` Phase C is shippable as a binary without this document — but the public registry **MUST NOT** accept publishes from outside the core team until this document is ratified and enforced. (See PRD R-12.)

---

## 1. Naming and normalization

- **Allowed characters:** `[a-z0-9_-]`. Must start with a letter.
- **Length:** 1–64 characters.
- **Case folding:** package names are case-insensitive. `Foo` and `foo` are the same package.
- **Underscore/hyphen equivalence:** `my_pkg` and `my-pkg` are the same package for uniqueness purposes. The canonical form uses hyphens.
- **Reserved prefixes:** the following prefixes may only be claimed by the Rexy core team (`@rexy-lang`):
  - `rexy-`
  - `rxy-`
  - `std-`
  - `core-`
  - `alloc-`
- **Reserved exact names:** the registry ships a list of high-value common names (`json`, `http`, `tls`, `regex`, `net`, `time`, `crypto`, ...) that require a manual claim with proven authorial activity (a public package authored elsewhere with comparable scope). The full list lives at `<index-root>/reserved.toml`.

Implementation: the registry index repo's pre-receive hook validates these rules on every push.

## 2. Ownership

- **First-come-first-served** for non-reserved names.
- **One owner per namespace at registration time.** Co-owners can be added by the original owner.
- **Identity:** ownership is keyed to a verified email address. v1 uses GitHub identity (the user's GitHub login + an email verified by GitHub). Future phases may add Sigstore identities.
- **Inactive-namespace release:**
  - 24-month wall-clock since the last publish.
  - 30-day notice window via the registered contact email.
  - At end of window, namespace is released for re-registration.
- **Voluntary transfer:** `rxy transfer <pkg> <new-owner>` (P2 command), recorded in the index audit log.
- **Forced transfer:** only on legal demand (DMCA / UDRP / court order). Documented case-by-case.

## 3. Squatting and typosquatting

- A namespace claim that publishes no version within 7 days is auto-released.
- A package whose latest version is empty (no source) is treated as a placeholder and is removed.
- Typosquatting (visually-similar names targeting popular packages) is grounds for immediate de-registration. Reports go to `abuse@rexy-lang.dev`.

## 4. Yanking

- **Who can yank:** package owners and registry admins.
- **What yanking does:** flips `yanked = true` on a specific version's index entry. The version's source remains in the registry's tree; existing lockfiles continue to work; new resolutions skip the version.
- **Severity field** (`yank-severity`):
  - `informational` (default): build-time *warning*; `rxy update` errors.
  - `security`: build-time *error* unless `--allow-yanked` is passed; `rxy update` errors.
- **Reversibility:** `rxy unyank` restores the entry. The audit log records both directions.
- **Reason field** is encouraged. Required for security yanks.

## 5. Immutability

- **Versions are forever.** Once `(name, version)` is published, the entry's `commit` and `checksum` are locked. The only mutable field is `yanked` (and its severity/reason).
- **No version deletion**, only yank. This prevents the npm `left-pad` failure mode.
- **No version reassignment.** Re-publishing the same version with a different commit is rejected by the index hook.

## 6. Malware and illegal content

- **Reports go to** `abuse@rexy-lang.dev`. Acknowledgement within 48 business hours.
- **Initial response:** the registry admin team yanks the affected version with `severity = security` while investigating.
- **Confirmed malicious package:** all versions are yanked, the namespace is suspended, the publisher's identity is banned from new publishes pending appeal.
- **DMCA:** standard counter-notice procedure. Address: TBD before public launch.
- **CSAM and other illegal content:** removed immediately, reported to the relevant authority.
- **PII reports:** removed within 24 hours; if the version is widely depended on, a yank with severity `security` is issued in tandem.

## 7. Account security

- **2FA mandatory** for all publishers. v1's "you can push to the index repo, you can publish" model relies on the underlying git host (e.g., GitHub) enforcing 2FA on the user's account. Branch protection on `main` enforces signed commits.
- **Compromised account:** the registry admins reset the account's credentials and audit-log every publish made under it during the suspected window. Publishers should report compromises to `security@rexy-lang.dev`.
- **API tokens (Phase D)** will be scoped (`publish-only`, `yank-only`, `admin`).

## 8. Authentication model (v1)

- The index is a git repository.
- **Write access** to the index repo's `main` branch IS the publish capability.
- Branch protection enforces:
  - Signed commits required
  - `pre-receive` hook validates: namespace ownership, semver monotonicity, no edit to existing entries except yank flag, reserved-prefix policy
  - Force-push disallowed
- A "Submitter Bot" (deferred to Phase D) will accept signed publish payloads over HTTPS and serialize commits to avoid push contention at scale.

## 9. Disputes

- **Registry Council:** 3 community members, 1-year terms, public elections.
- **Resolution SLA:** 30 days from the receipt of a complete dispute.
- **Appeal:** disputes around namespace ownership, transfers, or take-downs go to the Council. Council decisions are public and posted to `<index-root>/disputes/`.

## 10. Audit log

The registry maintains an append-only audit log at `<index-root>/audit.log`, one line per state change:
```
<RFC3339> <action> <name> [<version>] [<actor>] [<note>]
2026-04-30T03:14:15Z publish util 0.4.0 alice@example.com "..."
2026-04-30T04:20:00Z yank    util 0.4.0 alice@example.com "CVE-2026-12345"
2026-04-30T04:25:00Z unyank  util 0.4.0 alice@example.com "wrong yank"
```

This log is replicated to the public-archive mirror weekly.

## 11. What we are NOT doing in v1

- **No API server.** Index reads happen through `git pull`. Phase D may add an HTTP API.
- **No mirroring.** A mirror network is Phase E+.
- **No paid / private registries** as a service. Self-hosted private registries are supported via `[registries.<name>]` config.
- **No build provenance / SLSA attestation.** Phase E.
- **No deletion-on-request.** Yanking only.

## 12. Lessons we are explicitly avoiding

- **npm's left-pad incident (2016):** unrestricted unpublish broke the ecosystem. We enforce yank-only.
- **Cargo's land-grabbing (2014–2016):** pure global namespace + first-come-first-served encouraged squatting. We add reserved-prefix and reserved-exact-name lists.
- **PyPI's 2FA gap (pre-2023):** account takeovers led to malicious uploads. We mandate 2FA from day one.
- **RubyGems' rubygems-update vulnerability (2020):** insufficient signature verification. We require signed commits.

## 13. Amendments

This document is amended by Council vote (3-of-3 unanimity for v1; 2-of-3 majority once five amendments have shipped). Amendment proposals are pull requests against this file, with a 14-day public comment period before vote.

---

*This document is a v1 baseline. The registry binary may ship with broader capability than the policy permits — the policy is enforced by index-repo branch protection + the pre-receive hook.*
