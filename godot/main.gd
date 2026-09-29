extends Node2D
## Draws the C++ simulation. All game logic lives in core/; this script only
## reads the keyboard (or a replay), calls step() once per frame, and draws.

const COLORS := [Color(0.35, 0.6, 1.0), Color(1.0, 0.45, 0.35)]

# Keys are *physical* positions (US layout names), so on AZERTY "WASD" is ZQSD.
# Player 1: WASD stick, Space jump, J light, K heavy, L dodge.
# Player 2: arrow keys stick, Enter jump, and the three keys left of right
#           Shift: , dodge  . light  / heavy (on AZERTY: ; : !).
const KEYS := [
	{left = KEY_A, right = KEY_D, down = KEY_S, up = KEY_W, jump = KEY_SPACE, light = KEY_J, heavy = KEY_K, dodge = KEY_L},
	{left = KEY_LEFT, right = KEY_RIGHT, down = KEY_DOWN, up = KEY_UP, jump = KEY_ENTER, light = KEY_PERIOD, heavy = KEY_SLASH, dodge = KEY_COMMA},
]

# Controllers: the first one connected plays P1, the second P2; the keyboard
# works at the same time. Button names are Godot's (Xbox-style positions);
# on a PS4 pad: A = Cross, B = Circle, X = Square, Y = Triangle.
#   Left stick or d-pad: move      Cross / Triangle: jump
#   Square: light      Circle: heavy      L1 / R1 / L2 / R2: dodge
#   Options: restart (like R)
const PAD := {
	jump = [JOY_BUTTON_A, JOY_BUTTON_Y],
	light = [JOY_BUTTON_X],
	heavy = [JOY_BUTTON_B],
	dodge = [JOY_BUTTON_LEFT_SHOULDER, JOY_BUTTON_RIGHT_SHOULDER],
}
const DEADZONE := 0.5  # how far the stick must be pushed to count as a direction
const BUTTONS := ["jump", "light", "heavy", "dodge"]  # position + 1 = the Button enum in core/sim.h
# The sim takes one button per frame, so when several are held we must pick
# one. Per player: which buttons were held last frame, and the last one sent.
var prev_held := [[false, false, false, false], [false, false, false, false]]
var last_button := [0, 0]

var game := FightGame.new()
var move_names: PackedStringArray = game.get_move_names()  # from core/moves.cpp
var state_names: PackedStringArray = game.get_state_names()  # from core/sim.cpp
var ST := {}  # state name -> number, e.g. ST.HITSTUN (filled in _ready)
# Render interpolation: the sim runs at exactly 60 steps per second, but the
# screen may refresh faster (120/144 Hz). Drawing only the latest step makes
# motion look choppy, so we draw between the previous and the current step.
var prev_fighters := [{}, {}]
var cur_fighters := [{}, {}]
var cfg: Dictionary
var load_error := ""
# Camera: follows both fighters and zooms out as they get further apart.
var cam_center := Vector2(0, 5)  # sim coordinates
var px := 14.0  # zoom: pixels per sim unit

# Replay mode (play.bat replay): inputs come from a recording, not the keyboard.
const SPEEDS := [0.25, 0.5, 1.0, 2.0, 4.0, 8.0]
var replay := {}  # empty = normal keyboard play
var replay_name := ""
var replay_frame := 0
var replay_result := ""  # when the recording ends: "verified" or "DESYNC"
var paused := false
var speed_i := 2  # index into SPEEDS: 1x
var speed_acc := 0.0  # fractional frames owed, for speeds below 1x


func _ready() -> void:
	for i in state_names.size():
		ST[state_names[i]] = i
	var args := OS.get_cmdline_user_args()  # whatever came after "--" on the command line
	var path := ""
	for a in args:
		if a.begins_with("--replay="):
			path = a.trim_prefix("--replay=")
	if path == "":
		reload()
		return
	if path == "latest":
		path = find_latest_replay()
	load_replay(path)
	if "--verify" in args:
		# Headless check: run the whole recording at once, report, quit.
		while replay_step():
			pass
		var result := replay_result if replay_result != "" else "FAILED " + load_error
		print("REPLAY %s: %s" % [result, replay_name])
		get_tree().quit(0 if replay_result == "verified" else 1)


func load_replay(path: String) -> void:
	replay_name = path.get_file()
	var data = JSON.parse_string(FileAccess.get_file_as_string(path)) if path != "" else null
	if typeof(data) != TYPE_DICTIONARY:
		reload()
		load_error = "could not read replay '%s' (none recorded yet?)" % path
		return
	replay = data
	restart_replay()


# Loads the move files stored in the replay (not the current ones in
# data/moves), so old replays still play back after you tune the moves.
func restart_replay() -> void:
	var texts := PackedStringArray()
	for n in move_names:
		if not replay.moves.has(n):
			load_error = "replay was recorded without move '%s' (an older version of the game)" % n
			replay.inputs = []
			return
		texts.append(replay.moves[n])
	load_error = game.load_moves_from_text(texts)
	cfg = game.get_config()
	replay_frame = 0
	replay_result = ""


# Feeds the next recorded frame to the sim. Returns false once the recording is over.
func replay_step() -> bool:
	var inputs: Array = replay.inputs
	if replay_frame >= inputs.size():
		return false
	var i: Array = inputs[replay_frame]
	game.step(int(i[0]), int(i[1]), int(i[2]), int(i[3]), int(i[4]), int(i[5]))
	replay_frame += 1
	if replay_frame == inputs.size():
		# Same inputs + same code must give the exact same final state.
		replay_result = "verified" if game.get_hash() == replay.final_hash else "DESYNC"
	return true


# The newest .json anywhere under replays/.
func find_latest_replay() -> String:
	var best := ""
	var best_time := 0
	var stack := [ProjectSettings.globalize_path("res://").path_join("../replays").simplify_path()]
	while not stack.is_empty():
		var dir: String = stack.pop_back()
		for sub in DirAccess.get_directories_at(dir):
			stack.append(dir.path_join(sub))
		for f in DirAccess.get_files_at(dir):
			var t := FileAccess.get_modified_time(dir.path_join(f))
			if f.ends_with(".json") and t >= best_time:
				best = dir.path_join(f)
				best_time = t
	return best


# Re-reads data/moves/*.json and restarts. Bound to R, so you can edit a move
# file, press R, and try the change without restarting Godot.
func reload() -> void:
	var dir := ProjectSettings.globalize_path("res://").path_join("../data/moves").simplify_path()
	load_error = game.load_moves(dir)
	cfg = game.get_config()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventJoypadButton and event.pressed and event.button_index == JOY_BUTTON_START:
		if replay.is_empty():
			reload()
		else:
			restart_replay()
		return
	if not (event is InputEventKey and event.pressed):
		return
	var key: Key = event.physical_keycode
	if key == KEY_ESCAPE:
		get_tree().quit()
	elif replay.is_empty():
		if key == KEY_R and not event.echo:
			reload()
	elif key == KEY_R and not event.echo:
		restart_replay()
	elif key == KEY_SPACE and not event.echo:
		paused = not paused
	elif key == KEY_RIGHT and paused:  # holding it repeats, to scrub forward
		replay_step()
	elif key == KEY_UP and not event.echo:
		speed_i = min(speed_i + 1, SPEEDS.size() - 1)
	elif key == KEY_DOWN and not event.echo:
		speed_i = max(speed_i - 1, 0)


func _physics_process(_delta: float) -> void:
	# Runs exactly 60 times per second: one call = one sim frame at 1x speed.
	prev_fighters = [game.get_fighter(0), game.get_fighter(1)]
	if replay.is_empty():
		var a := merge(read_input(KEYS[0]), read_pad(0))
		var b := merge(read_input(KEYS[1]), read_pad(1))
		game.step(a[0], a[1], pick_button(0, a[2]), b[0], b[1], pick_button(1, b[2]))
	elif not paused:
		speed_acc += SPEEDS[speed_i]
		while speed_acc >= 1.0:
			speed_acc -= 1.0
			replay_step()
	cur_fighters = [game.get_fighter(0), game.get_fighter(1)]


# Runs once per screen refresh (can be more often than 60 Hz).
func _process(delta: float) -> void:
	if cur_fighters[0].is_empty():
		return
	update_camera(delta)
	queue_redraw()  # ask Godot to call _draw() again


# A fighter's dictionary with x/y blended between the last two sim steps.
func shown(i: int) -> Dictionary:
	var f: Dictionary = cur_fighters[i].duplicate()
	var p: Dictionary = prev_fighters[i]
	if p.is_empty() or abs(p.x - f.x) + abs(p.y - f.y) > 6.0:  # respawns teleport: don't blend
		return f
	var t := Engine.get_physics_interpolation_fraction()
	f.x = lerp(float(p.x), float(f.x), t)
	f.y = lerp(float(p.y), float(f.y), t)
	return f


func update_camera(delta: float) -> void:
	var a := shown(0)
	var b := shown(1)
	var lo := Vector2(min(a.x, b.x), min(a.y, b.y))
	var hi := Vector2(max(a.x, b.x), max(a.y, b.y) + cfg.hurtbox_h)
	# Half the visible area, in sim units: both fighters plus a margin, but
	# never closer than about the stage width (56 units).
	var half := Vector2(max((hi.x - lo.x) / 2 + 12, 28), max((hi.y - lo.y) / 2 + 8, 15))
	var size := get_viewport_rect().size
	var target_px: float = min(size.x / 2 / half.x, size.y / 2 / half.y)
	var target := (lo + hi) / 2 + Vector2(0, 3)  # a bit above the fighters, so the floor sits low
	# Move 10% of the way per 60 Hz frame: smooth instead of jerky. The power
	# makes it the same speed at any screen refresh rate.
	var k := 1.0 - pow(0.9, delta * 60.0)
	cam_center = cam_center.lerp(target, k)
	px = lerp(px, target_px, k)


# Keyboard: [stick_x, stick_y, [jump, light, heavy, dodge held?]].
func read_input(k: Dictionary) -> Array:
	var sx := int(held(k.right)) - int(held(k.left))
	var sy := int(held(k.up)) - int(held(k.down))
	return [sx, sy, BUTTONS.map(func(n): return held(k[n]))]


# The newest press wins: a button pressed this frame beats one that is still
# held. Otherwise holding jump (for a full hop) would hide an attack pressed
# mid-jump until jump was released, which feels like input lag.
func pick_button(p: int, now: Array) -> int:
	var b := 0  # none
	for i in 4:
		if now[i] and not prev_held[p][i]:
			b = i + 1
			break
	if b == 0 and last_button[p] > 0 and now[last_button[p] - 1]:
		b = last_button[p]  # keep holding the same button
	if b == 0:
		for i in 4:
			if now[i]:
				b = i + 1
				break
	prev_held[p] = now
	last_button[p] = b
	return b


func held(key: Key) -> bool:
	return Input.is_physical_key_pressed(key)


# The player's controller (the n-th one connected), as [stick_x, stick_y, held buttons].
func read_pad(n: int) -> Array:
	var pads := Input.get_connected_joypads()
	if n >= pads.size():
		return [0, 0, [false, false, false, false]]
	var dev: int = pads[n]
	var x := Input.get_joy_axis(dev, JOY_AXIS_LEFT_X)
	var y := -Input.get_joy_axis(dev, JOY_AXIS_LEFT_Y)  # Godot's stick y points down; the sim's up
	# The sim only knows -1/0/1 per axis (the same 3 values the agent picks from).
	var sx := int(x > DEADZONE) - int(x < -DEADZONE)
	var sy := int(y > DEADZONE) - int(y < -DEADZONE)
	sx = clampi(sx + int(pad(dev, JOY_BUTTON_DPAD_RIGHT)) - int(pad(dev, JOY_BUTTON_DPAD_LEFT)), -1, 1)
	sy = clampi(sy + int(pad(dev, JOY_BUTTON_DPAD_UP)) - int(pad(dev, JOY_BUTTON_DPAD_DOWN)), -1, 1)
	var triggers := Input.get_joy_axis(dev, JOY_AXIS_TRIGGER_LEFT) > 0.5 or Input.get_joy_axis(dev, JOY_AXIS_TRIGGER_RIGHT) > 0.5
	var now := BUTTONS.map(func(n): return PAD[n].any(func(b): return pad(dev, b)))
	now[3] = now[3] or triggers
	return [sx, sy, now]


func pad(dev: int, b: JoyButton) -> bool:
	return Input.is_joy_button_pressed(dev, b)


# Keyboard + controller for the same player: directions add up, a button is
# held if it's held on either.
func merge(k: Array, p: Array) -> Array:
	var now := []
	for i in 4:
		now.append(k[2][i] or p[2][i])
	return [clampi(k[0] + p[0], -1, 1), clampi(k[1] + p[1], -1, 1), now]


# Sim coordinates (y up) -> screen pixels (y down), through the camera.
func to_screen(x: float, y: float) -> Vector2:
	return get_viewport_rect().size / 2 + Vector2(x - cam_center.x, cam_center.y - y) * px


func _draw() -> void:
	if cur_fighters[0].is_empty():
		return  # nothing simulated yet
	var font := ThemeDB.fallback_font
	var w: float = cfg.stage_half_width

	# Blast zones (leave this rectangle = lose a stock) and the stage.
	var bz_tl := to_screen(-cfg.blast_x, cfg.blast_top)
	var bz_br := to_screen(cfg.blast_x, cfg.blast_bottom)
	draw_rect(Rect2(bz_tl, bz_br - bz_tl), Color(1, 1, 1, 0.15), false, 2.0)
	# The main stage is a solid block: you can't pass through its sides or underside.
	var stage_tl := to_screen(-w, 0)
	draw_rect(Rect2(stage_tl, to_screen(w, -cfg.stage_depth) - stage_tl), Color(0.45, 0.45, 0.5))
	# Ledges: the two top corners you can hang from.
	for side in [-1, 1]:
		draw_circle(to_screen(side * w, 0), px * 0.3, Color(0.9, 0.9, 0.6))
	# Platforms (every floor after the first): thin, pass-through from below.
	for k in range(1, cfg.surfaces.size()):
		var s: Array = cfg.surfaces[k]
		var p_tl := to_screen(s[0], s[2])
		draw_rect(Rect2(p_tl, to_screen(s[1], s[2] - 0.5) - p_tl), Color(0.55, 0.55, 0.62))

	var fighters := [shown(0), shown(1)]
	for i in 2:
		draw_fighter(i, fighters[i], font)

	# Hitboxes: translucent circles in the attacker's colour with a red rim.
	# "Spent" ones (the move already hit) are drawn faded; they can't hit again.
	# Grab boxes get a yellow rim. They move with their (interpolated) owner.
	for hb in game.get_hitboxes():
		var o: int = hb.owner
		var c := to_screen(hb.x + fighters[o].x - cur_fighters[o].x, hb.y + fighters[o].y - cur_fighters[o].y)
		var alpha := 0.15 if hb.spent else 0.45
		var rim := Color(1, 0.9, 0.1, alpha * 2) if hb.grab else Color(1, 0.1, 0.1, alpha * 2)
		draw_circle(c, hb.radius * px, Color(COLORS[hb.owner], alpha))
		draw_arc(c, hb.radius * px, 0, TAU, 32, rim, 2.0)

	draw_hud(font)


func draw_fighter(i: int, f: Dictionary, font: Font) -> void:
	var w: float = cfg.hurtbox_w
	var h: float = cfg.hurtbox_h
	if f.state == ST.KNOCKDOWN:
		w = h * 1.1  # lying on the floor
		h = cfg.lying_h
	elif f.state == ST.CROUCH:
		h = cfg.crouch_h  # a smaller target: some high attacks miss
	var hw := w / 2
	# Tumbling (launched by a strong hit): the body spins until you act or land.
	if f.tumble:
		var center := to_screen(f.x, f.y + h / 2)
		draw_set_transform(center, f.state_frame * 0.35, Vector2.ONE)
		draw_rect(Rect2(-Vector2(w, h) * px / 2, Vector2(w, h) * px), COLORS[i].lerp(Color.WHITE, 0.6))
		draw_set_transform(Vector2.ZERO)
		return
	var shake := 0.0
	if f.hitlag > 0 and f.state == ST.HITSTUN:
		shake = 0.18 * (1 if f.hitlag % 2 == 0 else -1)  # the victim shakes during the hit freeze
	var top_left := to_screen(f.x - hw + shake, f.y + h)
	var rect := Rect2(top_left, Vector2(w, h) * px)
	var color: Color = COLORS[i]
	if f.state == ST.HITSTUN:
		color = color.lerp(Color.WHITE, 0.6)  # flash pale while stunned
	elif f.state == ST.GRABBED:
		color = color.lerp(Color(0.6, 0.2, 0.8), 0.5)
	elif f.state == ST.SHIELDBREAK:
		color = color.lerp(Color(1, 0.9, 0.2), 0.5 + 0.3 * sin(f.state_frame * 0.4))  # dazed
	elif f.state == ST.ATTACK and f.charge > 0:
		# Charging a smash: glows brighter the longer it's held (up to 40% stronger).
		color = color.lerp(Color(1, 0.95, 0.4), 0.3 + 0.5 * f.charge / cfg.charge_max * (0.75 + 0.25 * sin(f.charge * 0.8)))
	elif f.state == ST.REBOUND:
		color = color.lerp(Color(0.2, 0.2, 0.2), 0.5)  # clanked or parried: stuck, open to a punish
	elif f.state == ST.SHIELDDROP and f.state_frame < 5:
		color = color.lerp(Color(0.6, 1, 1), 0.6)  # the parry window
	elif f.state == ST.LEDGE and f.state_frame > cfg.ledge_hang_max - 60 and (f.state_frame / 4) % 2 == 0:
		color = color.lerp(Color.RED, 0.6)  # hanging too long: about to let go
	if f.invuln > 0 and (f.invuln / 6) % 2 == 0:
		color.a = 0.35  # blink while invulnerable after respawning
	if f.intangible and f.invuln == 0:
		color.a = 0.3  # see-through while dodging: hits pass through
	draw_rect(rect, color)
	if f.intangible:
		draw_rect(rect, Color.WHITE, false, 2.0)  # white outline = intangible (i-frames)
	# White "eye" on the side the fighter is facing.
	draw_circle(to_screen(f.x + f.facing * hw * 0.6 + shake, f.y + h * 0.8), px * 0.25, Color.WHITE)
	# Shield: a bubble that shrinks as the shield wears down.
	if f.state == ST.SHIELD or f.state == ST.SHIELDSTUN:
		var r: float = (0.7 + 1.5 * f.shield_hp / cfg.shield_max) * px
		var c := to_screen(f.x, f.y + cfg.hurtbox_h / 2)
		draw_circle(c, r, Color(COLORS[i], 0.3))
		draw_arc(c, r, 0, TAU, 40, Color(1, 1, 1, 0.8 if f.state == ST.SHIELDSTUN else 0.4), 2.0)
	# Small player number above the head.
	draw_string(font, to_screen(f.x - 0.5, f.y + h + 0.5), "P%d" % (i + 1), HORIZONTAL_ALIGNMENT_LEFT, -1, 14, COLORS[i])


func draw_hud(font: Font) -> void:
	var size := get_viewport_rect().size
	for i in 2:
		var f: Dictionary = cur_fighters[i]
		var x := size.x * (0.3 if i == 0 else 0.6)
		# Shield meter: a thin bar under the damage.
		draw_rect(Rect2(x, size.y - 6, 80.0 * f.shield_hp / cfg.shield_max, 3), Color(COLORS[i], 0.7))
		draw_string(font, Vector2(x, size.y - 40), "P%d  %d%%" % [i + 1, int(f.percent)], HORIZONTAL_ALIGNMENT_LEFT, -1, 30, COLORS[i])
		for s in f.stocks:
			draw_circle(Vector2(x + 12 + s * 22, size.y - 18), 7, COLORS[i])
		draw_string(font, Vector2(x + 90, size.y - 14), state_label(f), HORIZONTAL_ALIGNMENT_LEFT, -1, 14)
		if f.combo >= 2:  # combo counter, like Smash's training mode
			draw_string(font, Vector2(x, size.y - 78), "%d HIT COMBO" % f.combo, HORIZONTAL_ALIGNMENT_LEFT, -1, 22, COLORS[i])

	var seconds_left: int = max(0, cfg.time_limit_frames - game.get_frame()) / 60
	draw_string(font, Vector2(size.x / 2 - 30, 30), "%d:%02d" % [seconds_left / 60, seconds_left % 60], HORIZONTAL_ALIGNMENT_LEFT, -1, 24)
	var dim := Color(1, 1, 1, 0.6)
	if replay.is_empty():
		draw_string(font, Vector2(16, 24), "P1: WASD, Space jump, J light, K heavy, L dodge (or controller 1)", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, dim)
		draw_string(font, Vector2(16, 44), "P2: arrows, Enter jump, . light, / heavy, , dodge (or controller 2)", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, dim)
		draw_string(font, Vector2(16, 64), "Attacks: light/heavy + up/down/side/neutral; keep pressing light = jab combo; hold heavy = charge a smash; light while running = dash attack.  Dodge: shield (+ side = roll, + down = spot dodge); let go as a hit lands = parry; light while shielding = grab (then light = pummel, a direction = throw).", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, dim)
		draw_string(font, Vector2(16, 84), "Tap jump = short hop.  Down = crouch / fast-fall / drop through.  Air dodge + direction = directional.  Dodge as you land = tech.  Ledge: toward = climb, jump, attack, dodge = roll.  R: restart  Esc: quit", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, dim)
	else:
		var status := "PAUSED" if paused else "%sx" % SPEEDS[speed_i]
		var line := "REPLAY  frame %d / %d   %s" % [replay_frame, replay.inputs.size(), status]
		draw_string(font, Vector2(16, 24), line, HORIZONTAL_ALIGNMENT_LEFT, -1, 14)
		draw_string(font, Vector2(16, 44), "Space: pause   Right: next frame (paused)   Up/Down: speed   R: restart   Esc: quit", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, dim)
		draw_string(font, Vector2(16, 64), "%s   %s" % [replay_name, JSON.stringify(replay.meta)], HORIZONTAL_ALIGNMENT_LEFT, -1, 14, dim)
		if replay_result == "verified":
			draw_string(font, Vector2(16, 84), "Replay verified: final state hash matches the recording", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0.5, 1, 0.5))
		elif replay_result == "DESYNC":
			draw_string(font, Vector2(16, 84), "DESYNC: final state differs from the recording (see docs/running.md)", HORIZONTAL_ALIGNMENT_LEFT, -1, 16, Color(1, 0.3, 0.3))

	if load_error != "":
		draw_string(font, Vector2(16, 80), "Move file error: " + load_error, HORIZONTAL_ALIGNMENT_LEFT, size.x - 32, 18, Color(1, 0.3, 0.3))
	if game.is_over():
		draw_string(font, Vector2(0, size.y / 2 - 100), winner_text() + "   (R to restart)", HORIZONTAL_ALIGNMENT_CENTER, size.x, 40)


# Debug text: the state machine's state, e.g. "ATTACK heavy 12" = frame 12 of the heavy.
func state_label(f: Dictionary) -> String:
	var label: String = state_names[f.state]
	if f.state == ST.RUN and f.state_frame < cfg.dash_frames:
		label = "DASH"
	if f.move >= 0:
		label += " " + move_names[f.move]
	label += " %d" % f.state_frame
	if f.state == ST.HITSTUN:
		label += " (%d left)" % f.hitstun
	if f.air_attack_locked:
		label += "  [no air attacks]"
	return label


func winner_text() -> String:
	var w: int = game.get_winner()  # same rule the RL side uses, from core/sim.cpp
	return "Draw" if w < 0 else "P%d wins!" % (w + 1)
