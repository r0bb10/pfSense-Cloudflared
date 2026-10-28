<?php

const CLOUDFLARED_UPDATE_DIR = '/var/db/cloudflared-update';
const CLOUDFLARED_UPDATE_REPOSITORY = 'https://github.com/r0bb10/pfSense-Cloudflared';
const CLOUDFLARED_PACKAGE_NAME = 'pfSense-pkg-cloudflared';
const CLOUDFLARED_PKG = '/usr/local/sbin/pkg';
const CLOUDFLARED_UPDATE_LOG = CLOUDFLARED_UPDATE_DIR . '/update.log';
const CLOUDFLARED_UPDATE_PHASES = ['queued', 'checking', 'downloading', 'verifying', 'installing', 'restarting'];

function cloudflared_can_update() {
	$permissions = $_SESSION['page-match'] ?? [];
	return isAdminUID($_SESSION['Username'] ?? '') || in_array('*', $permissions, true) || in_array('cloudflared-update', $permissions, true);
}

function cloudflared_update_repository($url) {
	if (!is_string($url)) {
		throw new RuntimeException('Invalid repository URL.');
	}
	$url = rtrim(trim($url), '/');
	if (!preg_match('#^https://github\.com/([A-Za-z0-9-]+)/([A-Za-z0-9_.-]+)$#D', $url, $match) || in_array($match[2], ['.', '..'], true)) {
		throw new RuntimeException('Use a public GitHub repository URL: https://github.com/owner/repository');
	}
	return $url;
}

function cloudflared_update_directory() {
	if ((!is_dir(CLOUDFLARED_UPDATE_DIR) && !mkdir(CLOUDFLARED_UPDATE_DIR, 0700, true)) || !chmod(CLOUDFLARED_UPDATE_DIR, 0700)) {
		throw new RuntimeException('Cannot secure the update directory.');
	}
}

function cloudflared_update_state($state = null) {
	$file = CLOUDFLARED_UPDATE_DIR . '/state.json';
	if ($state === null) {
		if (!is_file($file)) {
			return [];
		}
		$data = json_decode(file_get_contents($file), true);
		if (!is_array($data)) {
			throw new RuntimeException('Update state is unreadable. Check for updates to reset it.');
		}
		return $data;
	}
	$state['time'] = time();
	cloudflared_update_directory();
	cloudflared_atomic_write($file, json_encode($state, JSON_THROW_ON_ERROR));
	return $state;
}

function cloudflared_update_lock() {
	cloudflared_update_directory();
	// Prevent child processes from inheriting the lock.
	$lock = fopen(CLOUDFLARED_UPDATE_DIR . '/lock', 'ce');
	if ($lock === false) {
		throw new RuntimeException('Cannot open the Cloudflared operation lock.');
	}
	if (!flock($lock, LOCK_EX | LOCK_NB)) {
		fclose($lock);
		throw new RuntimeException('A Cloudflared operation is already running.', 409);
	}
	return $lock;
}

function cloudflared_update_unlock($lock) {
	flock($lock, LOCK_UN);
	fclose($lock);
}

function cloudflared_update_queued($state) {
	return ($state['status'] ?? '') === 'queued' && time() - ($state['time'] ?? 0) < 30;
}

function cloudflared_update_busy() {
	try {
		$lock = cloudflared_update_lock();
	} catch (RuntimeException $error) {
		if ($error->getCode() === 409) {
			return true;
		}
		throw $error;
	}
	try {
		return cloudflared_update_queued(cloudflared_update_state());
	} finally {
		cloudflared_update_unlock($lock);
	}
}

function cloudflared_update_log($text, $level = 'info') {
	cloudflared_update_directory();
	if (is_file(CLOUDFLARED_UPDATE_LOG) && filesize(CLOUDFLARED_UPDATE_LOG) > 1048576) {
		if (!rename(CLOUDFLARED_UPDATE_LOG, CLOUDFLARED_UPDATE_LOG . '.1')) {
			throw new RuntimeException('Cannot rotate the update log.');
		}
	}
	$timestamp = (new DateTimeImmutable('now', cloudflared_log_timezone()))->format(DateTimeInterface::RFC3339);
	$output = '';
	foreach (preg_split('/\r\n|\r|\n/', trim(cloudflared_redact($text))) as $line) {
		if ($line !== '') {
			$output .= $timestamp . ' ' . strtoupper($level) . ' ' . $line . "\n";
		}
	}
	if (file_put_contents(CLOUDFLARED_UPDATE_LOG, $output, FILE_APPEND | LOCK_EX) === false || !chmod(CLOUDFLARED_UPDATE_LOG, 0600)) {
		throw new RuntimeException('Cannot write update diagnostics.');
	}
}

function cloudflared_update_phase(&$state, $phase, $message) {
	$state['status'] = $phase;
	$state['message'] = $message;
	cloudflared_update_state($state);
	cloudflared_update_log($message);
}

function cloudflared_update_command($args, $log = false, $timeout = 300) {
	$process = proc_open($args, [0 => ['file', '/dev/null', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes);
	if (!is_resource($process)) {
		throw new RuntimeException('Cannot start package command.');
	}
	foreach ($pipes as $pipe) {
		stream_set_blocking($pipe, false);
	}
	$output = $errors = '';
	$started = microtime(true);
	try {
		do {
			foreach ($pipes as $fd => $pipe) {
				$chunk = stream_get_contents($pipe);
				if ($chunk === false || $chunk === '') {
					continue;
				}
				if ($fd === 1) {
					$output = substr($output . $chunk, -262144);
				} else {
					$errors = substr($errors . $chunk, -16384);
				}
				if ($log) {
					// Logging failures must not interrupt pkg.
					try {
						cloudflared_update_log($chunk, 'debug');
					} catch (Throwable $error) {
						error_log('Cloudflared update diagnostics: ' . $error->getMessage());
					}
				}
			}
			$status = proc_get_status($process);
			if (!$status['running']) {
				// Preserve the first exit status on PHP 8.2.
				$output = substr($output . stream_get_contents($pipes[1]), -262144);
				$errors = substr($errors . stream_get_contents($pipes[2]), -16384);
				break;
			}
			if ($timeout > 0 && microtime(true) - $started > $timeout) {
				proc_terminate($process, 9);
				throw new RuntimeException('Package query timed out. Retry when Package Manager is idle.');
			}
			usleep(100000);
		} while (true);
	} finally {
		foreach ($pipes as $pipe) {
			fclose($pipe);
		}
		proc_close($process);
	}
	if ($status['exitcode'] !== 0) {
		throw new RuntimeException('Package command failed: ' . cloudflared_redact(trim(substr($output . $errors, -2000))));
	}
	return trim($output);
}

function cloudflared_update_installed($cached = false) {
	$file = CLOUDFLARED_UPDATE_DIR . '/installed.json';
	if ($cached && is_file($file) && time() - filemtime($file) < 60) {
		$data = json_decode(file_get_contents($file), true);
		if (is_string($data['version'] ?? null)) {
			return $data['version'];
		}
	}
	$version = cloudflared_update_command([CLOUDFLARED_PKG, 'query', '%v', CLOUDFLARED_PACKAGE_NAME], false, 15);
	cloudflared_update_directory();
	cloudflared_atomic_write($file, json_encode(['version' => $version], JSON_THROW_ON_ERROR));
	return $version;
}

function cloudflared_update_url($url) {
	$parts = is_string($url) ? parse_url($url) : false;
	$hosts = ['api.github.com', 'github.com', 'release-assets.githubusercontent.com',
		'objects.githubusercontent.com', 'github-releases.githubusercontent.com'];
	if (!$parts || ($parts['scheme'] ?? '') !== 'https' || !in_array($parts['host'] ?? '', $hosts, true) ||
		isset($parts['user']) || isset($parts['pass']) || isset($parts['fragment']) ||
		(isset($parts['port']) && $parts['port'] !== 443) || preg_match('/[\x00-\x20\x7f]/', $url)) {
		throw new RuntimeException('GitHub returned an unsupported download or redirect URL.');
	}
	return $url;
}

function cloudflared_update_http($url, $destination = null) {
	$deadline = microtime(true) + ($destination === null ? 30 : 300);
	for ($redirect = 0; $redirect <= 5; $redirect++) {
		cloudflared_update_url($url);
		$data = $location = '';
		$size = 0;
		$file = $destination === null ? null : fopen($destination, 'wb');
		if ($destination !== null && $file === false) {
			throw new RuntimeException('Cannot create downloaded package.');
		}
		$curl = curl_init($url);
		curl_setopt_array($curl, [
			CURLOPT_USERAGENT => 'pfSense-cloudflared-updater',
			CURLOPT_HTTPHEADER => ['Accept: application/vnd.github+json'],
			CURLOPT_CONNECTTIMEOUT => 10,
			CURLOPT_TIMEOUT => max(1, (int)ceil($deadline - microtime(true))),
			CURLOPT_FOLLOWLOCATION => false,
			CURLOPT_PROTOCOLS => CURLPROTO_HTTPS,
			CURLOPT_SSL_VERIFYPEER => true, CURLOPT_SSL_VERIFYHOST => 2,
			CURLOPT_HEADERFUNCTION => function ($curl, $header) use (&$location) {
				if (stripos($header, 'Location:') === 0) {
					$location = trim(substr($header, 9));
				}
				return strlen($header);
			},
			CURLOPT_WRITEFUNCTION => function ($curl, $chunk) use (&$data, &$size, $file) {
				$size += strlen($chunk);
				if ($size > ($file === null ? 2097152 : 134217728)) {
					return 0;
				}
				if ($file !== null) {
					return fwrite($file, $chunk);
				}
				$data .= $chunk;
				return strlen($chunk);
			},
		]);
		try {
			$ok = curl_exec($curl);
			$error = curl_error($curl);
			$code = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
		} finally {
			curl_close($curl);
			if ($file !== null) {
				fclose($file);
			}
		}
		if ($ok === false) {
			throw new RuntimeException('GitHub transfer failed: ' . $error);
		}
		if ($code === 200) {
			return $data;
		}
		if (in_array($code, [301, 302, 303, 307, 308], true) && $location !== '' && microtime(true) < $deadline) {
			$url = cloudflared_update_url($location);
			continue;
		}
		throw new RuntimeException('GitHub request failed (HTTP ' . $code . '). Retry later.');
	}
	throw new RuntimeException('Too many GitHub redirects.');
}

function cloudflared_update_release($repository) {
	$repository = cloudflared_update_repository($repository);
	$slug = substr($repository, strlen('https://github.com/'));
	$release = json_decode(cloudflared_update_http('https://api.github.com/repos/' . $slug . '/releases/latest'), true, 512, JSON_THROW_ON_ERROR);
	return cloudflared_update_parse_release($repository, $release);
}

function cloudflared_update_parse_release($repository, $release) {
	$repository = cloudflared_update_repository($repository);
	if (!is_array($release) || ($release['draft'] ?? true) !== false || ($release['prerelease'] ?? true) !== false ||
		!is_string($release['tag_name'] ?? null) || !preg_match('/^[0-9]+\.[0-9]+\.[0-9]+$/D', $release['tag_name']) || !is_array($release['assets'] ?? null)) {
		throw new RuntimeException('No supported stable GitHub release available.');
	}
	$version = $release['tag_name'];
	$expected = CLOUDFLARED_PACKAGE_NAME . '-' . $version . '.pkg';
	$matches = array_values(array_filter($release['assets'], fn($asset) => is_array($asset) && ($asset['name'] ?? '') === $expected));
	if (count($matches) !== 1) {
		throw new RuntimeException('Release must contain exactly one package matching its version.');
	}
	$asset = $matches[0];
	$url = $repository . '/releases/download/' . $version . '/' . $expected;
	if (($asset['browser_download_url'] ?? '') !== $url || !is_string($asset['digest'] ?? null) ||
		!preg_match('/^sha256:([a-f0-9]{64})$/D', $asset['digest'], $digest) ||
		!is_int($asset['size'] ?? null) || $asset['size'] < 1 || $asset['size'] > 134217728) {
		throw new RuntimeException('Release package has an invalid URL, size or GitHub SHA-256 digest.');
	}
	return ['version' => $version, 'url' => $url, 'sha256' => $digest[1], 'size' => $asset['size'], 'repository' => $repository];
}

function cloudflared_update_candidate($state, $repository, $check_id, $installed) {
	if (($state['status'] ?? '') !== 'available' || !is_string($check_id) || $check_id === '' ||
		!hash_equals($state['check_id'] ?? '', $check_id) || ($state['repository'] ?? '') !== $repository ||
		($state['installed'] ?? '') !== $installed || time() - ($state['checked_at'] ?? 0) > 86400 ||
		!is_array($state['candidate'] ?? null)) {
		throw new RuntimeException('The checked update changed or expired. Check for updates again.');
	}
	return $state['candidate'];
}

function cloudflared_update_check_cached($state, $repository, $installed) {
	return ($state['action'] ?? '') === 'check' && in_array($state['status'] ?? '', ['available', 'current'], true) &&
		($state['repository'] ?? '') === $repository && ($state['installed'] ?? '') === $installed &&
		time() - ($state['checked_at'] ?? 0) < 900;
}

function cloudflared_update_launch($action, $repository, $check_id = '', $automatic = false) {
	if (!in_array($action, ['check', 'install'], true)) {
		throw new RuntimeException('Invalid update action.');
	}
	$repository = cloudflared_update_repository($repository);
	$lock = cloudflared_update_lock();
	try {
		if (!config_read_file() || config_get_path('installedpackages/cloudflared/updater/repository', CLOUDFLARED_UPDATE_REPOSITORY) !== $repository) {
			throw new RuntimeException('Update repository changed. Reload the page and check again.');
		}
		try {
			$state = cloudflared_update_state();
		} catch (RuntimeException $error) {
			if ($action !== 'check') {
				throw $error;
			}
			$state = [];
		}
		if (cloudflared_update_queued($state)) {
			if ($automatic) {
				return;
			}
			throw new RuntimeException('An update operation is already queued.');
		}
		if ($automatic && ((($state['action'] ?? '') === 'install' && ($state['status'] ?? '') === 'error') ||
			cloudflared_update_check_cached($state, $repository, cloudflared_update_installed()))) {
			return;
		}
		$candidate = $action === 'install' ? cloudflared_update_candidate($state, $repository, $check_id, cloudflared_update_installed()) : null;
		$id = bin2hex(random_bytes(16));
		$request = ['operation_id' => $id, 'action' => $action, 'repository' => $repository,
			'status' => 'queued', 'message' => 'Starting update ' . ($action === 'check' ? 'check' : 'installation') . '...',
			'candidate' => $candidate, 'installed' => $state['installed'] ?? '', 'checked_at' => $state['checked_at'] ?? null];
		cloudflared_update_state($request);
		$command = '/usr/sbin/daemon -f /usr/local/bin/php -f /usr/local/pkg/cloudflared_update.php -- ' . escapeshellarg($id);
		exec($command . ' >/dev/null 2>&1', $output, $code);
		if ($code !== 0) {
			$request['status'] = 'error';
			$request['message'] = 'Cannot start background updater.';
			cloudflared_update_state($request);
			throw new RuntimeException($request['message']);
		}
	} finally {
		cloudflared_update_unlock($lock);
	}
}

function cloudflared_update_verify_package($file, $release, $installed_abi) {
	clearstatcache(true, $file);
	if (!is_file($file) || filesize($file) !== $release['size'] || !hash_equals($release['sha256'], hash_file('sha256', $file))) {
		throw new RuntimeException('Package size or SHA-256 verification failed. Nothing installed.');
	}
	$metadata = cloudflared_update_command([CLOUDFLARED_PKG, 'query', '-F', $file, '%n|%v|%q']);
	if ($metadata !== CLOUDFLARED_PACKAGE_NAME . '|' . $release['version'] . '|' . $installed_abi) {
		throw new RuntimeException('Package name, version or FreeBSD ABI does not match. Nothing installed.');
	}
}

function cloudflared_update_install_package($file, $release, &$state) {
	cloudflared_update_phase($state, 'verifying', 'Verifying package size, SHA-256, identity and ABI...');
	$abi = cloudflared_update_command([CLOUDFLARED_PKG, 'query', '%q', CLOUDFLARED_PACKAGE_NAME]);
	$current_abi = cloudflared_update_command([CLOUDFLARED_PKG, 'config', 'ABI']);
	if ($abi !== $current_abi) {
		throw new RuntimeException('Installed package ABI differs from this system. Reinstall a compatible package first.');
	}
	cloudflared_update_verify_package($file, $release, $abi);
	// Check pfSense's advisory package lock.
	if (is_subsystem_dirty('pkg') || is_subsystem_dirty('packagelock')) {
		throw new RuntimeException('pfSense Package Manager is busy. Retry after it finishes.');
	}
	mark_subsystem_dirty('pkg');
	$stopped = false;
	$failure = null;
	try {
		if (cloudflared_update_installed() !== $state['installed']) {
			throw new RuntimeException('Installed version changed. Check for updates again.');
		}
		cloudflared_update_phase($state, 'installing', 'Installing package ' . $release['version'] . '; the tunnel will briefly stop...');
		$stopped = true;
		if (is_service_running('cloudflared')) {
			stop_service('cloudflared');
		}
		if (is_service_running('cloudflared')) {
			throw new RuntimeException('Cannot stop the tunnel safely. Nothing installed.');
		}
		// Never time out an active installation.
		cloudflared_update_command([CLOUDFLARED_PKG, 'add', '-f', $file], true, 0);
		if (cloudflared_update_installed() !== $release['version']) {
			throw new RuntimeException('Installed package version does not match the selected release.');
		}
		$state['package_installed'] = true;
	} catch (Throwable $error) {
		$failure = $error;
	} finally {
		try {
			if ($stopped) {
				try {
					cloudflared_update_phase($state, 'restarting', 'Restoring tunnel configuration and enabled state...');
				} catch (Throwable $error) {
					// Restore service even if diagnostics fail.
					error_log('Cloudflared update recovery diagnostics: ' . $error->getMessage());
				}
				cloudflared_sync();
			}
		} catch (Throwable $error) {
			$failure = new RuntimeException(($failure ? $failure->getMessage() . ' ' : '') .
				(!empty($state['package_installed']) ? 'Package installed, but service recovery failed: ' : 'Service recovery failed: ') . $error->getMessage());
		} finally {
			clear_subsystem_dirty('pkg');
		}
	}
	if ($failure) {
		throw $failure;
	}
}

function cloudflared_update_summary($can_update) {
	$busy = cloudflared_update_busy();
	$state = cloudflared_update_state();
	if (!$busy && in_array($state['status'] ?? '', CLOUDFLARED_UPDATE_PHASES, true)) {
		$state['status'] = 'error';
		$state['message'] = 'Update worker stopped unexpectedly. Inspect update diagnostics before retrying; installation may be incomplete.';
	}
	$repository = config_get_path('installedpackages/cloudflared/updater/repository', CLOUDFLARED_UPDATE_REPOSITORY);
	$installed = cloudflared_update_installed(true);
	if (!$busy && ($state['status'] ?? '') === 'available' &&
		(($state['repository'] ?? '') !== $repository || ($state['installed'] ?? '') !== $installed || time() - ($state['checked_at'] ?? 0) > 86400)) {
		$state['status'] = 'unchecked';
		$state['message'] = 'Check for updates again; the previous check expired or settings changed.';
	}
	return ['busy' => $busy, 'can_update' => $can_update, 'installed' => $installed,
		'status' => $state['status'] ?? 'unchecked', 'message' => ($state['status'] ?? '') === 'current' ? 'Up to date.' : ($state['message'] ?? 'Not checked.'),
		'latest' => $state['candidate']['version'] ?? $state['latest'] ?? '', 'check_id' => $state['check_id'] ?? ''];
}
