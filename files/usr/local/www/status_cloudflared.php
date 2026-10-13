<?php

require_once('guiconfig.inc');
require_once('/usr/local/pkg/cloudflared.inc');
require_once('/usr/local/pkg/cloudflared_update.inc');

const CLOUDFLARED_BIN = '/usr/local/bin/cloudflared';

$repository = config_get_path('installedpackages/cloudflared/updater/repository', CLOUDFLARED_UPDATE_REPOSITORY);
$update_permissions = $_SESSION['page-match'] ?? [];
// The action-only privilege is not a URL; pfSense's page matcher requires a real file.
$can_update = isAdminUID($_SESSION['Username'] ?? '') ||
	in_array('*', $update_permissions, true) || in_array('cloudflared-update', $update_permissions, true);
$input_errors = [];
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
	try {
		if (!$can_update) {
			http_response_code(403);
			throw new RuntimeException('Cloudflared Update privilege is required to change or install package updates.');
		}
		$repository = cloudflared_update_repository($_POST['repository'] ?? '');
		$action = isset($_POST['install']) ? 'install' : (isset($_POST['check']) ? 'check' : '');
		if (!in_array($action, ['check', 'install'], true)) {
			throw new RuntimeException('Invalid update action.');
		}
		if (cloudflared_update_busy()) {
			throw new RuntimeException('An update operation is already running.');
		}
		$state = cloudflared_update_state();
		if ($action === 'install' && (($state['status'] ?? '') !== 'available' || ($state['repository'] ?? '') !== $repository)) {
			throw new RuntimeException('Refresh this repository before installing an update.');
		}
		config_set_path('installedpackages/cloudflared/updater/repository', $repository);
		write_config('Cloudflared: updated package repository');
		cloudflared_update_launch($action, $repository);
		header('Location: /status_cloudflared.php');
		exit;
	} catch (Throwable $error) {
		$input_errors[] = $error->getMessage();
	}
}

function cloudflared_command_output($command) {
	$output = [];
	$result = 0;
	exec($command . ' 2>&1', $output, $result);
	return trim(implode("\n", $output));
}

function cloudflared_redact($text) {
	$text = preg_replace('/--token(?:=|\s+)\S+/', '--token [REDACTED]', (string)$text);
	$text = preg_replace('/token(?:=|: )\S+/i', 'token=[REDACTED]', $text);
	$text = preg_replace('/[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}/', '[REDACTED-JWT]', $text);
	return $text;
}

if (isset($_GET['ajax'])) {
	header('Content-Type: application/json');
	header('Cache-Control: no-store');
	try {
		$update = cloudflared_update_state();
		$update['busy'] = cloudflared_update_busy();
		$update['installed'] = cloudflared_update_installed();
		$update['can_update'] = $can_update;
		$update['checked'] = !empty($update['time']) ? cloudflared_log_time($update['time']) : '';
		if (!$update['busy'] && in_array($update['status'] ?? '', ['queued', 'checking', 'installing'], true)) {
			$update['status'] = 'error';
			$update['message'] = 'Update worker stopped unexpectedly. Refresh to retry; inspect the Recent Log below.';
		}
	} catch (Throwable $error) {
		$update = ['status' => 'error', 'message' => $error->getMessage(), 'busy' => false, 'can_update' => $can_update];
	}
	$status = [
		'enabled' => ((cloudflared_settings()['enable'] ?? '') === 'on'),
		'running' => is_service_running('cloudflared'),
		'token_configured' => is_readable(CLOUDFLARED_TOKEN_FILE) && filesize(CLOUDFLARED_TOKEN_FILE) > 0,
		'protocol' => cloudflared_settings()['protocol'] ?? 'auto',
		'loglevel' => cloudflared_settings()['loglevel'] ?? 'info',
		'version' => is_executable(CLOUDFLARED_BIN) ? cloudflared_command_output(CLOUDFLARED_BIN . ' version') : 'cloudflared binary is missing',
		'log' => cloudflared_redact(cloudflared_recent_log()),
		'update' => $update,
	];
	echo json_encode($status);
	exit;
}

$shortcut_section = 'cloudflared';
$pgtitle = [gettext('Services'), gettext('Cloudflared'), gettext('Status')];
include('head.inc');
if ($input_errors) {
	print_input_errors($input_errors);
}
$tabs = [
	[gettext('Settings'), false, '/cloudflared_settings.php'],
	[gettext('Status'), true, '/status_cloudflared.php'],
];
display_top_tabs($tabs);

?>
<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=gettext('Service')?></h2></div>
	<div class="panel-body form-horizontal">
<?php
foreach ([
	'cloudflared-enabled' => 'Enabled',
	'cloudflared-state' => 'State',
	'cloudflared-token' => 'Token Configured',
	'cloudflared-protocol' => 'Protocol',
	'cloudflared-loglevel' => 'Log Level',
	'cloudflared-version' => 'Version',
] as $id => $label) {
?>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?=gettext($label)?></label>
			<div class="col-sm-10"><span id="<?=$id?>">-</span></div>
		</div>
<?php
}
?>
	</div>
</div>

<form method="post" action="/status_cloudflared.php" class="form-horizontal" id="cloudflared-update-form">
	<div class="panel panel-default">
		<div class="panel-heading"><h2 class="panel-title"><?=gettext('Updater')?></h2></div>
		<div class="panel-body">
<?php
$repository_group = new Form_Group('GitHub Repository');
$repository_input = new Form_Input('repository', 'GitHub Repository', 'url', $repository);
$repository_input->setAttribute('required', true);
$repository_input->setHelp('Only use a repository you trust: its packages execute as root. Refresh saves the URL and checks the latest stable release. Updates preserve tunnel settings but briefly interrupt the tunnel.');
if (!$can_update) {
	$repository_input->setAttribute('disabled', true);
}
$repository_group->add($repository_input);
print $repository_group;
foreach ([
	'update-installed' => 'Installed Package',
	'update-latest' => 'Latest Package',
	'update-status' => 'Status',
	'update-checked' => 'Last Checked',
] as $id => $label) {
?>
			<div class="form-group">
				<label class="col-sm-2 control-label"><?=gettext($label)?></label>
				<div class="col-sm-10"><span id="<?=$id?>">-</span></div>
			</div>
<?php
}
$refresh = new Form_Button('check', 'Refresh', null, 'fa-solid fa-refresh');
$refresh->setAttribute('id', 'update-refresh')->setAttribute('disabled', true)->addClass('btn-default');
$install = new Form_Button('install', 'Update', null, 'fa-solid fa-download');
$install->setAttribute('id', 'update-install')->setAttribute('disabled', true)->addClass('btn-primary');
$buttons = new Form_StaticText(null, (string)$refresh . ' ' . (string)$install);
if (!$can_update) {
	$buttons->setHelp('The Cloudflared Update privilege is required to check or install updates.');
}
$button_group = new Form_Group(null);
$button_group->add($buttons);
print $button_group;
?>
		</div>
	</div>
</form>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=gettext('Recent Log')?></h2></div>
	<div class="panel-body">
		<pre id="cloudflared-log" style="max-height: 30em; overflow: auto;">-</pre>
	</div>
</div>

<script>
let updateState = {};
function updateButtons() {
	document.getElementById('update-refresh').disabled = !updateState.can_update || !!updateState.busy;
	document.getElementById('update-install').disabled = !updateState.can_update || !!updateState.busy || updateState.status !== 'available' || document.getElementById('repository').value.replace(/\/+$/, '') !== updateState.repository;
}
document.getElementById('repository').addEventListener('input', updateButtons);
document.getElementById('cloudflared-update-form').addEventListener('submit', function (event) {
	if (event.submitter && event.submitter.id === 'update-install' && !confirm('Install the latest complete Cloudflared package? The tunnel will briefly restart.')) {
		event.preventDefault();
	}
});
async function refreshCloudflared() {
	try {
		const response = await fetch('/status_cloudflared.php?ajax=1', {cache: 'no-store'});
		if (!response.ok) throw new Error('Cannot read status.');
		const data = await response.json();
		document.getElementById('cloudflared-enabled').textContent = data.enabled ? 'Yes' : 'No';
		document.getElementById('cloudflared-state').textContent = data.running ? 'Running' : 'Stopped';
		document.getElementById('cloudflared-token').textContent = data.token_configured ? 'Yes' : 'No';
		document.getElementById('cloudflared-protocol').textContent = data.protocol || 'auto';
		document.getElementById('cloudflared-loglevel').textContent = data.loglevel || 'info';
		document.getElementById('cloudflared-version').textContent = data.version || '-';
		document.getElementById('cloudflared-log').textContent = data.log || 'No readable log output.';
		updateState = data.update || {};
		document.getElementById('update-installed').textContent = updateState.installed || '-';
		document.getElementById('update-latest').textContent = updateState.latest || '-';
		document.getElementById('update-status').textContent = updateState.message || 'Not checked. Click Refresh.';
		document.getElementById('update-checked').textContent = updateState.checked || '-';
		updateButtons();
	} catch (error) {
		document.getElementById('update-status').textContent = 'Cannot read status. Reload or sign in again.';
		document.getElementById('update-refresh').disabled = true;
		document.getElementById('update-install').disabled = true;
	} finally {
		setTimeout(refreshCloudflared, updateState.busy ? 2000 : 5000);
	}
}
refreshCloudflared();
</script>

<?php include('foot.inc'); ?>
