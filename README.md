# pfSense Cloudflared

pfSense package for Cloudflare Tunnel (`cloudflared`) on pfSense 2.8.x / FreeBSD 15 amd64.

This repository builds the official upstream Cloudflare `cloudflared` source with the BSD compatibility overlay proven by `kjake/cloudflared`, then packages the resulting binary with pfSense WebGUI and service integration.

## Scope

- Build `cloudflared` for FreeBSD 15 amd64 in GitHub Actions.
- Package `/usr/local/bin/cloudflared` as `pfSense-pkg-cloudflared`.
- Provide a pfSense rc.d service.
- Provide GUI-based token configuration.
- Provide status page and dashboard widget.
- Track official upstream `cloudflare/cloudflared` releases.

## Local Package Build

The package build expects a FreeBSD/amd64 `cloudflared` binary at `dist/cloudflared`.

```sh
./build.sh package
```

The GitHub workflow performs the full upstream source build and package creation on FreeBSD 15.
