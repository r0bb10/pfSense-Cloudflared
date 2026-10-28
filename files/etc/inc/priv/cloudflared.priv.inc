<?php

$priv_list['page-services-cloudflared'] = [];
$priv_list['page-services-cloudflared']['name'] = 'WebCfg - Services: Cloudflared';
$priv_list['page-services-cloudflared']['descr'] = 'Allow access to Cloudflared settings and status';
$priv_list['page-services-cloudflared']['match'] = [];
$priv_list['page-services-cloudflared']['match'][] = 'cloudflared_settings.php*';
$priv_list['page-services-cloudflared']['match'][] = 'status_cloudflared.php*';

$priv_list['page-services-cloudflared-update'] = [];
$priv_list['page-services-cloudflared-update']['name'] = 'WebCfg - Services: Cloudflared: Update';
$priv_list['page-services-cloudflared-update']['descr'] = 'Install Cloudflared packages from GitHub (executes package scripts as root)';
$priv_list['page-services-cloudflared-update']['match'] = ['cloudflared-update', 'status_cloudflared.php*'];
$priv_list['page-services-cloudflared-update']['warn'] = 'standard-warning-root';

?>
