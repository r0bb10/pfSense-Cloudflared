<?php

require_once('functions.inc');
require_once('globals.inc');
require_once('service-utils.inc');
require_once('util.inc');

const CLOUDFLARED_CONFIG_DIR = '/usr/local/etc/cloudflared';
const CLOUDFLARED_TOKEN_FILE = CLOUDFLARED_CONFIG_DIR . '/token';
const CLOUDFLARED_RCCONF = '/usr/local/etc/rc.conf.d/cloudflared';
const CLOUDFLARED_LOG_FILE = '/var/log/cloudflared.log';

function cloudflared_settings() {
	return config_get_path('installedpackages/cloudflared/config/0', []);
}

function cloudflared_enabled() {
	$settings = cloudflared_settings();
	return (($settings['enable'] ?? '') === 'on');
}

function cloudflared_quote_rc_value($value) {
	return str_replace(['\\', '"', '$', '`'], ['\\\\', '\\"', '\\$', '\\`'], (string)$value);
}

function cloudflared_validate(&$post, &$input_errors) {
	$enabled = (($post['enable'] ?? '') === 'on');
	$token = trim((string)($post['token'] ?? ''));
	if ($enabled && $token === '') {
		$input_errors[] = gettext('Tunnel token is required when Cloudflared is enabled.');
	}
	if ($token !== '') {
		$decoded = base64_decode($token, true);
		if ($decoded === false || json_decode($decoded, true) === null) {
			$input_errors[] = gettext('Tunnel token must be a valid Cloudflare tunnel token.');
		}
	}

	$protocol = $post['protocol'] ?? 'auto';
	if (!in_array($protocol, ['auto', 'quic', 'http2'], true)) {
		$input_errors[] = gettext('Protocol must be Auto, QUIC, or HTTP/2.');
	}

	$loglevel = $post['loglevel'] ?? 'info';
	if (!in_array($loglevel, ['error', 'warn', 'info', 'debug'], true)) {
		$input_errors[] = gettext('Log level must be Error, Warning, Info, or Debug.');
	}

	$metrics = trim((string)($post['metrics'] ?? ''));
	if ($metrics !== '' && !preg_match('/^[A-Za-z0-9_.:-]+:[0-9]{1,5}$/', $metrics)) {
		$input_errors[] = gettext('Metrics address must look like 127.0.0.1:60123.');
	}
	if ($metrics !== '') {
		$port = (int)substr(strrchr($metrics, ':'), 1);
		if ($port < 1 || $port > 65535) {
			$input_errors[] = gettext('Metrics port must be between 1 and 65535.');
		}
	}

	$extra_args = trim((string)($post['extra_args'] ?? ''));
	if ($extra_args !== '' && preg_match('/[;&|`$<>\\]/', $extra_args)) {
		$input_errors[] = gettext('Extra arguments contain unsupported shell metacharacters.');
	}
}

function cloudflared_write_files() {
	$settings = cloudflared_settings();
	if (!is_dir(CLOUDFLARED_CONFIG_DIR)) {
		mkdir(CLOUDFLARED_CONFIG_DIR, 0755, true);
	}

	$token = trim((string)($settings['token'] ?? ''));
	if ($token !== '') {
		$tmp = CLOUDFLARED_TOKEN_FILE . '.tmp';
		file_put_contents($tmp, $token . "\n", LOCK_EX);
		chmod($tmp, 0600);
		rename($tmp, CLOUDFLARED_TOKEN_FILE);
	} else {
		@unlink(CLOUDFLARED_TOKEN_FILE);
	}

	if (!is_dir(dirname(CLOUDFLARED_RCCONF))) {
		mkdir(dirname(CLOUDFLARED_RCCONF), 0755, true);
	}
	$enabled = cloudflared_enabled() ? 'YES' : 'NO';
	$protocol = $settings['protocol'] ?? 'auto';
	$metrics = trim((string)($settings['metrics'] ?? ''));
	$loglevel = $settings['loglevel'] ?? 'info';
	$quic_disable_pmtu = (($settings['quic_disable_pmtu'] ?? '') === 'on') ? 'YES' : 'NO';
	$extra_args = trim((string)($settings['extra_args'] ?? ''));
	$rcconf = '';
	$rcconf .= "cloudflared_enable=\"{$enabled}\"\n";
	$rcconf .= 'cloudflared_token_file="' . CLOUDFLARED_TOKEN_FILE . "\"\n";
	$rcconf .= 'cloudflared_protocol="' . cloudflared_quote_rc_value($protocol) . "\"\n";
	$rcconf .= 'cloudflared_metrics="' . cloudflared_quote_rc_value($metrics) . "\"\n";
	$rcconf .= 'cloudflared_loglevel="' . cloudflared_quote_rc_value($loglevel) . "\"\n";
	$rcconf .= "cloudflared_quic_disable_pmtu=\"{$quic_disable_pmtu}\"\n";
	$rcconf .= 'cloudflared_extra_args="' . cloudflared_quote_rc_value($extra_args) . "\"\n";
	$rcconf .= 'cloudflared_logfile="' . CLOUDFLARED_LOG_FILE . "\"\n";
	file_put_contents(CLOUDFLARED_RCCONF, $rcconf, LOCK_EX);
}

function cloudflared_sync() {
	cloudflared_write_files();
	if (cloudflared_enabled()) {
		if (is_service_running('cloudflared')) {
			restart_service('cloudflared');
		} else {
			start_service('cloudflared');
		}
	} elseif (is_service_running('cloudflared')) {
		stop_service('cloudflared');
	}
}

function cloudflared_install() {
	cloudflared_write_files();
}

function cloudflared_deinstall() {
	if (is_service_running('cloudflared')) {
		stop_service('cloudflared');
	}
	@unlink(CLOUDFLARED_TOKEN_FILE);
	@unlink(CLOUDFLARED_RCCONF);
}

?>
