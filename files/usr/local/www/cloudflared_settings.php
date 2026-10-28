<?php

require_once('guiconfig.inc');
require_once('/usr/local/pkg/cloudflared.inc');
require_once('/usr/local/pkg/cloudflared_update.inc');

$settings = cloudflared_settings();
$repository = config_get_path('installedpackages/cloudflared/updater/repository', CLOUDFLARED_UPDATE_REPOSITORY);
$can_update = cloudflared_can_update();
$input_errors = [];
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
	$previous = $settings;
	foreach (['token', 'protocol', 'metrics', 'loglevel', 'extra_args'] as $field) {
		if (!is_string($_POST[$field] ?? '')) {
			$input_errors[] = gettext('Invalid settings input.');
			break;
		}
		$settings[$field] = trim($_POST[$field] ?? '');
	}
	if ($settings['token'] === DMYPWD) {
		$settings['token'] = $previous['token'];
	}
	$settings['enable'] = isset($_POST['enable']) ? 'on' : '';
	$settings['quic_disable_pmtu'] = isset($_POST['quic_disable_pmtu']) ? 'on' : '';
	if (!$input_errors) {
		cloudflared_validate($settings, $input_errors);
	}
	if (!$input_errors) {
		$lock = null;
		$saved = false;
		try {
			$new_repository = $can_update ? cloudflared_update_repository($_POST['repository'] ?? '') : $repository;
			$lock = cloudflared_update_lock();
			if (cloudflared_update_queued(cloudflared_update_state())) {
				throw new RuntimeException('An update is queued. Wait before changing settings.');
			}
			if (is_subsystem_dirty('pkg') || is_subsystem_dirty('packagelock')) {
				throw new RuntimeException('Package Manager is busy. Wait before changing settings.');
			}
			if (!config_read_file() || cloudflared_settings() !== $previous ||
				config_get_path('installedpackages/cloudflared/updater/repository', CLOUDFLARED_UPDATE_REPOSITORY) !== $repository) {
				throw new RuntimeException('Configuration changed while saving. Reload this page and try again.');
			}
			config_set_path('installedpackages/cloudflared/config/0', $settings);
			if ($can_update) {
				config_set_path('installedpackages/cloudflared/updater/repository', $new_repository);
			}
			write_config('Cloudflared: changed settings');
			$repository = $new_repository;
			if ($settings !== $previous) {
				cloudflared_sync();
			}
			$saved = true;
		} catch (Throwable $error) {
			$input_errors[] = $error->getMessage();
		} finally {
			if (is_resource($lock)) {
				cloudflared_update_unlock($lock);
			}
		}
		// exit does not execute PHP finally blocks; release the lock before redirecting.
		if ($saved) {
			header('Location: /cloudflared_settings.php?saved=1');
			exit;
		}
	}
}

$shortcut_section = 'cloudflared';
$pgtitle = [gettext('Services'), gettext('Cloudflared'), gettext('Settings')];
include('head.inc');
if ($input_errors) {
	print_input_errors($input_errors);
} elseif (isset($_GET['saved'])) {
	print_info_box(gettext('Cloudflared settings saved.'), 'success');
}
cloudflared_tabs('settings');

$form = new Form;
$form->setAction('/cloudflared_settings.php');
$service = new Form_Section('Tunnel');
$service->addInput(new Form_Checkbox('enable', 'Enable', 'Start Cloudflared at boot.', $settings['enable'] === 'on', 'on'));
$service->addPassword(new Form_Input('token', 'Tunnel Token', 'password', $settings['token']), false)
	->setHelp('Cloudflare Tunnel token. Included in pfSense configuration backups.');
$form->add($service);

$advanced = new Form_Section('Advanced', 'cloudflared-advanced', COLLAPSIBLE | ($input_errors ? 0 : SEC_CLOSED));
$advanced->addInput(new Form_Select('protocol', 'Protocol', $settings['protocol'], [
	'auto' => gettext('Auto'), 'quic' => gettext('QUIC'), 'http2' => gettext('HTTP/2'),
]));
$advanced->addInput(new Form_Select('loglevel', 'Log Level', $settings['loglevel'], [
	'error' => gettext('Error'), 'warn' => gettext('Warning'), 'info' => gettext('Info'), 'debug' => gettext('Debug'),
]));
$advanced->addInput(new Form_Input('metrics', 'Metrics Address', 'text', $settings['metrics']))
	->setHelp('Optional. Example: 127.0.0.1:60123.');
$advanced->addInput(new Form_Checkbox('quic_disable_pmtu', 'QUIC PMTU Discovery', 'Disable path MTU discovery.',
	$settings['quic_disable_pmtu'] === 'on', 'on'));
$advanced->addInput(new Form_Input('extra_args', 'Extra Arguments', 'text', $settings['extra_args']))
	->setHelp('Space-separated arguments. Quotes and shell syntax are not supported.');
if ($can_update) {
	$advanced->addInput(new Form_Input('repository', 'Update Repository', 'url', $repository))
		->setHelp('Only use a trusted repository. Packages run installation scripts as root.');
}
$form->add($advanced);
print $form;
include('foot.inc');
