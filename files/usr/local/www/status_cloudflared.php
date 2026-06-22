<?php

require_once('guiconfig.inc');
require_once('service-utils.inc');

const CLOUDFLARED_BIN = '/usr/local/bin/cloudflared';
const CLOUDFLARED_LOG = '/var/log/cloudflared.log';
const CLOUDFLARED_TOKEN_FILE = '/usr/local/etc/cloudflared/token';

function cloudflared_status_escape($value) {
	return htmlspecialchars((string)$value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function cloudflared_command_output($command) {
	$output = [];
	$result = 0;
	exec($command . ' 2>&1', $output, $result);
	return trim(implode("\n", $output));
}

function cloudflared_settings() {
	return config_get_path('installedpackages/cloudflared/config/0', []);
}

function cloudflared_redact($text) {
	$text = preg_replace('/--token(?:=|\s+)\S+/', '--token [REDACTED]', (string)$text);
	$text = preg_replace('/token(?:=|: )\S+/i', 'token=[REDACTED]', $text);
	$text = preg_replace('/[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}/', '[REDACTED-JWT]', $text);
	return $text;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action'])) {
	if ($_POST['action'] === 'start') {
		start_service('cloudflared');
	} elseif ($_POST['action'] === 'stop') {
		stop_service('cloudflared');
	} elseif ($_POST['action'] === 'restart') {
		restart_service('cloudflared');
	}
	header('Location: /status_cloudflared.php');
	exit;
}

if (isset($_GET['ajax'])) {
	header('Content-Type: application/json');
	header('Cache-Control: no-store');
	$status = [
		'enabled' => ((cloudflared_settings()['enable'] ?? '') === 'on'),
		'running' => is_service_running('cloudflared'),
		'token_configured' => is_readable(CLOUDFLARED_TOKEN_FILE) && filesize(CLOUDFLARED_TOKEN_FILE) > 0,
		'protocol' => cloudflared_settings()['protocol'] ?? 'auto',
		'loglevel' => cloudflared_settings()['loglevel'] ?? 'info',
		'version' => is_executable(CLOUDFLARED_BIN) ? cloudflared_command_output(CLOUDFLARED_BIN . ' version') : 'cloudflared binary is missing',
		'log' => is_readable(CLOUDFLARED_LOG) ? cloudflared_redact(implode("\n", array_slice(file(CLOUDFLARED_LOG, FILE_IGNORE_NEW_LINES), -30))) : '',
	];
	echo json_encode($status);
	exit;
}

$pgtitle = [gettext('Status'), gettext('Cloudflared')];
include('head.inc');
?>

<ul class="nav nav-tabs">
	<li><a href="/pkg_edit.php?xml=cloudflared.xml"><?=gettext('Settings')?></a></li>
	<li class="active"><a href="/status_cloudflared.php"><?=gettext('Status')?></a></li>
</ul>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=gettext('Service')?></h2></div>
	<div class="panel-body">
		<form method="post" class="form-inline">
			<button class="btn btn-success" type="submit" name="action" value="start"><?=gettext('Start')?></button>
			<button class="btn btn-warning" type="submit" name="action" value="restart"><?=gettext('Restart')?></button>
			<button class="btn btn-danger" type="submit" name="action" value="stop"><?=gettext('Stop')?></button>
		</form>
		<hr>
		<table class="table table-striped table-condensed">
			<tbody>
				<tr><th><?=gettext('Enabled')?></th><td id="cloudflared-enabled">-</td></tr>
				<tr><th><?=gettext('State')?></th><td id="cloudflared-state">-</td></tr>
				<tr><th><?=gettext('Token Configured')?></th><td id="cloudflared-token">-</td></tr>
				<tr><th><?=gettext('Protocol')?></th><td id="cloudflared-protocol">-</td></tr>
				<tr><th><?=gettext('Log Level')?></th><td id="cloudflared-loglevel">-</td></tr>
				<tr><th><?=gettext('Version')?></th><td id="cloudflared-version">-</td></tr>
			</tbody>
		</table>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=gettext('Recent Log')?></h2></div>
	<div class="panel-body">
		<pre id="cloudflared-log" style="max-height: 30em; overflow: auto;">-</pre>
	</div>
</div>

<script>
async function refreshCloudflared() {
	const response = await fetch('/status_cloudflared.php?ajax=1', {cache: 'no-store'});
	const data = await response.json();
	document.getElementById('cloudflared-enabled').textContent = data.enabled ? 'Yes' : 'No';
	document.getElementById('cloudflared-state').textContent = data.running ? 'Running' : 'Stopped';
	document.getElementById('cloudflared-token').textContent = data.token_configured ? 'Yes' : 'No';
	document.getElementById('cloudflared-protocol').textContent = data.protocol || 'auto';
	document.getElementById('cloudflared-loglevel').textContent = data.loglevel || 'info';
	document.getElementById('cloudflared-version').textContent = data.version || '-';
	document.getElementById('cloudflared-log').textContent = data.log || 'No readable log output.';
}
refreshCloudflared();
setInterval(refreshCloudflared, 5000);
</script>

<?php include('foot.inc'); ?>
