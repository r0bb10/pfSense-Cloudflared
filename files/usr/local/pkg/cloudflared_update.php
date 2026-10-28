<?php

if (PHP_SAPI !== 'cli') {
	http_response_code(403);
	exit;
}
require_once('/usr/local/pkg/cloudflared.inc');
require_once('/usr/local/pkg/cloudflared_update.inc');

umask(0077);
cloudflared_update_directory();
$id = $argv[1] ?? '';
if (!preg_match('/^[a-f0-9]{32}$/D', $id)) {
	exit(1);
}
// Wait for the launcher's lock handoff.
$lock = fopen(CLOUDFLARED_UPDATE_DIR . '/lock', 'ce');
if ($lock === false || !flock($lock, LOCK_EX)) {
	exit(1);
}
$state = cloudflared_update_state();
if (($state['operation_id'] ?? '') !== $id || !cloudflared_update_queued($state)) {
	cloudflared_update_unlock($lock);
	exit(1);
}
$file = CLOUDFLARED_UPDATE_DIR . '/download-' . $id . '.pkg';
$exit_code = 0;
try {
	$repository = cloudflared_update_repository($state['repository']);
	$action = $state['action'];
	if ($action === 'check') {
		cloudflared_update_phase($state, 'checking', 'Checking the latest stable GitHub release...');
		$release = cloudflared_update_release($repository);
		$installed = cloudflared_update_installed();
		$newer = cloudflared_update_command([CLOUDFLARED_PKG, 'version', '-t', $installed, $release['version']]) === '<';
		$state['candidate'] = $release;
		$state['check_id'] = $id;
		$state['installed'] = $installed;
		$state['checked_at'] = time();
		cloudflared_update_phase($state, $newer ? 'available' : 'current', $newer ? 'Version ' . $release['version'] . ' is available.' : 'Up to date.');
	} elseif ($action === 'install' && is_array($state['candidate'] ?? null)) {
		// Install only the confirmed candidate.
		$release = $state['candidate'];
		cloudflared_update_phase($state, 'downloading', 'Downloading checked package ' . $release['version'] . '...');
		cloudflared_update_http($release['url'], $file);
		cloudflared_update_install_package($file, $release, $state);
		$state['installed'] = cloudflared_update_installed();
		cloudflared_update_phase($state, 'current', 'Update complete. Installed ' . $state['installed'] . '.');
	} else {
		throw new RuntimeException('Invalid queued update action.');
	}
} catch (Throwable $error) {
	$exit_code = 1;
	$state['status'] = 'error';
	$state['message'] = cloudflared_redact($error->getMessage());
	cloudflared_update_state($state);
	cloudflared_update_log($state['message'], 'error');
} finally {
	if (is_file($file)) {
		unlink($file);
	}
	cloudflared_update_unlock($lock);
}
exit($exit_code);
