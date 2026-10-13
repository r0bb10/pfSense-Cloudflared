<?php

const CLOUDFLARED_UPDATE_DIR = '/var/db/cloudflared-update';
const CLOUDFLARED_UPDATE_REPOSITORY = 'https://github.com/r0bb10/pfSense-Cloudflared';
const CLOUDFLARED_PACKAGE_NAME = 'pfSense-pkg-cloudflared';
const CLOUDFLARED_PKG = '/usr/local/sbin/pkg';

function cloudflared_update_repository($url) {
	$url = rtrim(trim((string)$url), '/');
	if (!preg_match('#^https://github\.com/([A-Za-z0-9-]+)/([A-Za-z0-9_.-]+)$#D', $url, $match) || in_array($match[2], ['.', '..'], true)) {
		throw new RuntimeException('Use a public GitHub repository URL: https://github.com/owner/repository');
	}
	return $url;
}

function cloudflared_update_directory() {
	if (!is_dir(CLOUDFLARED_UPDATE_DIR) && !mkdir(CLOUDFLARED_UPDATE_DIR, 0700, true)) {
		throw new RuntimeException('Cannot create update directory.');
	}
}

function cloudflared_update_state($state = null) {
	$file = CLOUDFLARED_UPDATE_DIR . '/state.json';
	if ($state === null) {
		return is_file($file) ? (json_decode(file_get_contents($file), true) ?: []) : [];
	}
	$state['time'] = time();
	if (file_put_contents($file . '.tmp', json_encode($state), LOCK_EX) === false || !rename($file . '.tmp', $file)) {
		throw new RuntimeException('Cannot save update status.');
	}
	return $state;
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

function cloudflared_update_log($text, $level = 'info') {
	$levels = ['error' => 0, 'warn' => 1, 'info' => 2, 'debug' => 3];
	$threshold = $levels[cloudflared_settings()['loglevel'] ?? 'info'] ?? $levels['info'];
	if (!isset($levels[$level]) || $levels[$level] > $threshold) {
		return;
	}
	$labels = ['error' => 'ERR', 'warn' => 'WRN', 'info' => 'INF', 'debug' => 'DBG'];
	$timestamp = (new DateTimeImmutable('now', cloudflared_log_timezone()))->format(DateTimeInterface::RFC3339);
	$lines = preg_split('/\r\n|\r|\n/', trim($text));
	$output = '';
	foreach ($lines as $line) {
		if ($line !== '') {
			$output .= $timestamp . ' ' . $labels[$level] . ' [updater] ' . $line . "\n";
		}
	}
	if ($output !== '') {
		// Append only: the daemon may have this same file open while an update runs.
		file_put_contents(CLOUDFLARED_LOG_FILE, $output, FILE_APPEND | LOCK_EX);
	}
}

function cloudflared_recent_log() {
	$file = @fopen(CLOUDFLARED_LOG_FILE, 'rb');
	if ($file === false) {
		return '';
	}
	$size = fstat($file)['size'];
	$offset = max(0, $size - 65536);
	fseek($file, $offset);
	if ($offset > 0) {
		fgets($file); // Discard a possibly partial first line.
	}
	$text = stream_get_contents($file);
	fclose($file);
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
	unset($line);
	return implode("\n", $lines);
}

function cloudflared_update_command($args, $log = false, $timeout = 300) {
	$process = proc_open($args, [0 => ['file', '/dev/null', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes);
	if (!is_resource($process)) {
		throw new RuntimeException('Cannot start package command.');
	}
	stream_set_blocking($pipes[1], false);
	stream_set_blocking($pipes[2], false);
	$output = '';
	$errors = '';
	$started = time();
	do {
		foreach ([1, 2] as $fd) {
			$chunk = stream_get_contents($pipes[$fd]);
			if ($chunk !== false && $chunk !== '') {
				if ($fd === 1) {
					$output .= $chunk;
				} else {
					$errors .= $chunk;
				}
				if ($log) {
					cloudflared_update_log($chunk, 'debug');
				}
			}
		}
		$status = proc_get_status($process);
		if ($status['running'] && time() - $started > $timeout) {
			proc_terminate($process, 9);
			foreach ($pipes as $pipe) {
				fclose($pipe);
			}
			proc_close($process);
			throw new RuntimeException('Package command timed out; inspect the Status log.');
		}
		if ($status['running']) {
			usleep(100000);
		}
	} while ($status['running']);
	foreach ($pipes as $fd => $pipe) {
		$chunk = stream_get_contents($pipe);
		if ($fd === 1) {
			$output .= $chunk;
		} else {
			$errors .= $chunk;
		}
		if ($log && $chunk !== '') {
			cloudflared_update_log($chunk, 'debug');
		}
		fclose($pipe);
	}
	proc_close($process);
	if ($status['exitcode'] !== 0) {
		throw new RuntimeException('Package command failed: ' . trim(substr($output . $errors, -2000)));
	}
	return trim($output);
}

function cloudflared_update_installed() {
	return cloudflared_update_command([CLOUDFLARED_PKG, 'query', '%v', CLOUDFLARED_PACKAGE_NAME], false, 15);
}

function cloudflared_update_http($url, $destination = null) {
	$curl = curl_init($url);
	$options = [
		CURLOPT_USERAGENT => 'pfSense-cloudflared-updater',
		CURLOPT_HTTPHEADER => ['Accept: application/vnd.github+json'],
		CURLOPT_CONNECTTIMEOUT => 15,
		CURLOPT_TIMEOUT => $destination === null ? 30 : 300,
		CURLOPT_FOLLOWLOCATION => true,
		CURLOPT_MAXREDIRS => 5,
		CURLOPT_PROTOCOLS => CURLPROTO_HTTPS,
		CURLOPT_REDIR_PROTOCOLS => CURLPROTO_HTTPS,
		CURLOPT_FAILONERROR => true,
	];
	$data = '';
	$size = 0;
	$file = $destination === null ? null : fopen($destination, 'wb');
	if ($destination !== null && $file === false) {
		throw new RuntimeException('Cannot create downloaded package.');
	}
	$options[CURLOPT_WRITEFUNCTION] = function ($curl, $chunk) use (&$data, &$size, $file) {
		$size += strlen($chunk);
		if ($size > ($file === null ? 2097152 : 134217728)) {
			return 0;
		}
		if ($file !== null) {
			return fwrite($file, $chunk);
		}
		$data .= $chunk;
		return strlen($chunk);
	};
	curl_setopt_array($curl, $options);
	$ok = curl_exec($curl);
	$error = curl_error($curl);
	$code = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
	curl_close($curl);
	if ($file !== null) {
		fclose($file);
	}
	if ($ok === false || $code !== 200) {
		throw new RuntimeException('GitHub request failed (HTTP ' . $code . '): ' . $error);
	}
	return $data;
}

function cloudflared_update_release($repository) {
	$repository = cloudflared_update_repository($repository);
	$slug = substr($repository, strlen('https://github.com/'));
	$release = json_decode(cloudflared_update_http('https://api.github.com/repos/' . $slug . '/releases/latest'), true);
	return cloudflared_update_parse_release($repository, $release);
}

function cloudflared_update_parse_release($repository, $release) {
	$repository = cloudflared_update_repository($repository);
	if (!is_array($release) || !empty($release['draft']) || !empty($release['prerelease'])) {
		throw new RuntimeException('No stable GitHub release available.');
	}
	foreach ($release['assets'] ?? [] as $asset) {
		if (!preg_match('/^pfSense-pkg-cloudflared-([0-9]+(?:\.[0-9]+)*(?:_[0-9]+)?)\.pkg$/D', $asset['name'] ?? '', $match)) {
			continue;
		}
		$url = $asset['browser_download_url'] ?? '';
		if (strpos($url, $repository . '/releases/download/') !== 0 || !preg_match('/^sha256:([a-f0-9]{64})$/D', $asset['digest'] ?? '', $digest)) {
			throw new RuntimeException('Release package is missing a trusted GitHub SHA-256 digest or download URL.');
		}
		return ['version' => $match[1], 'url' => $url, 'sha256' => $digest[1], 'repository' => $repository];
	}
	throw new RuntimeException('Latest release has no supported pfSense Cloudflared package.');
}

function cloudflared_update_busy() {
	cloudflared_update_directory();
	$lock = fopen(CLOUDFLARED_UPDATE_DIR . '/lock', 'c');
	$busy = !flock($lock, LOCK_EX | LOCK_NB);
	if (!$busy) {
		flock($lock, LOCK_UN);
	}
	fclose($lock);
	$state = cloudflared_update_state();
	return $busy || (($state['status'] ?? '') === 'queued' && time() - ($state['time'] ?? 0) < 30);
}

function cloudflared_update_launch($action, $repository) {
	if (!in_array($action, ['check', 'install'], true)) {
		throw new RuntimeException('Invalid update action.');
	}
	$repository = cloudflared_update_repository($repository);
	cloudflared_update_directory();
	$lock = fopen(CLOUDFLARED_UPDATE_DIR . '/lock', 'c');
	if (!flock($lock, LOCK_EX | LOCK_NB)) {
		fclose($lock);
		throw new RuntimeException('An update operation is already running.');
	}
	try {
		$state = cloudflared_update_state();
		if (($state['status'] ?? '') === 'queued' && time() - ($state['time'] ?? 0) < 30) {
			throw new RuntimeException('An update operation is already queued.');
		}
		cloudflared_update_state(['status' => 'queued', 'message' => 'Starting ' . $action . '...', 'repository' => $repository]);
		$command = '/usr/sbin/daemon -f /usr/local/bin/php -f /usr/local/pkg/cloudflared_update.php -- ' . escapeshellarg($action) . ' ' . escapeshellarg($repository);
		exec($command . ' >/dev/null 2>&1', $output, $code);
		if ($code !== 0) {
			cloudflared_update_state(['status' => 'error', 'message' => 'Cannot start background updater.']);
			throw new RuntimeException('Cannot start background updater.');
		}
	} finally {
		flock($lock, LOCK_UN);
		fclose($lock);
	}
}

function cloudflared_update_install_package($file, $release) {
	if (!hash_equals($release['sha256'], hash_file('sha256', $file))) {
		throw new RuntimeException('Package SHA-256 verification failed. Nothing installed.');
	}
	cloudflared_update_log('SHA-256 verified. Checking package identity and ABI.', 'debug');
	$metadata = cloudflared_update_command([CLOUDFLARED_PKG, 'query', '-F', $file, '%n|%v|%q']);
	$installed_abi = cloudflared_update_command([CLOUDFLARED_PKG, 'query', '%q', CLOUDFLARED_PACKAGE_NAME]);
	if ($metadata !== CLOUDFLARED_PACKAGE_NAME . '|' . $release['version'] . '|' . $installed_abi) {
		throw new RuntimeException('Package name, version, or FreeBSD ABI does not match this installation.');
	}
	$backup = CLOUDFLARED_UPDATE_DIR . '/previous';
	if (!is_dir($backup)) {
		mkdir($backup, 0700);
	}
	foreach (glob($backup . '/*.pkg') as $old) {
		unlink($old);
	}
	cloudflared_update_log('Backing up the installed package for manual recovery.', 'debug');
	cloudflared_update_command([CLOUDFLARED_PKG, 'create', '-o', $backup, CLOUDFLARED_PACKAGE_NAME], true);
	cloudflared_update_log('Installing complete package ' . $release['version'] . '.');
	try {
		if (is_service_running('cloudflared')) {
			stop_service('cloudflared');
		}
		cloudflared_update_command([CLOUDFLARED_PKG, 'add', '-f', $file], true);
		if (cloudflared_update_installed() !== $release['version']) {
			throw new RuntimeException('Installed package version does not match the release.');
		}
	} finally {
		cloudflared_update_log('Restoring service configuration and enabled state.', 'debug');
		cloudflared_sync();
		// pfSense starts services asynchronously; do not mistake startup latency for failure.
		$deadline = time() + 30;
		while (cloudflared_enabled() && !is_service_running('cloudflared') && time() < $deadline) {
			usleep(250000);
		}
	}
	if (cloudflared_enabled() && !is_service_running('cloudflared')) {
		throw new RuntimeException('Package installed, but Cloudflared did not restart. Inspect the Status tab.');
	}
	cloudflared_update_log('Package installation complete; registered version is ' . cloudflared_update_installed() . '.');
}
