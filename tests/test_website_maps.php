<?php
// SPDX-License-Identifier: MIT
// Offline map catalog checks. SQLite supplies a disposable gallery fixture.
require __DIR__ . '/../public-website/inc/listing.php';
function check_maps(bool $condition, string $message): void {
    if (!$condition) throw new RuntimeException($message);
}
check_maps(ofp_map_modes([], 'classic') === ['classic'], 'Legacy Classic maps use Classic rules');
check_maps(ofp_map_modes(['mode'=>'classic','modes'=>'flag'], 'desert') === ['classic'], 'Custom mode wins over legacy compatibility metadata');
check_maps(ofp_map_modes(['mode'=>'hardpoint','modes'=>'conquest'], 'winter') === ['hardpoint'], 'Custom maps keep their specific game mode');
check_maps(ofp_map_modes(['modes'=>'deathmatch,warlord'], 'volcanic') === ['deathmatch','killconfirmed','warlord'], 'Legacy arena compatibility');
check_maps(ofp_map_modes(['modes'=>'conquest'], 'desert') === ['conquest','hardpoint'], 'Hardpoint uses legacy Conquest checkpoints');
check_maps(ofp_map_modes(['modes'=>'notflag,unknown'], 'desert') === [], 'Unknown IDs are not substring matches');
check_maps(ofp_map_modes(['arena'=>'yes'], 'desert') === ['annihilation'], 'Legacy arena fallback');
check_maps(!in_array('annihilation', ofp_map_modes([], 'desert'), true), 'Maps without arenas retain team modes');
$header = "OFPMAP 1\r\nmode=flag\r\nmodes=flag,conquest\r\narena=yes\r\ntiles\r\nmode=warlord\r\n";
$details = ofp_map_details($header);
check_maps($details['mode'] === 'flag' && $details['modes'] === 'flag,conquest' && $details['arena'] === 'yes', 'Read mode metadata only before tiles');
$catalog = json_decode(file_get_contents(__DIR__.'/../public-website/assets/bundled-maps.json'), true, 512, JSON_THROW_ON_ERROR);
$paths = array_merge(glob(__DIR__.'/../data/maps/*.ofpmap'), glob(__DIR__.'/../data/maps/classic/*.ofpmap'));
$expected = [];
foreach ($paths as $path) {
    $fields = ofp_map_details(file_get_contents($path));
    if ($fields['biome'] === 'scifi') continue;
    $expected[$fields['biome'].':'.$fields['name']] = ofp_map_modes($fields, $fields['biome']);
}
check_maps(count($expected) === count($catalog), 'Every public bundled map is listed');
foreach ($catalog as $map) {
    check_maps($map['modes'] === $expected[$map['world'].':'.$map['name']], 'Generated metadata matches PHP filtering: '.$map['name']);
    check_maps(count($map['modes']) > 0, 'Each bundled map has a game mode');
}
if (!in_array('sqlite', PDO::getAvailableDrivers(), true)) {
    echo "Map metadata checks passed; SQLite gallery fixture skipped (pdo_sqlite unavailable).\n";
    exit;
}
$pdoClass = class_exists('Pdo\Sqlite') ? 'Pdo\Sqlite' : PDO::class;
$db = new $pdoClass('sqlite::memory:', null, null, [PDO::ATTR_ERRMODE=>PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE=>PDO::FETCH_ASSOC]);
$createFunction = method_exists($db, 'createFunction') ? 'createFunction' : 'sqliteCreateFunction';
$db->$createFunction('SUBSTRING_INDEX', static function($text, $delimiter, $count) {
    return implode($delimiter, array_slice(explode($delimiter, $text), 0, $count));
}, 3);
$db->exec('CREATE TABLE gallery(id INTEGER PRIMARY KEY, name TEXT, author TEXT, world TEXT, time TEXT, size INTEGER, uploaded INTEGER, downloads INTEGER, text TEXT, hidden INTEGER)');
$q = $db->prepare('INSERT INTO gallery VALUES(?,?,?,?,?,?,?,?,?,?)');
for ($id=1; $id<=310; $id++) {
    $isTarget = $id<=5;
    $text = "OFPMAP 1\nname=Map $id\nauthor=Map author\nmode=".($isTarget?'hardpoint':'flag')."\ntiles\n....\n";
    $q->execute([$id,"Map $id",'Map author','desert','day',strlen($text),$id,0,$text,0]);
}
$q->execute([311,'Hidden','Author','desert','day',1,311,0,"OFPMAP 1\nmode=hardpoint\ntiles\n.",1]);
$q->execute([312,'Other world','Author','winter','day',1,312,0,"OFPMAP 1\nmode=hardpoint\ntiles\n.",0]);
$rows = ofp_gallery_maps('desert', 'hardpoint', 3, $db);
check_maps(array_column($rows, 'id') === [5,4,3], 'Find older matches beyond the first database batch, then limit');
check_maps(count(ofp_gallery_maps('', 'hardpoint', 200, $db)) === 6, 'Mode filter spans worlds and hides moderated maps');
check_maps(count(ofp_gallery_maps('desert', 'conquest', 200, $db)) === 0, 'Custom Hardpoint map is not mislabelled Conquest');
check_maps(count(ofp_gallery_maps('desert', '', 200, $db)) === 200, 'World-only result limit');
check_maps(!isset($rows[0]['map_header']) && $rows[0]['modes'] === ['hardpoint'], 'Only public metadata is returned');
echo 'Map metadata and gallery filters passed ('.count($catalog)." bundled maps).\n";
