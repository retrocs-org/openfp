<?php
// SPDX-License-Identifier: MIT
// Preserve bookmarks and filters under the combined Activity page.
$query = $_GET;
$query['view'] = 'matches';
header('Location: /activity/?' . http_build_query($query), true, 301);
exit;
