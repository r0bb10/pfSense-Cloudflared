<?php

require_once('guiconfig.inc');
require_once('service-utils.inc');
require_once('util.inc');

if (isset($_POST['widgetkey']) || isset($_GET['widgetkey'])) {
	$requested_widgetkey = $_POST['widgetkey'] ?? $_GET['widgetkey'];
	[$widget_name, $widget_id] = array_pad(explode('-', $requested_widgetkey, 2), 2, null);
	if ($widget_name === basename(__FILE__, '.widget.php') && is_numericint($widget_id)) {
		$widgetkey = $requested_widgetkey;
	} else {
		print gettext('Invalid Widget Key');
		exit;
	}
}

if (!isset($widgetkey)) {
	print gettext('Missing Widget Key');
	exit;
}

function cloudflared_widget_escape($value) {
	return htmlspecialchars((string)$value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function cloudflared_widget_body() {
	$running = is_service_running('cloudflared');
	$service_icon = $running ? 'fa-arrow-up text-success' : 'fa-arrow-down text-danger';
	$service_label = $running ? gettext('Running') : gettext('Stopped');
	$version = is_executable('/usr/local/bin/cloudflared') ? trim(shell_exec('/usr/local/bin/cloudflared version 2>&1')) : gettext('Missing binary');
	$html = '';
	$html .= '<tr><th>' . cloudflared_widget_escape(gettext('Service')) . '</th><td><i class="fa-solid ' . $service_icon . '"></i> ' . cloudflared_widget_escape($service_label) . '</td></tr>';
	$html .= '<tr><th>' . cloudflared_widget_escape(gettext('Version')) . '</th><td>' . cloudflared_widget_escape($version) . '</td></tr>';
	return $html;
}

if (isset($_POST['ajax'])) {
	print cloudflared_widget_body();
	exit;
}

?>
<div class="table-responsive">
	<table class="table table-striped table-hover table-condensed">
		<tbody id="<?=cloudflared_widget_escape($widgetkey)?>">
			<?=cloudflared_widget_body()?>
		</tbody>
	</table>
</div>
<div class="text-right">
	<a href="/status_cloudflared.php"><?=gettext('Full status')?></a>
</div>

<script type="text/javascript">
events.push(function() {
	function cloudflaredCallback(response) {
		$(<?=json_encode('#' . $widgetkey)?>).html(response);
	}

	var refreshObject = new Object();
	refreshObject.name = 'cloudflared';
	refreshObject.url = '/widgets/widgets/cloudflared.widget.php';
	refreshObject.callback = cloudflaredCallback;
	refreshObject.parms = {
		ajax: 'ajax',
		widgetkey: <?=json_encode($widgetkey)?>
	};
	refreshObject.freq = 5;
	register_ajax(refreshObject);
});
</script>
