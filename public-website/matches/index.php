<?php
// SPDX-License-Identifier: MIT
// Preserve bookmarks and filters under the combined Statistics page.
$query = $_GET;
$query['view'] = 'matches';
header('Location: /statistics/?' . http_build_query($query), true, 301);
exit;
