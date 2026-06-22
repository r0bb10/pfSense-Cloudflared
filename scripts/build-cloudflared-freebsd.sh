#!/bin/sh

set -eu

VERSION="${1:?usage: $0 <cloudflared-version>}"
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
WORK="${ROOT}/work"
SRC="${WORK}/cloudflared-src"

rm -rf "${SRC}"
mkdir -p "${WORK}" "${ROOT}/dist"

git clone --depth 1 --branch "${VERSION}" https://github.com/cloudflare/cloudflared.git "${SRC}"
cd "${SRC}"

git remote add kjake https://github.com/kjake/cloudflared.git
git fetch --depth 1 kjake customizations

git checkout FETCH_HEAD -- \
	cmd/cloudflared/generic_service.go \
	diagnostic/network/collector_unix.go \
	diagnostic/network/collector_unix_test.go \
	diagnostic/system_collector_unix.go \
	ingress/icmp_posix.go \
	ingress/icmp_posix_test.go \
	Makefile \
	patches

for patch in patches/*.patch; do
	git apply --3way "${patch}"
done

export GOEXPERIMENT="${GOEXPERIMENT:-noboringcrypto}"
export CGO_ENABLED="${CGO_ENABLED:-0}"
export GOTOOLCHAIN="${GOTOOLCHAIN:-auto}"

go mod download
gmake cloudflared
install -m 0755 cloudflared "${ROOT}/dist/cloudflared"
"${ROOT}/dist/cloudflared" version
