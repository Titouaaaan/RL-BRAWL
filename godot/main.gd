extends Node2D
## Draws the C++ simulation. All game logic lives in core/sim.cpp; this script
## only reads the keyboard, calls step() once per frame, and draws the result.

const PX := 16.0  # pixels per sim unit
const FIGHTER_SIZE := Vector2(2.0, 3.0)  # sim units. ponytail: visual only until phase 1 adds hurtboxes to the sim
const COLORS := [Color(0.35, 0.6, 1.0), Color(1.0, 0.45, 0.35)]
const STATE_NAMES := ["IDLE", "WALK", "AIRBORNE"]  # same order as FighterState in core/sim.h

# Keys are *physical* positions (US layout names), so on AZERTY "WASD" is ZQSD.
# Player 1: WASD stick, Space jump, J light, K heavy.
# Player 2: arrow keys stick, Enter jump, the two keys left of right Shift
#           (. and / on US, : and ! on AZERTY) light and heavy.
const KEYS := [
	{left = KEY_A, right = KEY_D, down = KEY_S, up = KEY_W, jump = KEY_SPACE, light = KEY_J, heavy = KEY_K},
	{left = KEY_LEFT, right = KEY_RIGHT, down = KEY_DOWN, up = KEY_UP, jump = KEY_ENTER, light = KEY_PERIOD, heavy = KEY_SLASH},
]

var game := FightGame.new()


func _physics_process(_delta: float) -> void:
	# Runs exactly 60 times per second: one call = one sim frame.
	if Input.is_physical_key_pressed(KEY_ESCAPE):
		get_tree().quit()
	if Input.is_physical_key_pressed(KEY_R):
		game.reset()
	var a := read_input(KEYS[0])
	var b := read_input(KEYS[1])
	game.step(a[0], a[1], a[2], b[0], b[1], b[2])
	queue_redraw()  # ask Godot to call _draw() again


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


# Sim coordinates (y up, origin at stage centre) -> screen pixels (y down).
func to_screen(x: float, y: float) -> Vector2:
	var center := get_viewport_rect().size * Vector2(0.5, 0.7)
	return center + Vector2(x, -y) * PX


func _draw() -> void:
	var font := ThemeDB.fallback_font
	var w: float = game.get_stage_half_width()

	# Stage: a thick line from one edge to the other.
	draw_line(to_screen(-w, 0), to_screen(w, 0), Color(0.8, 0.8, 0.8), 4.0)

	for i in 2:
		var f: Dictionary = game.get_fighter(i)
		# Box with its bottom-centre at the fighter's position.
		var top_left := to_screen(f.x - FIGHTER_SIZE.x / 2, f.y + FIGHTER_SIZE.y)
		draw_rect(Rect2(top_left, FIGHTER_SIZE * PX), COLORS[i])
		# Small white "eye" on the side the fighter is facing.
		var eye := to_screen(f.x + f.facing * FIGHTER_SIZE.x * 0.3, f.y + FIGHTER_SIZE.y * 0.8)
		draw_circle(eye, 3.0, Color.WHITE)
		# State label above the head.
		var label := "P%d %s %d" % [i + 1, STATE_NAMES[f.state], f.state_frame]
		draw_string(font, to_screen(f.x - 2, f.y + FIGHTER_SIZE.y + 0.6), label, HORIZONTAL_ALIGNMENT_LEFT, -1, 14)

	var help := "frame %d   |   P1: WASD + Space   P2: arrows + Enter   |   R reset   Esc quit" % game.get_frame()
	draw_string(font, Vector2(16, 28), help, HORIZONTAL_ALIGNMENT_LEFT, -1, 16)
