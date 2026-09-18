# Wiki Log

Chronological record of wiki activity.

## [2026-09-18] ingest | Initial bulk import

Ingested 27 vulnerabilities from 4 sources:
- 13 bugs from student researcher sbingqua (bug-001 through bug-016, excluding false positives)
- 7 bugs from student researcher waynelow (all with PoC code)
- 3 bugs from student researchers tchinhe1 and syn_zheng_yi (fuzzer crashes + DHCP null deref)
- 9 bugs from GitHub issues and PRs (GH-880, GH-1789, GH-2711, GH-2832, GH-2852, GH-3083, PR-2939, PR-3741, PR-3756)
- 2 OSS-Fuzz findings (OSV-2026-196, OSV-2026-215) noted but excluded as likely false positives per PR #3579

Created:
- 27 vulnerability pages in `vulns/`
- `index.md` — master catalog by component and CWE
- `overview.md` — synthesis with statistics and attack surface analysis
- `concepts/attack-surface.md` — USB attack vectors
- `concepts/cwes.md` — CWE taxonomy of findings
- `concepts/exploit-patterns.md` — common exploitation patterns

Deduplication notes:
- WAYNELOW-5 and PR-3741 describe the same NCM NDP16 bug (merged into one page)
- WAYNELOW-7 and PR-3756 describe the same RNDIS integer overflow (merged into one page)
- BUG-015 and WAYNELOW-4 both concern hub.c array indexing (separate pages, cross-referenced)
