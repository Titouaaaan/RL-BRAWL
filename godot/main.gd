extends Node2D
## Draws the C++ simulation. All game logic lives in core/; this script only
## reads the keyboard, calls step() once per frame, and draws the result.

const COLORS := [Color(0.35, 0.6, 1.0), Color(1.0, 0.45, 0.35)]
# Same order as the enums in core/sim.h.
const STATE_NAMES := ["IDLE", "WALK", "JUMPSQUAT", "AIRBORNE", "LANDING", "ATTACK", "HITSTUN"]
const MOVE_NAMES := ["jab", "heavy", "aerial"]
const ST_HITSTUN := 6

# Keys are *physical* positions (US layout names), so on AZERTY "WASD" is ZQSD.
# Player 1: WASD stick, Space jump, J light, K heavy.
# Player 2: arrow keys stick, Enter jump, the two keys left of right Shift
#           (. and / on US, : and ! on AZERTY) light and heavy.
const KEYS := [
	{left = KEY_A, right = KEY_D, down = KEY_S, up = KEY_W, jump = KEY_SPACE, light = KEY_J, heavy = KEY_K},
	{left = KEY_LEFT, right = KEY_RIGHT, down = KEY_DOWN, up = KEY_UP, jump = KEY_ENTER, light = KEY_PERIOD, heavy = KEY_SLASH},
]

var game := FightGame.new()
var cfg: Dictionary
var load_error := ""
# Camera: follows both fighters and zooms out as they get further apart.
var cam_center := Vector2(0, 5)  # sim coordinates
var px := 14.0  # zoom: pixels per sim unit


func _ready() -> void:
	reload()


# Re-reads data/moves/*.json and restarts. Bound to R, so you can edit a move
# file, press R, and try the change without restarting Godot.
func reload() -> void:
	var dir := ProjectSettings.globalize_path("res://").path_join("../data/moves").simplify_path()
	load_error = game.load_moves(dir)
	cfg = game.get_config()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.physical_keycode == KEY_R:
			reload()
		elif event.physical_keycode == KEY_ESCAPE:
			get_tree().quit()


func _physics_process(_delta: float) -> void:
	# Runs exactly 60 times per second: one call = one sim frame.
	var a := read_input(KEYS[0])
	var b := read_input(KEYS[1])
	game.step(a[0], a[1], a[2], b[0], b[1], b[2])
	update_camera()
	queue_redraw()  # ask Godot to call _draw() again


func update_camera() -> void:
	var a: Dictionary = game.get_fighter(0)
	var b: Dictionary = game.get_fighter(1)
	var lo := Vector2(min(a.x, b.x), min(a.y, b.y))
	var hi := Vector2(max(a.x, b.x), max(a.y, b.y) + cfg.hurtbox_h)
	# Half the visible area, in sim units: both fighters plus a margin, but
	# never closer than about the stage width (56 units).
	var half := Vector2(max((hi.x - lo.x) / 2 + 12, 28), max((hi.y - lo.y) / 2 + 8, 15))
	var size := get_viewport_rect().size
	var target_px: float = min(size.x / 2 / half.x, size.y / 2 / half.y)
	var target := (lo + hi) / 2 + Vector2(0, 3)  # a bit above the fighters, so the floor sits low
	# Move 10% of the way each frame: smooth instead of jerky.
	cam_center = cam_center.lerp(target, 0.1)
	px = lerp(px, target_px, 0.1)


func read_input(k: Dictionary) -> Array:
	var sx := int(held(k.right)) - int(held(k.left))
	var sy := int(held(k.up)) - int(held(k.down))
	var button := 0  # none; matches the Button enum in core/sim.h
	if held(k.jump): button = 1
	elif held(k.light): button = 2
	elif held(k.heavy): button = 3
	return [sx, sy, button]


func held(key: Key) -> bool:
	return Input.is_physical_key_pressed(key)


# Sim coordinates (y up) -> screen pixels (y down), through the camera.
func to_screen(x: float, y: float) -> Vector2:
	return get_viewport_rect().size / 2 + Vector2(x - cam_center.x, cam_center.y - y) * px


func _draw() -> void:
	var font := ThemeDB.fallback_font
	var w: float = cfg.stage_half_width

	# Blast zones (leave this rectangle = lose a stock) and the stage.
	var bz_tl := to_screen(-cfg.blast_x, cfg.blast_top)
	var bz_br := to_screen(cfg.blast_x, cfg.blast_bottom)
	draw_rect(Rect2(bz_tl, bz_br - bz_tl), Color(1, 1, 1, 0.15), false, 2.0)
	var stage_tl := to_screen(-w, 0)
	draw_rect(Rect2(stage_tl, to_screen(w, -2) - stage_tl), Color(0.45, 0.45, 0.5))

	for i in 2:
		draw_fighter(i, game.get_fighter(i), font)

	# Hitboxes: translucent circles in the attacker's colour with a red rim.
	# "Spent" ones (the move already hit) are drawn faded; they can't hit again.
	for hb in game.get_hitboxes():
		var c := to_screen(hb.x, hb.y)
		var alpha := 0.15 if hb.spent else 0.45
		draw_circle(c, hb.radius * px, Color(COLORS[hb.owner], alpha))
		draw_arc(c, hb.radius * px, 0, TAU, 32, Color(1, 0.1, 0.1, alpha * 2), 2.0)

	draw_hud(font)


func draw_fighter(i: int, f: Dictionary, font: Font) -> void:
	var hw: float = cfg.hurtbox_w / 2
	var top_left := to_screen(f.x - hw, f.y + cfg.hurtbox_h)
	var rect := Rect2(top_left, Vector2(cfg.hurtbox_w, cfg.hurtbox_h) * px)
	var color: Color = COLORS[i]
	if f.state == ST_HITSTUN:
		color = color.lerp(Color.WHITE, 0.6)  # flash pale while stunned
	if f.invuln > 0 and (f.invuln / 6) % 2 == 0:
		color.a = 0.35  # blink while invulnerable after respawning
	draw_rect(rect, color)
	# White "eye" on the side the fighter is facing.
	draw_circle(to_screen(f.x + f.facing * hw * 0.6, f.y + cfg.hurtbox_h * 0.8), px * 0.25, Color.WHITE)
	# Small player number above the head.
	draw_string(font, to_screen(f.x - 0.5, f.y + cfg.hurtbox_h + 0.5), "P%d" % (i + 1), HORIZONTAL_ALIGNMENT_LEFT, -1, 14, COLORS[i])


func draw_hud(font: Font) -> void:
	var size := get_viewport_rect().size
	for i in 2:
		var f: Dictionary = game.get_fighter(i)
		var x := size.x * (0.3 if i == 0 else 0.6)
		draw_string(font, Vector2(x, size.y - 40), "P%d  %d%%" % [i + 1, int(f.percent)], HORIZONTAL_ALIGNMENT_LEFT, -1, 30, COLORS[i])
		for s in f.stocks:
			draw_circle(Vector2(x + 12 + s * 22, size.y - 18), 7, COLORS[i])
		draw_string(font, Vector2(x + 90, size.y - 14), state_label(f), HORIZONTAL_ALIGNMENT_LEFT, -1, 14)

	var seconds_left: int = max(0, cfg.time_limit_frames - game.get_frame()) / 60
	draw_string(font, Vector2(size.x / 2 - 30, 30), "%d:%02d" % [seconds_left / 60, seconds_left % 60], HORIZONTAL_ALIGNMENT_LEFT, -1, 24)
	draw_string(font, Vector2(16, 24), "P1: WASD, Space jump, J light, K heavy   |   P2: arrows, Enter jump, . light, / heavy", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(1, 1, 1, 0.6))
	draw_string(font, Vector2(16, 44), "R: reload moves + restart   Esc: quit", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(1, 1, 1, 0.6))

	if load_error != "":
		draw_string(font, Vector2(16, 80), "Move file error: " + load_error, HORIZONTAL_ALIGNMENT_LEFT, size.x - 32, 18, Color(1, 0.3, 0.3))
	if game.is_over():
		draw_string(font, Vector2(0, size.y / 2 - 100), winner_text() + "   (R to restart)", HORIZONTAL_ALIGNMENT_CENTER, size.x, 40)


# Debug text: the state machine's state, e.g. "ATTACK heavy 12" = frame 12 of the heavy.
func state_label(f: Dictionary) -> String:
	var label: String = STATE_NAMES[f.state]
	if f.move >= 0:
		label += " " + MOVE_NAMES[f.move]
	label += " %d" % f.state_frame
	if f.state == ST_HITSTUN:
		label += " (%d left)" % f.hitstun
	return label


func winner_text() -> String:
	var w: int = game.get_winner()  # same rule the RL side uses, from core/sim.cpp
	return "Draw" if w < 0 else "P%d wins!" % (w + 1)
