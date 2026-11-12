<?php
// SPDX-License-Identifier: MIT
// Home and Play now share one page. Keeping the fragment out of Location lets
// browsers retain existing download bookmarks.
$query = $_GET ? '?' . http_build_query($_GET) : '';
header('Location: /' . $query, true, 301);
exit;
