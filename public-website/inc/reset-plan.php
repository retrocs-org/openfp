<?php
// SPDX-License-Identifier: MIT
function ofp_reset_tables(): array {
    return ['account_results','account_tickets','account_sessions','account_resets','account_devices','account_verifications','account_limits','accounts',
        'replays','live','matches','leaders','daily_queue','daily','gallery','punch','samples','hits','servers'];
}
function ofp_reset_data(PDO $db): void {
    $db->beginTransaction();
    try { foreach (ofp_reset_tables() as $t) $db->exec("DELETE FROM $t"); $db->commit(); }
    catch (Throwable $e) { $db->rollBack(); throw $e; }
}
