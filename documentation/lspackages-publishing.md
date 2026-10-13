# Publishing packages to lspackages

The CI pipeline includes `templates/publishing.gitlab-ci.yml` from
`lightspeedrtx/lightspeedvault` at `main`. Merge the shared templates into that
branch before running or merging this consumer configuration. Shared updates
apply to newly created pipelines. The shared job supplies workspace cleanup; the
local wrapper supplies package names, versions, prepared directories and the
explicit remote. The installed uploader packages the directory contents into a
temporary `.7z` and uploads it. There is no downloaded helper or temporary
Packman publishing XML.

| Package selection | Remote | Packages |
| --- | --- | --- |
| `internal` | `rtx-remix-internal` | `rtx-remix-hdremix`, `rtx-remix-omni_core_materials` |
| `public` | `rtx-remix-external` | Same package names |

The existing automatic rules still publish release HDRemix and MDL packages from
main/release branches externally and private branches internally. Package content,
`int-`/`ext-` version prefixes, branch suffixes, and HDRemix flavor suffixes are
preserved. Public packages use the external bucket directly; they do not need the
old CloudFront bucket's `public=true` tag. Existing dependency downloads remain
configured, and signing keeps its own Vault integration. Package lookup tries
`rtx-remix-internal`, then `rtx-remix-external`, then `cloudfront` and `gtl`.
An internal package takes precedence if the same name and version exists in both
Remix buckets. Select `-r rtx-remix-external` explicitly when the external copy
is required.

The XML files in `scripts-common/packman/` match [DXVK MR !2334](https://gitlab-master.nvidia.com/lightspeedrtx/dxvk-remix-nv/-/merge_requests/2334).
`config.packman.xml` defines HTTPS download/list and S3 upload for both Remix
remotes, HTTPS download only for CloudFront, and download/list/upload for GTL.
Neither XML config has `get-pass` or `set-pass` transports.

`external_config.packman.xml` is the public mirror replacement: external Remix
then legacy CloudFront, with HTTPS downloads only. The `update_oss_repo` job substitutes this public configuration at
`config.packman.xml` starting with the commit that introduces it. Older exported
commits keep their original configuration and commit IDs, so existing mirrors
continue to advance by ordinary fast-forward pushes. Later internal-config edits
keep the public copy selected; no force push or post-export commit is added.
The callback is covered by an isolated Git export test with two successive pushes.

Runtime ZIPs, symbols, checksums and GitHub draft releases are prepared by
[remix-release](https://gitlab-master.nvidia.com/dpt/devops/remix-release) using
its `RELEASE_VERSION` input. DXVK CI provides the builds, signing trigger, source
export, and HDRemix/core-materials publishing jobs.

## Manual publishing

Create a pipeline in the GitLab web UI. The pipeline user needs read access to the
shared include. Leave `MANUAL_SIGNING_PIPELINE` at `false` for normal package
publishing.

- For HDRemix, select `HDREMIX_PACKAGE_PUBLISH=internal` or `public` and
  `HDREMIX_PUBLISH_FLAVOR=release`, `debug`, or `debug_optimized`. The selected
  build and publisher run automatically. Release publishing downloads both x64
  and ARM64EC build artifacts, matching the automatic release composition.
- For materials, select `MDL_PACKAGE_PUBLISH=internal` or `public`. Set an unused
  package selector to `do_not_upload`.
- Each upload requires a new version. Use `HDREMIX_VERSION` or `MDL_VERSION` when
  publishing another version of the same source. No test suffix is added and no
  overwrite is requested. Normal symbol publishing remains enabled.

HDRemix and MDL package uploads use `win_build` runners. Shared job templates
and manual publishing rules live in `tools/ci/gitlab/.extends.yml`.
The Windows runners need `lightspeed-uploader.exe` on PATH and an inherited
`LSPACKAGES_TOKEN` with Upload access to the selected remote, or an approved
source-IP grant in LSPackages. No Vault S3 credentials or new GitLab variables are
needed on an already configured runner. Confirm token expiry, bucket/IP scope,
and runner service/session inheritance. Unrelated signing retains its own Vault
integration. Group-runner access is automatic within `lightspeedrtx`; contact
@lightspeeddevops in Slack's #dpt-devops for uploader authorization or projects
outside that group. The feature branch uses the `kupusama` remote for the fork MR.

## Dependency download routing

All nine tracked package manifests were audited. The root x64/ARM64 manifests,
HdRemix, USD plugins (x64/ARM64), HydraTestRender, and the test-kit downloader use
the shared remote order above. The test-kit script does not force the legacy
CloudFront remote, so later `rtx_remix` versions can come from either Remix bucket.
Package versions remain pinned.

The two APIC manifests contain 59 private capture-package references. Their update
scripts intentionally select `gtl`; those packages are not published by this
migration. The ARM64 build also retains its `NVM_GTLAPI_TOKEN` prerequisite.

Packman 7.26, pinned by this repository, treats HTTP 401/403/404 during package
lookup as a miss and continues to the next configured remote. Exhausted network
errors or HTTP 500 retries stop the lookup; the remote order is not an offline
fallback guarantee. GTL package availability and actual downloads require the
appropriate developer/runner access.
