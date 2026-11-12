<?php
// SPDX-License-Identifier: MIT
$query = $_GET ? '?' . http_build_query($_GET) : '';
header('Location: /statistics/' . $query, true, 301);
exit;
