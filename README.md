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