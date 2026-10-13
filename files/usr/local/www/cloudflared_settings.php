<?php

require_once('guiconfig.inc');
require_once('/usr/local/pkg/cloudflared.inc');

$settings = array_merge([
	'enable' => '',
	'token' => '',
	'protocol' => 'auto',
	'metrics' => '',
	'loglevel' => 'info',
	'quic_disable_pmtu' => '',
	'extra_args' => '',
], cloudflared_settings());
$input_errors = [];
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
	foreach (['token', 'protocol', 'metrics', 'loglevel', 'extra_args'] as $field) {
		if (!is_string($_POST[$field] ?? '')) {
			$input_errors[] = gettext('Invalid settings input.');
			break;
		}
		$settings[$field] = trim($_POST[$field] ?? '');
	}
	$settings['enable'] = isset($_POST['enable']) ? 'on' : '';
	$settings['quic_disable_pmtu'] = isset($_POST['quic_disable_pmtu']) ? 'on' : '';
	if (!$input_errors) {
		cloudflared_validate($settings, $input_errors);
	}
	if (!$input_errors) {
		config_set_path('installedpackages/cloudflared/config/0', $settings);
		write_config('Cloudflared: changed settings');
		cloudflared_sync();
		header('Location: /cloudflared_settings.php');
		exit;
	}
}

$shortcut_section = 'cloudflared';
$pgtitle = [gettext('Services'), gettext('Cloudflared'), gettext('Settings')];
include('head.inc');
if ($input_errors) {
	print_input_errors($input_errors);
}
$tabs = [
	[gettext('Settings'), true, '/cloudflared_settings.php'],
	[gettext('Status'), false, '/status_cloudflared.php'],
];
display_top_tabs($tabs);

$form = new Form;
$form->setAction('/cloudflared_settings.php');
$service = new Form_Section('Service');
$service->addInput(new Form_Checkbox(
	'enable', 'Enable', 'Start Cloudflared at boot and keep the tunnel service running.',
	$settings['enable'] === 'on', 'on'
));
$service->addInput(new Form_Input('token', 'Tunnel Token', 'password', $settings['token'], [
	'autocomplete' => 'new-password',
]))->setHelp('Cloudflare Tunnel token from Zero Trust. The token is stored in the pfSense configuration and backups, and written locally to /usr/local/etc/cloudflared/token with mode 0600.');
$form->add($service);

$runtime = new Form_Section('Runtime');
$runtime->addInput(new Form_Select('protocol', 'Protocol', $settings['protocol'], [
	'auto' => gettext('Auto'), 'quic' => gettext('QUIC'), 'http2' => gettext('HTTP/2'),
]))->setHelp('Transport protocol. Use http2 if QUIC is unstable on this firewall.');
$runtime->addInput(new Form_Input('metrics', 'Metrics Address', 'text', $settings['metrics']))
	->setHelp('Optional cloudflared metrics listener, for example 127.0.0.1:60123.');
$runtime->addInput(new Form_Select('loglevel', 'Log Level', $settings['loglevel'], [
	'error' => gettext('Error'), 'warn' => gettext('Warning'), 'info' => gettext('Info'), 'debug' => gettext('Debug'),
]))->setHelp('Cloudflared log verbosity.');
$runtime->addInput(new Form_Checkbox(
	'quic_disable_pmtu', 'Disable QUIC PMTU Discovery',
	'Add --quic-disable-pmtu-discovery. Enable only if QUIC is unstable or logs show repeated QUIC path issues.',
	$settings['quic_disable_pmtu'] === 'on', 'on'
));
$runtime->addInput(new Form_Input('extra_args', 'Extra Arguments', 'text', $settings['extra_args']))
	->setHelp('Optional advanced tunnel arguments inserted before run. Avoid secrets here because arguments may appear in process listings.');
$form->add($runtime);
print $form;

include('foot.inc');
