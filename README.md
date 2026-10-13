# pfSense Cloudflared

pfSense package for Cloudflare Tunnel (`cloudflared`) on pfSense 2.8.x.

This repository builds the official upstream Cloudflare `cloudflared` source
with the BSD compatibility overlay proven by `kjake/cloudflared`, then packages
the resulting binary with pfSense WebGUI and service integration.

## Scope

- Build `cloudflared` for FreeBSD 15 amd64 in GitHub Actions.
- Package `/usr/local/bin/cloudflared` as `pfSense-pkg-cloudflared`.
- Provide a pfSense rc.d service.
- Provide GUI-based token configuration.
- Provide status page and dashboard widget.
- Track official upstream `cloudflare/cloudflared` releases.

## Install

To install download latest pkg in /tmp and run:

```sh
pkg add -f /tmp/pfSense-pkg-cloudflared-*.pkg
```

## WebGUI updates

Services > Cloudflared opens Settings, with Status available as a tab. Both pages
use native pfSense breadcrumbs without clickable package-editor headings.
Status includes an Updater section below Service. It checks
the latest stable release from the public
GitHub repository shown on the page. Refresh saves that URL and checks for a newer
package; Update installs the entire package in the background after confirmation.
The package database version, binary, and WebGUI files are updated together.
Tunnel settings are preserved, and an enabled service is restarted (brief outage).
Update activity appears in the same Recent Log as tunnel runtime events and
respects the configured log level; detailed package output is debug-only.
Displayed log timestamps and Last Checked use the pfSense system timezone.
Cloudflared's raw runtime timestamps remain UTC; the Status page converts them
for display. Updater entries are written with the local timezone offset.

Only use repositories you trust: package installation scripts execute as root.
Downloads require HTTPS, GitHub's asset SHA-256 digest, and matching package name,
version, and FreeBSD ABI. A digest checks integrity, not independent publisher
authenticity. Missing digests or incompatible packages are rejected.
The dedicated Cloudflared Update privilege grants installation access; settings
and status access alone does not. Administrators already have access.

The updater keeps one previous package under `/var/db/cloudflared-update/previous`
for manual recovery with `pkg add -f`. It does not automatically roll back a failed
installation. Update state lives under `/var/db/cloudflared-update`; the shared
runtime/update log is `/var/log/cloudflared.log`.

Package versions and GitHub release names/tags always match the selected upstream
Cloudflared version, for example `2026.10.0`. There are no package-version
overrides, suffixes, or independent version bumps. Scheduled runs skip releases
already built for that upstream version. For integration-only fixes, manually
rerun the workflow with the same upstream tag to rebuild that release; clients
already on that version will not detect it as a newer package automatically.
Existing installations need one initial installation of an updater-enabled
package before subsequent updates can be performed entirely in the WebGUI.
