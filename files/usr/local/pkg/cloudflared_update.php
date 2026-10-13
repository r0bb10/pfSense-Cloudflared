<?php

// CLI-only worker. The WebGUI launches it after pfSense authentication and CSRF validation.
if (PHP_SAPI !== 'cli') {
	http_response_code(403);
	exit;
}
require_once('/usr/local/pkg/cloudflared.inc');
require_once('/usr/local/pkg/cloudflared_update.inc');

umask(0077);
cloudflared_update_directory();
$lock = fopen(CLOUDFLARED_UPDATE_DIR . '/lock', 'c');
if (!flock($lock, LOCK_EX)) {
	exit(1);
}
$file = CLOUDFLARED_UPDATE_DIR . '/download.pkg';
$state = [];
try {
	$action = $argv[1] ?? '';
	if (!in_array($action, ['check', 'install'], true)) {
		throw new RuntimeException('Invalid update action.');
	}
	$repository = cloudflared_update_repository($argv[2] ?? '');
	$state = ['status' => 'checking', 'repository' => $repository, 'message' => 'Checking GitHub release...'];
	cloudflared_update_state($state);
	cloudflared_update_log('Checking ' . $repository . '.', 'debug');
	$release = cloudflared_update_release($repository);
	$installed = cloudflared_update_installed();
	$newer = cloudflared_update_command([CLOUDFLARED_PKG, 'version', '-t', $installed, $release['version']]) === '<';
	$state['latest'] = $release['version'];
	$state['installed'] = $installed;
	$state['status'] = $newer ? 'available' : 'current';
	$state['message'] = $newer ? 'Update available.' : 'Up to date.';
	if ($action === 'install' && $newer) {
		$state['status'] = 'installing';
		$state['message'] = 'Downloading and installing package ' . $release['version'] . '...';
		cloudflared_update_state($state);
		cloudflared_update_log('Downloading package ' . $release['version'] . '.');
		cloudflared_update_http($release['url'], $file);
		cloudflared_update_install_package($file, $release);
		$state['installed'] = cloudflared_update_installed();
		$state['status'] = 'current';
		$state['message'] = 'Update complete. Up to date.';
	}
	cloudflared_update_log($state['message'] . ' Installed: ' . $state['installed'] . '; latest: ' . $state['latest'] . '.');
	cloudflared_update_state($state);
} catch (Throwable $error) {
	$state['status'] = 'error';
	$state['message'] = $error->getMessage();
	cloudflared_update_log($error->getMessage(), 'error');
	cloudflared_update_state($state);
} finally {
	@unlink($file);
	flock($lock, LOCK_UN);
	fclose($lock);
}
