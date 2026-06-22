<?php

$priv_list['page-services-cloudflared'] = [];
$priv_list['page-services-cloudflared']['name'] = 'WebCfg - Services: Cloudflared';
$priv_list['page-services-cloudflared']['descr'] = 'Allow access to Cloudflared settings and status';
$priv_list['page-services-cloudflared']['match'] = [];
$priv_list['page-services-cloudflared']['match'][] = 'pkg_edit.php?xml=cloudflared.xml*';
$priv_list['page-services-cloudflared']['match'][] = 'status_cloudflared.php*';

?>
