<?php

require_once('guiconfig.inc');
require_once('/usr/local/pkg/cloudflared.inc');
require_once('/usr/local/pkg/cloudflared_update.inc');

$can_update = cloudflared_can_update();
$input_errors = [];
$automatic = $_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['autocheck']);
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
	try {
		if (!$can_update) {
			http_response_code(403);
			throw new RuntimeException('The Cloudflared Update privilege is required.');
		}
		if (isset($_POST['install']) === $automatic) {
			throw new RuntimeException('Invalid update action.');
		}
		$repository = config_get_path('installedpackages/cloudflared/updater/repository', CLOUDFLARED_UPDATE_REPOSITORY);
		try {
			cloudflared_update_launch($automatic ? 'check' : 'install', $repository, $_POST['check_id'] ?? '', $automatic);
		} catch (RuntimeException $error) {
			if (!$automatic || $error->getCode() !== 409) {
				throw $error;
			}
		}
		if (!$automatic) {
			header('Location: /status_cloudflared.php');
			exit;
		}
	} catch (Throwable $error) {
		$input_errors[] = $error->getMessage();
	}
}

if (isset($_GET['ajax']) || $automatic) {
	header('Content-Type: application/json');
	header('Cache-Control: no-store');
	try {
		$update = cloudflared_update_summary($can_update);
		if ($input_errors) {
			$update['status'] = 'error';
			$update['message'] = implode(' ', $input_errors);
		}
	} catch (Throwable $error) {
		$update = ['status' => 'error', 'message' => $error->getMessage(), 'busy' => false, 'can_update' => $can_update];
	}
	echo json_encode(['runtime' => cloudflared_runtime_status(), 'log' => cloudflared_recent_log(),
		'update' => $update, 'update_log' => $can_update && $update['status'] === 'error' ? cloudflared_recent_log(CLOUDFLARED_UPDATE_LOG) : ''], JSON_INVALID_UTF8_SUBSTITUTE);
	exit;
}

$shortcut_section = 'cloudflared';
$pgtitle = [gettext('Services'), gettext('Cloudflared'), gettext('Status')];
include('head.inc');
if ($input_errors) {
	print_input_errors($input_errors);
}
cloudflared_tabs('status');
$install = new Form_Button('install', 'Update', null, 'fa-solid fa-download');
$install->setAttribute('id', 'update-install')->setAttribute('disabled', true)->addClass('btn-primary');
$form = new Form($install);
$form->setAction('/status_cloudflared.php');
$form->setAttribute('id', 'cloudflared-update-form');
$service = new Form_Section('Tunnel');
$service->addInput(new Form_StaticText('Service', '<span id="cloudflared-state">Checking…</span>'));
$service->addInput(new Form_StaticText('Edge Connections', '<span id="cloudflared-connection">Checking…</span>'));
$form->add($service);

$package = new Form_Section('Package Update');
$package->addInput(new Form_StaticText('Installed Version', '<span id="update-installed">' .
	htmlspecialchars(cloudflared_binary_version(), ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8') . '</span>'));
$package->addInput(new Form_StaticText('Status',
	'<span id="update-status" role="status" aria-live="polite">Checking…</span>' .
	($can_update ? '<details id="update-error-details" class="hidden"><summary>' . gettext('Error details') . '</summary>' .
	'<pre id="cloudflared-update-log" style="max-height: 10em; overflow: auto;"></pre></details>' : '')));
$form->addGlobal(new Form_Input('check_id', null, 'hidden', ''));
$form->add($package);
print $form;
?>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=gettext('Recent Tunnel Log')?></h2></div>
	<div class="panel-body"><pre id="cloudflared-log" style="max-height: 24em; overflow: auto;">—</pre></div>
</div>

<script>
events.push(function () {
	let updateState = {};
	const install = document.getElementById('update-install');
	const form = document.getElementById('cloudflared-update-form');
	form.addEventListener('submit', function (event) {
		if (event.submitter !== install || install.disabled || !updateState.check_id) {
			event.preventDefault();
		}
	});
	function render(data) {
		document.getElementById('cloudflared-state').textContent = data.runtime.service;
		document.getElementById('cloudflared-connection').textContent = data.runtime.connection;
		document.getElementById('cloudflared-log').textContent = data.log || 'No readable tunnel log output.';
		updateState = data.update;
		document.getElementById('update-status').textContent = updateState.message;
		document.getElementById('check_id').value = updateState.check_id || '';
		install.disabled = !updateState.can_update || updateState.busy || updateState.status !== 'available';
		const diagnostics = document.getElementById('cloudflared-update-log');
		if (diagnostics) {
			diagnostics.textContent = data.update_log || '';
			document.getElementById('update-error-details').classList.toggle('hidden', updateState.status !== 'error' || !data.update_log);
		}
	}
	async function refreshCloudflared(automatic = false) {
		try {
			const options = {cache: 'no-store', signal: AbortSignal.timeout(20000)};
			if (automatic) {
				options.method = 'POST';
				options.body = new FormData(form);
				options.body.set('autocheck', '1');
			}
			const response = await fetch('/status_cloudflared.php?ajax=1', options);
			if (!response.ok) throw new Error('Cannot read status.');
			render(await response.json());
		} catch (error) {
			document.getElementById('cloudflared-state').textContent = 'Status unavailable';
			document.getElementById('cloudflared-connection').textContent = 'Unknown';
			document.getElementById('update-status').textContent = 'Cannot read status. Reload or sign in again.';
			install.disabled = true;
		} finally {
			setTimeout(refreshCloudflared, updateState.busy ? 2000 : 5000);
		}
	}
	refreshCloudflared(<?= $can_update ? 'true' : 'false' ?>);
});
</script>
<?php include('foot.inc'); ?>
