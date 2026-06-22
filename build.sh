#!/bin/sh

set -eu

PORTNAME="pfSense-pkg-cloudflared"
PORTVERSION="${PORTVERSION:-0.0.0}"
ABI="FreeBSD:15:amd64"
PREFIX="/usr/local"
ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
FILES="${ROOT}/files"
BUILD="${ROOT}/build"
STAGE="${BUILD}/stage"
OUTPUT="${BUILD}/pkg"
BINARY="${ROOT}/dist/cloudflared"

clean() {
	rm -rf "${BUILD}"
}

json_escape() {
	awk '{ gsub(/\\/, "\\\\"); gsub(/\t/, "\\t"); gsub(/"/, "\\\""); printf "%s\\n", $0 }'
}

stage() {
	if [ ! -x "${BINARY}" ]; then
		echo "Missing FreeBSD/amd64 binary: ${BINARY}" >&2
		echo "Run the GitHub FreeBSD build workflow or place cloudflared at dist/cloudflared." >&2
		exit 1
	fi

	rm -rf "${STAGE}"
	mkdir -p \
		"${STAGE}${PREFIX}/bin" \
		"${STAGE}${PREFIX}/etc/rc.d" \
		"${STAGE}${PREFIX}/pkg" \
		"${STAGE}${PREFIX}/www" \
		"${STAGE}${PREFIX}/www/widgets/widgets" \
		"${STAGE}${PREFIX}/share/${PORTNAME}" \
		"${STAGE}/etc/inc/priv"

	install -m 0755 "${BINARY}" "${STAGE}${PREFIX}/bin/cloudflared"
	install -m 0555 "${FILES}${PREFIX}/etc/rc.d/cloudflared" "${STAGE}${PREFIX}/etc/rc.d/cloudflared"
	install -m 0644 "${FILES}${PREFIX}/pkg/cloudflared.xml" "${STAGE}${PREFIX}/pkg/cloudflared.xml"
	install -m 0644 "${FILES}${PREFIX}/pkg/cloudflared.inc" "${STAGE}${PREFIX}/pkg/cloudflared.inc"
	install -m 0644 "${FILES}${PREFIX}/www/status_cloudflared.php" "${STAGE}${PREFIX}/www/status_cloudflared.php"
	install -m 0644 "${FILES}${PREFIX}/www/widgets/widgets/cloudflared.widget.php" "${STAGE}${PREFIX}/www/widgets/widgets/cloudflared.widget.php"
	install -m 0644 "${FILES}${PREFIX}/share/${PORTNAME}/info.xml" "${STAGE}${PREFIX}/share/${PORTNAME}/info.xml"
	install -m 0644 "${FILES}/etc/inc/priv/cloudflared.priv.inc" "${STAGE}/etc/inc/priv/cloudflared.priv.inc"

	for file in \
		"${STAGE}${PREFIX}/pkg/cloudflared.xml" \
		"${STAGE}${PREFIX}/share/${PORTNAME}/info.xml"; do
		sed "s/%%PKGVERSION%%/${PORTVERSION}/g" "${file}" > "${file}.tmp"
		mv "${file}.tmp" "${file}"
	done
}

manifest() {
	post_install_script=$(sed "s/%%PORTNAME%%/${PORTNAME}/g" "${FILES}/pkg-install.in" | json_escape)
	pre_deinstall_script=$(sed "s/%%PORTNAME%%/${PORTNAME}/g" "${FILES}/pkg-deinstall.in" | json_escape)

	cat > "${BUILD}/+MANIFEST" <<EOF
name: "${PORTNAME}"
version: "${PORTVERSION}"
origin: "net/${PORTNAME}"
comment: "Cloudflare Tunnel daemon for pfSense"
maintainer: "noreply@github.com"
prefix: "${PREFIX}"
abi: "${ABI}"
desc: "Cloudflare Tunnel daemon package for pfSense with WebGUI configuration and service integration."
www: "https://github.com/r0bb10/pfSense-Cloudflared"
licenselogic: "single"
licenses: ["APACHE20"]
categories: ["net"]
scripts: {
  post-install: "${post_install_script}",
  pre-deinstall: "${pre_deinstall_script}"
}
EOF

	sed "s|%%DATADIR%%|share/${PORTNAME}|g" "${ROOT}/pkg-plist" > "${BUILD}/plist"
}

package() {
	stage
	manifest
	mkdir -p "${OUTPUT}"
	pkg create -M "${BUILD}/+MANIFEST" -p "${BUILD}/plist" -r "${STAGE}" -o "${OUTPUT}"
	find "${OUTPUT}" -maxdepth 1 -type f -print
}

case "${1:-package}" in
	clean) clean ;;
	stage) stage ;;
	package) package ;;
	*) echo "Usage: $0 [package|stage|clean]" >&2; exit 2 ;;
esac
