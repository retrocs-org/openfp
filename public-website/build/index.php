<?php
// SPDX-License-Identifier: MIT
header('Location: /source/' . ($_GET ? '?' . http_build_query($_GET) : ''), true, 301);
exit;
