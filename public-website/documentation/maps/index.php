<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../../inc/documentation.php';
documentation_start('maps');
?>
<h2 id="start">Start with a blank map</h2>
<p>Map Editor opens an empty Large map in the Classic world. Escape opens the editor menu: File contains Open, Save, Random Map and Erase Map; Options contains world, weather, time, size, game mode and player count. Network contains Upload Map to Gallery.</p>
<h2 id="tools">Choose objects and move around</h2>
<p>The sidebar separates Ground, Roads, Nature and Base. Use Tab or the category arrows to switch lists, then scroll the objects below. Player 1's directions and Fire move and paint. Right-drag pans; right-click erases.</p>
<p>Map opens a miniature overview. Click a location or move its marker with the controls and press Fire to jump there. Set Area opens the team-area editor. The Time and Weather buttons toggle previews; Details opens the map's title, description, author and optional contact information.</p>
<h2 id="bases">Build bases from individual objects</h2>
<p>Use Set Area to draw separate Team 1 and Team 2 areas. Drag inside an area to move it, or drag an edge or corner to resize it. Place walls, gates, garages and other base objects individually. Their team follows the area containing the object's footprint. Guns outside both team areas attack both teams.</p>
<p>Place one garage per player, split evenly between the teams. Each garage and its exit must fit inside its own team's area. Flag buildings are available for Classic and Capture the Flag.</p>
<h2 id="options">Map size and rules</h2>
<div class="documentation-table"><table><thead><tr><th>Size</th><th>Players</th></tr></thead><tbody><tr><td>Small</td><td>2</td></tr><tr><td>Medium</td><td>2 or 4</td></tr><tr><td>Large</td><td>4 or 6</td></tr><tr><td>Huge</td><td>8</td></tr></tbody></table></div>
<p>A new Large map defaults to four players. AmigaOS can create larger network maps, but its local test uses two tanks.</p>
<p>Classic worlds and Classic rules disable weather and newer gameplay features. Enhanced worlds offer appropriate weather: Winter has snow and snowstorms, for example. The world also determines terrain: Moon Base uses dry crater terrain; Winter can have water and slippery ice.</p>
<p>Resizing erases the draft after confirmation. World, weather and time changes preserve the layout. Save a copy before making major changes.</p>
<h2 id="check">Check and test your map</h2>
<p>Run the playability check before saving, testing or sharing. It checks team areas, required garages and routes between bases, along with objectives needed by the selected mode. Fix errors before continuing and review warnings about routes that depend on destroying obstacles.</p>
<p>Test Run Map launches the draft. Return to the editor to adjust it, then save the finished map.</p>
<h2 id="sharing">Save, recover and share</h2>
<p>Both editors keep rotating recovery drafts while you work. After an interruption, reopening the editor offers Restore Draft or Discard Draft. A recovered draft is unsaved; it does not replace a manually saved map.</p>
<p>File → Save stores an <code>.ofpmap</code> file. Network → Upload Map to Gallery publishes the map and the information entered under Details. Only include contact information you want to make public. The <a href="/maps/">map browser</a> lists bundled and community maps with world and game-mode filters.</p>
<p>For map-file fields and custom objectives, see the <a href="https://github.com/retrocs-org/openfp/blob/main/docs/MAPS.md">map format reference</a>.</p>
<?php documentation_end();
