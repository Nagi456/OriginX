# 共有コードの番号（FieldIds）

設計書：`docs/config-sharing-design.md` §3。この表と `Registry.h` は同じでなければならない（`tests/config_share.cpp` の `registry_matches_field_ids_md` が確かめる）。

## 決まり

- **廃止した番号は欠番にして、使い回さない。** 番号は共有コードの形式そのものなので、変えると前に配ったコードが読めなくなる。
- 欠番は下の「欠番」の表に、版と理由を書いて残す。
- 範囲：1〜99 Modules、100〜299 HUD（`100 + HudElementId×4 + {0:x, 1:y, 2:scale}`、+3 は空き）、300〜399 Theme、400〜499 Lite、500 以降は区切り B 以降。
- 番号は 1〜16383 に収める。
- HUD の x と y は画面に対する割合 ×10000、scale は ×100。
- step は「min + step の倍数」に丸めることを表す。

## 欠番

| 番号 | 版 | 理由 |
|---|---|---|
| 6 | schema 1 | PartyAutoAccept は共有に含めない（サーバーの動きに関わる自動化。AutoRq と同じ扱い） |

廃止した番号：まだ無い。

## 項目（133 個）

| 番号 | key | カテゴリ | min | max | step |
|---|---|---|---|---|---|
| 1 | `mod.keystrokes` | Modules | 0 | 1 | 1 |
| 2 | `mod.cps` | Modules | 0 | 1 | 1 |
| 3 | `mod.armor` | Modules | 0 | 1 | 1 |
| 4 | `mod.arrows` | Modules | 0 | 1 | 1 |
| 5 | `mod.fps` | Modules | 0 | 1 | 1 |
| 7 | `mod.block_outline` | Modules | 0 | 1 | 1 |
| 8 | `mod.zoom` | Modules | 0 | 1 | 1 |
| 9 | `mod.resources` | Modules | 0 | 1 | 1 |
| 10 | `mod.fullbright` | Modules | 0 | 1 | 1 |
| 11 | `mod.nametag` | Modules | 0 | 1 | 1 |
| 12 | `mod.ping` | Modules | 0 | 1 | 1 |
| 13 | `mod.textures` | Modules | 0 | 1 | 1 |
| 14 | `mod.feature_list` | Modules | 0 | 1 | 1 |
| 15 | `mod.shulker` | Modules | 0 | 1 | 1 |
| 16 | `mod.hive` | Modules | 0 | 1 | 1 |
| 17 | `mod.tab_list` | Modules | 0 | 1 | 1 |
| 18 | `mod.hive_stats` | Modules | 0 | 1 | 1 |
| 19 | `mod.chunk_border` | Modules | 0 | 1 | 1 |
| 20 | `mod.hitbox` | Modules | 0 | 1 | 1 |
| 21 | `mod.nick` | Modules | 0 | 1 | 1 |
| 22 | `mod.replay` | Modules | 0 | 1 | 1 |
| 23 | `mod.menu_sounds` | Modules | 0 | 1 | 1 |
| 24 | `mod.lite` | Modules | 0 | 1 | 1 |
| 25 | `mod.fast_inventory` | Modules | 0 | 1 | 1 |
| 26 | `mod.left_hand` | Modules | 0 | 1 | 1 |
| 27 | `mod.hit_effect` | Modules | 0 | 1 | 1 |
| 28 | `mod.frame_gen` | Modules | 0 | 1 | 1 |
| 29 | `mod.day_timer` | Modules | 0 | 1 | 1 |
| 30 | `mod.debug_menu` | Modules | 0 | 1 | 1 |
| 31 | `mod.block_details` | Modules | 0 | 1 | 1 |
| 32 | `mod.health_warning` | Modules | 0 | 1 | 1 |
| 33 | `mod.coordinates` | Modules | 0 | 1 | 1 |
| 34 | `mod.timer` | Modules | 0 | 1 | 1 |
| 35 | `mod.cps_limiter` | Modules | 0 | 1 | 1 |
| 36 | `mod.item_counter` | Modules | 0 | 1 | 1 |
| 37 | `mod.pack_display` | Modules | 0 | 1 | 1 |
| 38 | `mod.java_chat` | Modules | 0 | 1 | 1 |
| 39 | `mod.minimap` | Modules | 0 | 1 | 1 |
| 40 | `mod.combo` | Modules | 0 | 1 | 1 |
| 104 | `hud.keystrokes.x` | Hud | 0 | 10000 | 1 |
| 105 | `hud.keystrokes.y` | Hud | 0 | 10000 | 1 |
| 106 | `hud.keystrokes.scale` | Hud | 50 | 300 | 1 |
| 108 | `hud.cps.x` | Hud | 0 | 10000 | 1 |
| 109 | `hud.cps.y` | Hud | 0 | 10000 | 1 |
| 110 | `hud.cps.scale` | Hud | 50 | 300 | 1 |
| 112 | `hud.armor.x` | Hud | 0 | 10000 | 1 |
| 113 | `hud.armor.y` | Hud | 0 | 10000 | 1 |
| 114 | `hud.armor.scale` | Hud | 50 | 300 | 1 |
| 116 | `hud.arrows.x` | Hud | 0 | 10000 | 1 |
| 117 | `hud.arrows.y` | Hud | 0 | 10000 | 1 |
| 118 | `hud.arrows.scale` | Hud | 50 | 300 | 1 |
| 120 | `hud.fps.x` | Hud | 0 | 10000 | 1 |
| 121 | `hud.fps.y` | Hud | 0 | 10000 | 1 |
| 122 | `hud.fps.scale` | Hud | 50 | 300 | 1 |
| 124 | `hud.resources.x` | Hud | 0 | 10000 | 1 |
| 125 | `hud.resources.y` | Hud | 0 | 10000 | 1 |
| 126 | `hud.resources.scale` | Hud | 50 | 300 | 1 |
| 128 | `hud.ping.x` | Hud | 0 | 10000 | 1 |
| 129 | `hud.ping.y` | Hud | 0 | 10000 | 1 |
| 130 | `hud.ping.scale` | Hud | 50 | 300 | 1 |
| 132 | `hud.feature_list.x` | Hud | 0 | 10000 | 1 |
| 133 | `hud.feature_list.y` | Hud | 0 | 10000 | 1 |
| 134 | `hud.feature_list.scale` | Hud | 50 | 300 | 1 |
| 136 | `hud.shulker.x` | Hud | 0 | 10000 | 1 |
| 137 | `hud.shulker.y` | Hud | 0 | 10000 | 1 |
| 138 | `hud.shulker.scale` | Hud | 50 | 300 | 1 |
| 140 | `hud.tab_list.x` | Hud | 0 | 10000 | 1 |
| 141 | `hud.tab_list.y` | Hud | 0 | 10000 | 1 |
| 142 | `hud.tab_list.scale` | Hud | 50 | 300 | 1 |
| 144 | `hud.hive_stats.x` | Hud | 0 | 10000 | 1 |
| 145 | `hud.hive_stats.y` | Hud | 0 | 10000 | 1 |
| 146 | `hud.hive_stats.scale` | Hud | 50 | 300 | 1 |
| 148 | `hud.day_timer.x` | Hud | 0 | 10000 | 1 |
| 149 | `hud.day_timer.y` | Hud | 0 | 10000 | 1 |
| 150 | `hud.day_timer.scale` | Hud | 50 | 300 | 1 |
| 152 | `hud.block_details.x` | Hud | 0 | 10000 | 1 |
| 153 | `hud.block_details.y` | Hud | 0 | 10000 | 1 |
| 154 | `hud.block_details.scale` | Hud | 50 | 300 | 1 |
| 156 | `hud.coordinates.x` | Hud | 0 | 10000 | 1 |
| 157 | `hud.coordinates.y` | Hud | 0 | 10000 | 1 |
| 158 | `hud.coordinates.scale` | Hud | 50 | 300 | 1 |
| 160 | `hud.timer.x` | Hud | 0 | 10000 | 1 |
| 161 | `hud.timer.y` | Hud | 0 | 10000 | 1 |
| 162 | `hud.timer.scale` | Hud | 50 | 300 | 1 |
| 164 | `hud.pack_display.x` | Hud | 0 | 10000 | 1 |
| 165 | `hud.pack_display.y` | Hud | 0 | 10000 | 1 |
| 166 | `hud.pack_display.scale` | Hud | 50 | 300 | 1 |
| 168 | `hud.java_chat.x` | Hud | 0 | 10000 | 1 |
| 169 | `hud.java_chat.y` | Hud | 0 | 10000 | 1 |
| 170 | `hud.java_chat.scale` | Hud | 50 | 300 | 1 |
| 172 | `hud.minimap.x` | Hud | 0 | 10000 | 1 |
| 173 | `hud.minimap.y` | Hud | 0 | 10000 | 1 |
| 174 | `hud.minimap.scale` | Hud | 50 | 300 | 1 |
| 176 | `hud.combo.x` | Hud | 0 | 10000 | 1 |
| 177 | `hud.combo.y` | Hud | 0 | 10000 | 1 |
| 178 | `hud.combo.scale` | Hud | 50 | 300 | 1 |
| 300 | `theme.dark` | Theme | 0 | 1 | 1 |
| 301 | `theme.accent` | Theme | 0 | 7 | 1 |
| 302 | `theme.light` | Theme | 0 | 6 | 1 |
| 303 | `theme.dark_tint` | Theme | 0 | 7 | 1 |
| 304 | `theme.shadow` | Theme | 0 | 100 | 1 |
| 305 | `theme.opacity` | Theme | 20 | 100 | 1 |
| 306 | `theme.custom0.h` | Theme | 0 | 359 | 1 |
| 307 | `theme.custom0.s` | Theme | 0 | 100 | 1 |
| 308 | `theme.custom0.v` | Theme | 0 | 100 | 1 |
| 309 | `theme.custom1.h` | Theme | 0 | 359 | 1 |
| 310 | `theme.custom1.s` | Theme | 0 | 100 | 1 |
| 311 | `theme.custom1.v` | Theme | 0 | 100 | 1 |
| 312 | `theme.custom2.h` | Theme | 0 | 359 | 1 |
| 313 | `theme.custom2.s` | Theme | 0 | 100 | 1 |
| 314 | `theme.custom2.v` | Theme | 0 | 100 | 1 |
| 400 | `lite.clouds` | Lite | 0 | 1 | 1 |
| 401 | `lite.sky` | Lite | 0 | 1 | 1 |
| 402 | `lite.fog` | Lite | 0 | 1 | 1 |
| 403 | `lite.weather` | Lite | 0 | 1 | 1 |
| 404 | `lite.no_shadows` | Lite | 0 | 1 | 1 |
| 405 | `lite.no_particles` | Lite | 0 | 1 | 1 |
| 406 | `lite.no_crit` | Lite | 0 | 1 | 1 |
| 407 | `lite.no_potion` | Lite | 0 | 1 | 1 |
| 408 | `lite.entity_limit` | Lite | 0 | 1 | 1 |
| 409 | `lite.entity_distance` | Lite | 8 | 128 | 8 |
| 410 | `lite.cull_mobs` | Lite | 0 | 1 | 1 |
| 411 | `lite.cull_items` | Lite | 0 | 1 | 1 |
| 412 | `lite.cull_misc` | Lite | 0 | 1 | 1 |
| 413 | `lite.player_limit` | Lite | 0 | 1 | 1 |
| 414 | `lite.player_distance` | Lite | 8 | 128 | 8 |
| 415 | `lite.no_name_tags` | Lite | 0 | 1 | 1 |
| 416 | `lite.no_sky_full` | Lite | 0 | 1 | 1 |
| 417 | `lite.cull_behind` | Lite | 0 | 1 | 1 |
| 418 | `lite.block_entity_limit` | Lite | 0 | 1 | 1 |
| 419 | `lite.block_entity_distance` | Lite | 8 | 128 | 8 |
| 420 | `lite.vertical_limit` | Lite | 0 | 1 | 1 |
| 421 | `lite.vertical_rows` | Lite | 1 | 8 | 1 |
| 422 | `lite.render` | Lite | 0 | 1 | 1 |
| 423 | `lite.render_distance` | Lite | 4 | 48 | 2 |
| 424 | `lite.fps_limit` | Lite | 0 | 1 | 1 |
| 425 | `lite.fps_limit_code` | Lite | 1 | 6 | 1 |
