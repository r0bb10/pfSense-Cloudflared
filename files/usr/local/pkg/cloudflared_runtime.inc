<?php

function cloudflared_binary_version() {
	$binary = '/usr/local/bin/cloudflared';
	if (!is_executable($binary)) {
		return 'Cloudflared binary is missing';
	}
	$output = [];
	$result = 0;
	exec($binary . ' version 2>&1', $output, $result);
	return $result === 0 ? trim(implode("\n", $output)) : 'Cloudflared version unavailable';
}

function cloudflared_tabs($active) {
	$tabs = [
		[gettext('Settings'), $active === 'settings', '/cloudflared_settings.php'],
		[gettext('Status'), $active === 'status', '/status_cloudflared.php'],
	];
	display_top_tabs($tabs);
}

function cloudflared_log_timezone() {
	try {
		return new DateTimeZone(config_get_path('system/timezone', date_default_timezone_get()));
	} catch (Exception $error) {
		return new DateTimeZone(date_default_timezone_get());
	}
}

function cloudflared_log_time($timestamp) {
	return (new DateTimeImmutable('@' . (int)$timestamp))->setTimezone(cloudflared_log_timezone())->format('Y-m-d H:i:s T');
}

function cloudflared_redact($text) {
	$token = cloudflared_settings()['token'];
	if ($token !== '') {
		$text = str_replace($token, '[REDACTED]', $text);
	}
	$text = preg_replace('/(--token(?:=|\s+))\S+/i', '$1[REDACTED]', (string)$text);
	$text = preg_replace('/((?:token|secret|credentials-contents)["\x27]?\s*[:=]\s*["\x27]?)\S+/i', '$1[REDACTED]', $text);
	return preg_replace('/[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}/', '[REDACTED-JWT]', $text);
}

function cloudflared_recent_log($path = CLOUDFLARED_LOG_FILE) {
	$file = @fopen($path, 'rb');
	if ($file === false) {
		return '';
	}
	try {
		$offset = max(0, fstat($file)['size'] - 65536);
		fseek($file, $offset);
		if ($offset > 0) {
			fgets($file);
		}
		$text = stream_get_contents($file);
	} finally {
		fclose($file);
	}
	$lines = array_slice(explode("\n", rtrim($text)), -60);
	$timezone = cloudflared_log_timezone();
	foreach ($lines as &$line) {
		$line = preg_replace_callback('/^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2}))/', function ($match) use ($timezone) {
			try {
				return (new DateTimeImmutable($match[1]))->setTimezone($timezone)->format('Y-m-d H:i:s');
			} catch (Exception $error) {
				return $match[1];
			}
		}, $line);
	}
	return cloudflared_redact(implode("\n", $lines));
}

function cloudflared_connection_count($metrics) {
	if (preg_match('/^cloudflared_tunnel_ha_connections(?:\{[^\n]*\})?\s+(\d+)(?:\.0+)?\s*$/m', $metrics, $match)) {
		return (int)$match[1];
	}
	return null;
}

function cloudflared_runtime_status() {
	$running = (bool)is_service_running('cloudflared');
	$status = ['running' => $running, 'service' => $running ? 'Running' : (cloudflared_enabled() ? 'Stopped' : 'Disabled'),
		'connection' => $running ? 'Unknown — local metrics unavailable' : 'Not connected'];
	if (!$running) {
		return $status;
	}
	$address = cloudflared_settings()['metrics'];
	$addresses = $address === '' ? array_map(fn($port) => '127.0.0.1:' . $port, range(20241, 20245)) : [$address];
	foreach ($addresses as $address) {
		if (!preg_match('/^(?:127\.0\.0\.1|\[::1\]|localhost):([0-9]{1,5})$/D', $address, $match) || (int)$match[1] > 65535 || (int)$match[1] < 1) {
			continue;
		}
		$curl = curl_init('http://' . $address . '/metrics');
		$body = '';
		curl_setopt_array($curl, [CURLOPT_PROXY => '', CURLOPT_CONNECTTIMEOUT_MS => 150, CURLOPT_TIMEOUT_MS => 250,
			CURLOPT_FOLLOWLOCATION => false, CURLOPT_WRITEFUNCTION => function ($curl, $chunk) use (&$body) {
				if (strlen($body) + strlen($chunk) > 1048576) {
					return 0;
				}
				$body .= $chunk;
				return strlen($chunk);
			}]);
		$ok = curl_exec($curl);
		$code = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
		curl_close($curl);
		$count = $ok && $code === 200 ? cloudflared_connection_count($body) : null;
		if ($count !== null) {
			$status['connection'] = $count > 0 ? $count . ' active edge connection' . ($count === 1 ? '' : 's') : 'No active edge connections';
			break;
		}
	}
	return $status;
}
