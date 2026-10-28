<?php

require_once('functions.inc');
require_once('globals.inc');
require_once('service-utils.inc');
require_once('util.inc');

const CLOUDFLARED_CONFIG_DIR = '/usr/local/etc/cloudflared';
const CLOUDFLARED_TOKEN_FILE = CLOUDFLARED_CONFIG_DIR . '/token';
const CLOUDFLARED_RCCONF = '/usr/local/etc/rc.conf.d/cloudflared';
const CLOUDFLARED_LOG_FILE = '/var/log/cloudflared.log';

function cloudflared_defaults() {
	return ['enable' => '', 'token' => '', 'protocol' => 'auto', 'metrics' => '',
		'loglevel' => 'info', 'quic_disable_pmtu' => '', 'extra_args' => ''];
}

function cloudflared_settings() {
	return array_merge(cloudflared_defaults(), config_get_path('installedpackages/cloudflared/config/0', []));
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
		$credentials = $decoded === false ? null : json_decode($decoded, true);
		if (!is_array($credentials) || !$credentials) {
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
	if ($metrics !== '' && !preg_match('/^(?:[A-Za-z0-9_.-]+|\[[a-fA-F0-9:]+\]):[0-9]{1,5}$/D', $metrics)) {
		$input_errors[] = gettext('Metrics address must look like 127.0.0.1:60123.');
	}
	if ($metrics !== '') {
		$port = (int)substr(strrchr($metrics, ':'), 1);
		if ($port < 1 || $port > 65535) {
			$input_errors[] = gettext('Metrics port must be between 1 and 65535.');
		}
	}

	$extra_args = trim((string)($post['extra_args'] ?? ''));
	if ($extra_args !== '' && !preg_match('#^[A-Za-z0-9_./:=,@%+\[\] -]+$#D', $extra_args)) {
		$input_errors[] = gettext('Extra arguments must be plain space-separated arguments without quotes or shell metacharacters.');
	}
	if (preg_match('/(?:^| )--(?:token(?:-file)?|credentials-(?:file|contents)|config|logfile|loglevel|metrics|protocol|no-autoupdate|quic-disable-pmtu-discovery)(?:[= ]|$)/', $extra_args)) {
		$input_errors[] = gettext('Use the dedicated settings for credentials, logging, metrics and transport, not Extra Arguments.');
	}
}

function cloudflared_atomic_write($file, $contents, $mode = 0600) {
	$directory = dirname($file);
	if (!is_dir($directory) && !mkdir($directory, 0755, true)) {
		throw new RuntimeException('Cannot create Cloudflared configuration directory.');
	}
	$tmp = tempnam($directory, '.cloudflared-');
	if ($tmp === false) {
		throw new RuntimeException('Cannot create temporary configuration file.');
	}
	try {
		if (!chmod($tmp, $mode) || file_put_contents($tmp, $contents, LOCK_EX) !== strlen($contents) || !rename($tmp, $file)) {
			throw new RuntimeException('Cannot write Cloudflared configuration file: ' . $file);
		}
	} finally {
		if (is_file($tmp)) {
			unlink($tmp);
		}
	}
}

function cloudflared_write_files() {
	$settings = cloudflared_settings();
	$errors = [];
	cloudflared_validate($settings, $errors);
	if ($errors) {
		throw new RuntimeException(implode(' ', $errors));
	}

	$token = trim((string)($settings['token'] ?? ''));
	if ($token !== '') {
		cloudflared_atomic_write(CLOUDFLARED_TOKEN_FILE, $token . "\n");
	} else {
		if (is_file(CLOUDFLARED_TOKEN_FILE) && !unlink(CLOUDFLARED_TOKEN_FILE)) {
			throw new RuntimeException('Cannot remove the old tunnel token.');
		}
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
	cloudflared_atomic_write(CLOUDFLARED_RCCONF, $rcconf);
	// Rotate logs through the supervisor.
	cloudflared_atomic_write('/var/etc/newsyslog.conf.d/cloudflared.conf',
		CLOUDFLARED_LOG_FILE . " 600 3 1024 * JC /var/run/cloudflared-daemon.pid 1\n", 0644);
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
	$deadline = microtime(true) + 15;
	while (is_service_running('cloudflared') !== cloudflared_enabled() && microtime(true) < $deadline) {
		usleep(250000);
	}
	if (is_service_running('cloudflared') !== cloudflared_enabled()) {
		throw new RuntimeException('Settings saved, but the service did not reach the requested state. Inspect the tunnel log.');
	}
}

function cloudflared_install() {
	cloudflared_sync_menu();
	cloudflared_write_files();
}

function cloudflared_sync_menu() {
	$menus = config_get_path('installedpackages/menu', []);
	foreach ($menus as $key => &$menu) {
		if (($menu['configfile'] ?? '') !== 'cloudflared.xml') {
			continue;
		}
		if (($menu['section'] ?? '') === 'Status') {
			unset($menus[$key]);
		} elseif (($menu['section'] ?? '') === 'Services') {
			$menu['url'] = '/cloudflared_settings.php';
		}
	}
	unset($menu);
	config_set_path('installedpackages/menu', array_values($menus));
}

function cloudflared_deinstall() {
	if (is_service_running('cloudflared')) {
		stop_service('cloudflared');
	}
	@unlink(CLOUDFLARED_TOKEN_FILE);
	@unlink(CLOUDFLARED_RCCONF);
	@unlink('/var/etc/newsyslog.conf.d/cloudflared.conf');
}

require_once(__DIR__ . '/cloudflared_runtime.inc');
