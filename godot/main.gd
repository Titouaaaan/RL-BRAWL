extends Node2D
## Draws the C++ simulation. All game logic lives in core/; this script only
## reads the keyboard (or a replay), calls step() once per frame, and draws.

const COLORS := [Color(0.35, 0.6, 1.0), Color(1.0, 0.45, 0.35)]

# Characters: assets/characters/<id>/, listed in characters.json (made by
# tools/sprites.py from the folders in assets/sprites). Each character is a
# square canvas of "px" pixels (64 by default), feet on the bottom row, drawn
# so "ref_h" pixels (61 by default) = the fighter's height.
# Pick them with --p1=ID --p2=ID (play.bat passes them on).
const SPRITE_PX := 64.0
const SPRITE_REF_PX := 61.0
var chars := [{}, {}]  # per player: {tex, flip, px, ref, body_w, name, anims}; tex is null if missing
# Animations: horizontal strips of px-by-px frames (assets/characters/<id>/<name>.png).
# Looping ones play at their "fps" from characters.json (drawings per second;
# the game runs at 60 frames per second); attacks are stretched over the
# whole move instead.
const DEFAULT_FPS := 7.5
# Map art (assets/maps): a background filling the window; stage.png and
# platform.png (made from the tiles, edit them freely) stretched over each
# surface, platforms 1 unit thick. Collision is still the full stage rectangle
# (core/sim.h): the stage picture's cut-away corners are drawing only.
const BACKGROUND := "orange_clouds.png"
const SHOW_LEDGES := false  # true: yellow dots on the two grabbable ledge corners
var map_tex := {}  # file name -> texture
# KO explosion (assets/effects/explosion_1.png: 10 frames of 128 px, packed
# from the Explosion_1 folder), shown where the fighter left the screen.
const EXPLOSION_FPS := 15.0
const EXPLOSION_SIZE := 8.0  # sim units
var explosion_tex: Texture2D
var explosions := []  # [{at, start}]: position on screen (-1..1 each way, so it stays put while the camera moves), game frame it started

# After the last stock is taken the game runs on for END_DELAY frames: the
# camera holds on the explosion for the first half, then follows the winner,
# who keeps control; then everything freezes on "Px wins!".
const END_DELAY := 240
var ko_frame := -1  # game frame of the final KO; -1 while both still have stocks

# Live bots (--bot1=... / --bot2=..., from the menu or play.bat --vs): a player
# can be a trained agent, run inside the game (core/net.cpp), or a scripted
# bot, for which the game starts train/serve.py (one Python process per bot,
# ports BOT_PORT and BOT_PORT + 1) and talks to it over a local TCP
# connection. A bot plays under the same conditions as in training:
# it sees the game BOT_DELAY frames late and picks an action every BOT_SKIP
# frames, which is held in between. Everything below is per player.
const BOT_DELAY := 6  # reaction delay, frames (train/ppo.py --reaction-delay)
const BOT_SKIP := 4   # frame skip (train/ppo.py --frame-skip)
const BOT_PORT := 5055
var bot_spec := ["", ""]   # what each bot is ("latest", a .bin or .pt path, a scripted bot); "" = a human
var bot_pid := [-1, -1]    # the serve.py process, to stop it when leaving
var bot_tcp := [StreamPeerTCP.new(), StreamPeerTCP.new()]
var bot_state := ["", ""]  # "", "connecting", "handshake", "ready", or an error message
var bot_wait := [0.0, 0.0]  # seconds spent connecting
var bot_hist := [[], []]   # each bot's last BOT_DELAY + 1 observations, oldest first
var bot_action := [[0, 0, 0], [0, 0, 0]]  # real (un-mirrored) stick x, stick y, button: held between decisions

# Keys are *physical* positions (US layout names), so on AZERTY "WASD" is ZQSD.
# Player 1: WASD stick, Space jump, J light, K heavy, L dodge.
# Player 2: arrow keys stick, Enter jump, and the three keys left of right
#           Shift: , dodge  . light  / heavy (on AZERTY: ; : !).
const KEYS := [
	{left = KEY_A, right = KEY_D, down = KEY_S, up = KEY_W, jump = KEY_SPACE, light = KEY_J, heavy = KEY_K, dodge = KEY_L},
	{left = KEY_LEFT, right = KEY_RIGHT, down = KEY_DOWN, up = KEY_UP, jump = KEY_ENTER, light = KEY_PERIOD, heavy = KEY_SLASH, dodge = KEY_COMMA},
]

# Controllers: the first one connected plays P1, the second P2; the keyboard
# works at the same time. Smash Ultimate's default layout, by button position
# (Godot names buttons by position: A = bottom, B = right, X = left, Y = top;
# on a PS4 pad: A = Cross, B = Circle, X = Square, Y = Triangle):
#   Left stick (or d-pad): move          Right stick: heavy in that direction (Smash's C-stick)
#   Circle (Smash A): light attack       Cross (Smash B, specials): heavy, until specials exist
#   Triangle / Square (Smash X / Y): jump
#   L2 / R2 (Smash ZL / ZR): shield / dodge     L1 / R1 (Smash L / R): grab
#   Options: restart (like R)
const PAD := {
	jump = [JOY_BUTTON_X, JOY_BUTTON_Y],
	light = [JOY_BUTTON_B],
	heavy = [JOY_BUTTON_A],
	dodge = [],  # the triggers (L2 / R2): read as axes in read_pad
}
const PAD_GRAB := [JOY_BUTTON_LEFT_SHOULDER, JOY_BUTTON_RIGHT_SHOULDER]
# The grab button is a 2-frame shortcut: our grab is "light while shielding",
# so it sends shield (stick neutral, so it isn't a roll) then light. In the air
# that's an air dodge, like Smash's grab button for most characters.
var grab_seq := [0, 0]  # frames of the shortcut still to send, per player
var grab_prev := [false, false]
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
	# Pixel art: sample the nearest pixel instead of blurring when scaled.
	texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	# Options: from the menu (Session.args) or whatever came after "--" on the command line.
	var args: PackedStringArray = Session.args if Session.from_menu else OS.get_cmdline_user_args()
	load_characters(args)
	var maps := ProjectSettings.globalize_path("res://").path_join("../assets/maps").simplify_path()
	var boom := Image.load_from_file(maps.path_join("../effects/explosion_1.png").simplify_path())
	explosion_tex = ImageTexture.create_from_image(boom) if boom != null else null
	for f in [BACKGROUND, "stage.png", "platform.png"]:
		var img := Image.load_from_file(maps.path_join(f))
		if img != null:
			map_tex[f] = ImageTexture.create_from_image(img)
	for i in state_names.size():
		ST[state_names[i]] = i
	var path := ""
	for a in args:
		if a.begins_with("--replay="):
			path = a.trim_prefix("--replay=")
	if path == "":
		reload()
		for a in args:
			if a.begins_with("--bot1="):
				bot_spec[0] = a.trim_prefix("--bot1=")
			elif a.begins_with("--bot2="):
				bot_spec[1] = a.trim_prefix("--bot2=")
		for p in 2:
			if bot_spec[p] != "":
				start_bot(p)
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


# Loads the two players' characters: --p1=ID / --p2=ID, default 1 and 2.
func load_characters(args: PackedStringArray) -> void:
	var dir := ProjectSettings.globalize_path("res://").path_join("../assets/characters").simplify_path()
	var manifest = JSON.parse_string(FileAccess.get_file_as_string(dir.path_join("characters.json")))
	if typeof(manifest) != TYPE_DICTIONARY:
		manifest = {}
	var ids := [Session.p1_char, Session.p2_char]  # the saved choice (Character Select), or 1 and 2
	for a in args:
		if a.begins_with("--p1="):
			ids[0] = a.trim_prefix("--p1=")
		elif a.begins_with("--p2="):
			ids[1] = a.trim_prefix("--p2=")
	for p in 2:
		var c: Dictionary = manifest.get(ids[p], {})
		var img := Image.load_from_file(dir.path_join(c.get("file", "missing.png"))) if not c.is_empty() else null
		var anims := {}  # animation name -> {tex, frames}
		for anim_name in c.get("anims", {}):
			var a: Dictionary = c.anims[anim_name]
			var strip := Image.load_from_file(dir.path_join(a.file))
			if strip != null:
				anims[anim_name] = {tex = ImageTexture.create_from_image(strip), frames = int(a.frames),
					fps = float(a.get("fps", DEFAULT_FPS))}
		chars[p] = {
			tex = ImageTexture.create_from_image(img) if img != null else null,
			flip = -1 if c.get("faces", "right") == "left" else 1,  # drawings are flipped to face right
			px = float(c.get("px", SPRITE_PX)),
			ref = float(c.get("ref_h", SPRITE_REF_PX)),
			body_w = float(c.get("body_w", 47)),
			name = c.get("name", "(no character %s)" % ids[p]),
			anims = anims,
		}


# Which animation a fighter shows, most specific first. The first one the
# character has is used; with none, the static sprite is drawn.
func anim_for(f: Dictionary) -> Array:
	var st: int = f.state
	if st == ST.ATTACK:
		return [move_names[f.move] if f.move >= 0 else "", "attack"]
	if st == ST.IDLE or st == ST.LANDING:
		return ["idle"]
	if st == ST.RUN:
		return ["run"]
	if st == ST.JUMPSQUAT or st == ST.AIRBORNE or st == ST.HELPLESS:
		return ["jump", "fall"] if f.vy > 0 else ["fall", "jump"]
	if st == ST.HITSTUN:
		return ["hurt"]
	if st == ST.SHIELD or st == ST.SHIELDSTUN:
		return ["shield"]
	if st == ST.CROUCH:
		return ["crouch"]
	return []


# The texture and the part of it to draw for fighter i right now.
func sprite_frame(i: int, f: Dictionary) -> Array:
	var ch: Dictionary = chars[i]
	for anim_name in anim_for(f):
		if ch.anims.has(anim_name):
			var a: Dictionary = ch.anims[anim_name]
			var k: int
			if f.state == ST.ATTACK and f.move >= 0:  # stretched over the move: the last drawing at its end
				k = mini(a.frames - 1, int(f.state_frame * a.frames / max(1, cfg.move_frames[f.move])))
			else:  # looping: drawing number = seconds in this state x drawings per second
				k = int(f.state_frame * a.fps / 60.0) % a.frames
			return [a.tex, Rect2(k * ch.px, 0, ch.px, ch.px)]
	return [ch.tex, Rect2(0, 0, ch.px, ch.px)]


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
	# The input buffer the recording was made with (agents play without it).
	for p in 2:
		game.set_input_buffer(p, int(replay.get("input_buffer", 9)))
	cfg = game.get_config()
	replay_frame = 0
	replay_result = ""


# Feeds the next recorded frame to the sim. Returns false once the recording
# is over: at its end or at the game's end (the last decision is padded to 4
# frames, and the sim no longer ignores frames after the game ends).
func replay_step() -> bool:
	var inputs: Array = replay.inputs
	if replay_frame >= inputs.size() or game.is_over():
		return false
	var i: Array = inputs[replay_frame]
	game.step(int(i[0]), int(i[1]), int(i[2]), int(i[3]), int(i[4]), int(i[5]))
	replay_frame += 1
	if replay_frame == inputs.size() or game.is_over():
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
	for p in 2:
		bot_hist[p].clear()  # a restart: the bots' memory of past frames starts over


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
		if Session.from_menu:
			stop_bots()
			Session.back_to_menu()
		else:
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
		var all_ready := true
		for p in 2:  # the game waits until every bot is connected
			if bot_spec[p] != "" and not bot_update(p):
				all_ready = false
		if all_ready and not frozen():
			var a: Array = bot_decide(0) if bot_spec[0] != "" else human_input(0, merge(read_input(KEYS[0]), read_pad(0)))
			var b: Array = bot_decide(1) if bot_spec[1] != "" else human_input(1, merge(read_input(KEYS[1]), read_pad(1)))
			game.step(a[0], a[1], a[2], b[0], b[1], b[2])
	elif not paused:
		speed_acc += SPEEDS[speed_i]
		while speed_acc >= 1.0:
			speed_acc -= 1.0
			if not replay_step() and not frozen():
				game.step(0, 0, 0, 0, 0, 0)  # the recording stops at the KO: play the ending with nobody pressing anything
	cur_fighters = [game.get_fighter(0), game.get_fighter(1)]
	if game.get_frame() < ko_frame:
		ko_frame = -1  # restarted
	if ko_frame < 0 and (cur_fighters[0].stocks == 0 or cur_fighters[1].stocks == 0):
		ko_frame = game.get_frame()
	var half := get_viewport_rect().size / 2 / px  # half the visible area, in sim units
	for i in 2:  # a KO this frame: an explosion where the fighter left the screen
		var p: Dictionary = prev_fighters[i]
		var c: Dictionary = cur_fighters[i]
		if not p.is_empty() and ((c.respawn_wait > 0 and p.respawn_wait == 0) or (c.stocks == 0 and p.stocks > 0)):  # the last KO has no respawn wait
			var at := (Vector2(p.x, p.y) - cam_center) / half
			explosions.append({at = at.clamp(Vector2(-0.85, -0.8), Vector2(0.85, 0.8)), start = game.get_frame()})
	explosions = explosions.filter(func(e): return game.get_frame() - e.start in range(int(600 / EXPLOSION_FPS)))


# The result screen: the sim stops. A time-out freezes at once; a KO after END_DELAY.
func frozen() -> bool:
	return game.is_over() and (ko_frame < 0 or game.get_frame() - ko_frame >= END_DELAY)


# A network bot (a checkpoint or "latest") runs inside the game (core/net.cpp,
# weights exported to .bin by ppo.py or tools/export_bot.py); a scripted bot
# (fighter, spacer, ...) is Python code, so it runs in train/serve.py.
func is_net(spec: String) -> bool:
	return spec == "latest" or spec.ends_with(".bin") or spec.ends_with(".pt")


# Starts player p's bot: loads its network, or starts serve.py and begins connecting to it.
func start_bot(p: int) -> void:
	var spec: String = bot_spec[p]
	game.set_input_buffer(p, 0)  # bots were trained without the input buffer (humans keep 9)
	if is_net(spec):
		var path := newest_bot() if spec == "latest" else spec.get_basename() + ".bin"
		if not path.is_absolute_path():
			path = Session.repo_root().path_join(path)
		if not FileAccess.file_exists(path):
			bot_state[p] = "no trained run yet" if spec == "latest" else "no %s (export it: python tools/export_bot.py %s)" % [path.get_file(), spec]
			return
		var err: String = game.load_bot(p, FileAccess.get_file_as_bytes(path))
		bot_state[p] = err if err != "" else "ready"
		return
	var serve := Session.repo_root().path_join("train/serve.py")
	bot_pid[p] = OS.create_process("python", [serve, spec, "--port", str(BOT_PORT + p)])
	bot_state[p] = "connecting" if bot_pid[p] > 0 else "could not start python (is it on the PATH?)"
	bot_tcp[p].connect_to_host("127.0.0.1", BOT_PORT + p)


# The newest runs/*/agent_latest.bin (ppo.py rewrites it at every eval), or "".
func newest_bot() -> String:
	var best := ""
	var root := Session.repo_root().path_join("runs")
	for d in DirAccess.get_directories_at(root):
		var f := root.path_join(d).path_join("agent_latest.bin")
		if FileAccess.file_exists(f) and (best == "" or FileAccess.get_modified_time(f) > FileAccess.get_modified_time(best)):
			best = f
	return best


# Disconnects and stops the bot servers (each also quits by itself when the game disconnects).
func stop_bots() -> void:
	for p in 2:
		bot_tcp[p].disconnect_from_host()
		if bot_pid[p] > 0 and OS.is_process_running(bot_pid[p]):
			OS.kill(bot_pid[p])
		bot_pid[p] = -1


func _exit_tree() -> void:
	stop_bots()


# Keeps the connection to player p's bot going. Returns true once it's ready.
func bot_update(p: int) -> bool:
	if bot_state[p] == "ready":
		return true
	if bot_state[p] != "connecting" and bot_state[p] != "handshake":
		return false  # an error: shown on screen
	bot_wait[p] += 1.0 / 60.0
	var tcp: StreamPeerTCP = bot_tcp[p]
	tcp.poll()
	var status := tcp.get_status()
	if status == StreamPeerTCP.STATUS_ERROR or status == StreamPeerTCP.STATUS_NONE:
		# The server is probably still starting: try again.
		if bot_wait[p] > 30.0:
			bot_state[p] = "could not reach the bot (try: python train/serve.py %s)" % bot_spec[p]
			return false
		bot_tcp[p] = StreamPeerTCP.new()
		bot_tcp[p].connect_to_host("127.0.0.1", BOT_PORT + p)
		return false
	if status != StreamPeerTCP.STATUS_CONNECTED:
		return false
	if bot_state[p] == "connecting":  # say hello: our observation size
		tcp.set_no_delay(true)
		tcp.put_32(game.get_obs_dim())
		bot_state[p] = "handshake"
		return false
	if tcp.get_available_bytes() < 4:
		return false
	var theirs := tcp.get_32()
	if theirs != game.get_obs_dim():
		bot_state[p] = "the bot expects %d inputs, this game gives %d: rebuild both from the same code" % [theirs, game.get_obs_dim()]
		return false
	bot_state[p] = "ready"
	return true


# Player p's bot input for this frame: a new decision every BOT_SKIP frames,
# from what it saw BOT_DELAY frames ago; the same action is held in between.
func bot_decide(p: int) -> Array:
	var hist: Array = bot_hist[p]
	hist.append(game.get_obs(p))  # player p's view of the game right now
	while hist.size() > BOT_DELAY + 1:
		hist.pop_front()
	if game.get_frame() % BOT_SKIP != 0:
		return bot_action[p]
	var seen: PackedFloat32Array = hist[0]  # the oldest = BOT_DELAY frames ago (the start, early on)
	var act := []
	if is_net(bot_spec[p]):
		act = Array(game.bot_act(p, seen, randf()))
	else:
		act = serve_decide(p, seen)
		if act.is_empty():
			return [0, 0, 0]
	# Actions are egocentric like observations: player 2's "right" is the real left.
	bot_action[p] = [act[0] if p == 0 else -act[0], act[1], act[2]]
	return bot_action[p]


# A scripted bot's decision, from serve.py: [stick x, stick y, button], or [] if the connection broke.
func serve_decide(p: int, seen: PackedFloat32Array) -> Array:
	var tcp: StreamPeerTCP = bot_tcp[p]
	var t := Time.get_ticks_usec()
	tcp.put_data(seen.to_byte_array())
	while tcp.get_available_bytes() < 3:  # wait for the answer (well under a millisecond)
		tcp.poll()
		if tcp.get_status() != StreamPeerTCP.STATUS_CONNECTED or Time.get_ticks_usec() - t > 500000:
			bot_state[p] = "lost the connection to the bot"
			return []
	var got: Array = tcp.get_data(3)  # [error, bytes]
	var raw: PackedByteArray = got[1]
	var act := []
	for k in 3:
		act.append(raw[k] - 256 if raw[k] > 127 else raw[k])  # bytes are unsigned; the stick is -1..1
	return act


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
	if a.respawn_wait > 0 or b.respawn_wait > 0 or (ko_frame >= 0 and game.get_frame() - ko_frame < END_DELAY / 2):
		return  # after a KO the camera holds still until the drop-in (the explosion stays put)
	if a.stocks == 0:  # game over: follow the winner alone
		a = b
	if b.stocks == 0:
		b = a
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
		return [0, 0, [false, false, false, false], false]
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
	# Right stick (C-stick): heavy in the stick's direction (smash attacks on
	# the ground, heavy aerials in the air); it overrides the left stick.
	var cx := Input.get_joy_axis(dev, JOY_AXIS_RIGHT_X)
	var cy := -Input.get_joy_axis(dev, JOY_AXIS_RIGHT_Y)
	if absf(cx) > DEADZONE or absf(cy) > DEADZONE:
		sx = int(cx > DEADZONE) - int(cx < -DEADZONE)
		sy = int(cy > DEADZONE) - int(cy < -DEADZONE)
		now[2] = true
	return [sx, sy, now, PAD_GRAB.any(func(b): return pad(dev, b))]


func pad(dev: int, b: JoyButton) -> bool:
	return Input.is_joy_button_pressed(dev, b)


# Keyboard + controller for the same player: directions add up, a button is
# held if it's held on either.
func merge(k: Array, p: Array) -> Array:
	var now := []
	for i in 4:
		now.append(k[2][i] or p[2][i])
	return [clampi(k[0] + p[0], -1, 1), clampi(k[1] + p[1], -1, 1), now, p[3]]


# Player p's final input for this frame: the button picked from those held,
# or the grab shortcut (shield, then light) while it runs.
func human_input(p: int, a: Array) -> Array:
	if a[3] and not grab_prev[p]:
		grab_seq[p] = 2  # grab pressed: start the shortcut
	grab_prev[p] = a[3]
	var button := pick_button(p, a[2])
	if grab_seq[p] == 2:
		grab_seq[p] = 1
		return [0, 0, 4]  # shield (stick neutral: not a roll or spot dodge)
	if grab_seq[p] == 1:
		grab_seq[p] = 0
		return [a[0], a[1], 2]  # light while shielding = grab
	return [a[0], a[1], button]


# Sim coordinates (y up) -> screen pixels (y down), through the camera.
func to_screen(x: float, y: float) -> Vector2:
	return get_viewport_rect().size / 2 + Vector2(x - cam_center.x, cam_center.y - y) * px


func _draw() -> void:
	if cur_fighters[0].is_empty():
		return  # nothing simulated yet
	var font := ThemeDB.fallback_font
	var w: float = cfg.stage_half_width
	if map_tex.has(BACKGROUND):
		draw_texture_rect(map_tex[BACKGROUND], get_viewport_rect(), false)

	# Blast zones (leave this rectangle = lose a stock) and the stage.
	var bz_tl := to_screen(-cfg.blast_x, cfg.blast_top)
	var bz_br := to_screen(cfg.blast_x, cfg.blast_bottom)
	draw_rect(Rect2(bz_tl, bz_br - bz_tl), Color(1, 1, 1, 0.15), false, 2.0)
	# The main stage is a solid block: you can't pass through its sides or underside.
	draw_map("stage.png", -w, w, 0, -cfg.stage_depth)
	# Ledges: the two top corners you can hang from.
	for side in ([-1, 1] if SHOW_LEDGES else []):
		draw_circle(to_screen(side * w, 0), px * 0.3, Color(0.9, 0.9, 0.6))
	# Platforms (every floor after the first): thin, pass-through from below.
	for k in range(1, cfg.surfaces.size()):
		var s: Array = cfg.surfaces[k]
		draw_map("platform.png", s[0], s[1], s[2], s[2] - 1)

	var fighters := [shown(0), shown(1)]
	for i in 2:
		draw_fighter(i, fighters[i], font)
	for e in explosions:
		var k := mini(9, int((game.get_frame() - e.start) * EXPLOSION_FPS / 60))
		var c: Vector2 = cam_center + e.at * get_viewport_rect().size / 2 / px  # back to sim units, for the camera now
		if explosion_tex != null:
			draw_texture_rect_region(explosion_tex, Rect2(to_screen(c.x - EXPLOSION_SIZE / 2, c.y + EXPLOSION_SIZE / 2),
				Vector2(EXPLOSION_SIZE, EXPLOSION_SIZE) * px), Rect2(k * 128, 0, 128, 128))

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


# A map picture stretched over a sim rectangle (a grey box if it's missing).
func draw_map(name: String, x0: float, x1: float, y0: float, y1: float) -> void:
	var tl := to_screen(x0, y0)
	var rect := Rect2(tl, to_screen(x1, y1) - tl)
	if map_tex.has(name):
		draw_texture_rect(map_tex[name], rect, false)
	else:
		draw_rect(rect, Color(0.45, 0.45, 0.5))


func draw_fighter(i: int, f: Dictionary, font: Font) -> void:
	if f.respawn_wait > 0 or f.stocks == 0:
		return  # off the field after a KO
	var w: float = cfg.hurtbox_w
	var h: float = cfg.hurtbox_h
	var shake := 0.0
	if f.hitlag > 0 and f.state == ST.HITSTUN:
		shake = 0.18 * (1 if f.hitlag % 2 == 0 else -1)  # the victim shakes during the hit freeze
	# Tint: white = the sprite's own colors. Values above 1 brighten it.
	var tint := Color.WHITE
	if f.state == ST.HITSTUN:
		tint = Color(1.8, 1.8, 1.8)  # flash bright while stunned
	elif f.state == ST.GRABBED:
		tint = Color(0.8, 0.5, 1.0)
	elif f.state == ST.SHIELDBREAK:
		tint = Color.WHITE.lerp(Color(1.4, 1.3, 0.3), 0.5 + 0.3 * sin(f.state_frame * 0.4))  # dazed
	elif f.state == ST.ATTACK and f.charge > 0:
		# Charging a smash: glows brighter the longer it's held (up to 40% stronger).
		tint = Color.WHITE.lerp(Color(1.6, 1.5, 0.6), 0.3 + 0.7 * f.charge / cfg.charge_max * (0.75 + 0.25 * sin(f.charge * 0.8)))
	elif f.state == ST.REBOUND:
		tint = Color(0.45, 0.45, 0.45)  # clanked or parried: stuck, open to a punish
	elif f.state == ST.SHIELDDROP and f.state_frame < 5:
		tint = Color(0.7, 1.4, 1.4)  # the parry window
	elif f.state == ST.LEDGE and f.state_frame > cfg.ledge_hang_max - 60 and (f.state_frame / 4) % 2 == 0:
		tint = Color(1.5, 0.4, 0.4)  # hanging too long: about to let go
	if f.invuln > 0 and (f.invuln / 6) % 2 == 0:
		tint.a = 0.35  # blink while invulnerable after respawning
	if f.intangible and f.invuln == 0:
		tint.a = 0.3  # see-through while dodging: hits pass through

	# The sprite is drawn around its center, so it can be flipped, squashed
	# and rotated in place: flipped to face left, squashed when crouching,
	# lying on its side when knocked down, spinning while tumbling.
	var ch: Dictionary = chars[i]
	var size: float = ch.px * h / ch.ref  # the canvas, in sim units
	var body_h := h  # the body is the canvas's bottom ref_h px, centered on the pivot
	var center_y: float = f.y + h / 2
	var angle := 0.0
	var squash := 1.0
	if f.state == ST.KNOCKDOWN:
		angle = -f.facing * PI / 2
		center_y = f.y + ch.body_w / ch.ref * h / 2  # lying on its side on the floor
		w = h * 1.1
		h = cfg.lying_h
	elif f.state == ST.CROUCH:
		squash = cfg.crouch_h / h
		h = cfg.crouch_h
		center_y = f.y + h / 2
	elif f.tumble:
		angle = f.state_frame * 0.35
	var center := to_screen(f.x + shake, center_y)
	if ch.tex != null:
		var frame := sprite_frame(i, f)  # [texture, the region of it to draw]
		draw_set_transform(center, angle, Vector2(f.facing * ch.flip, squash))  # -1 mirrors it to face left
		draw_texture_rect_region(frame[0], Rect2(Vector2(-size / 2, body_h / 2 - size) * px, Vector2(size, size) * px), frame[1], tint)
		draw_set_transform(Vector2.ZERO)
	else:  # sprite missing: the old colored box
		var box := Color(COLORS[i], tint.a)
		draw_rect(Rect2(to_screen(f.x - w / 2 + shake, f.y + h), Vector2(w, h) * px), box)
	if f.intangible:
		# White outline of the hurtbox = intangible (i-frames).
		draw_rect(Rect2(to_screen(f.x - w / 2 + shake, f.y + h), Vector2(w, h) * px), Color.WHITE, false, 2.0)
	# Shield: a bubble that shrinks as the shield wears down.
	if f.state == ST.SHIELD or f.state == ST.SHIELDSTUN:
		var r: float = (0.7 + 1.5 * f.shield_hp / cfg.shield_max) * px
		var c := to_screen(f.x, f.y + cfg.hurtbox_h / 2)
		draw_circle(c, r, Color(COLORS[i], 0.3))
		draw_arc(c, r, 0, TAU, 40, Color(1, 1, 1, 0.8 if f.state == ST.SHIELDSTUN else 0.4), 2.0)
	# Small player number above the head.
	draw_string(font, to_screen(f.x - 0.5, f.y + cfg.hurtbox_h + 0.6), "P%d" % (i + 1), HORIZONTAL_ALIGNMENT_LEFT, -1, 14, COLORS[i])


func draw_hud(font: Font) -> void:
	var size := get_viewport_rect().size
	for i in 2:
		var f: Dictionary = cur_fighters[i]
		var x := size.x * (0.3 if i == 0 else 0.6)
		# Shield meter: a thin bar under the damage.
		draw_rect(Rect2(x, size.y - 6, 80.0 * f.shield_hp / cfg.shield_max, 3), Color(COLORS[i], 0.7))
		draw_string(font, Vector2(x, size.y - 40), "P%d  %d%%" % [i + 1, int(f.percent)], HORIZONTAL_ALIGNMENT_LEFT, -1, 30, COLORS[i])
		draw_string(font, Vector2(x, size.y - 66), chars[i].name, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, COLORS[i])
		for s in f.stocks:
			draw_circle(Vector2(x + 12 + s * 22, size.y - 18), 7, COLORS[i])
		draw_string(font, Vector2(x + 90, size.y - 14), state_label(f), HORIZONTAL_ALIGNMENT_LEFT, -1, 14)
		if f.combo >= 2:  # combo counter, like Smash's training mode
			draw_string(font, Vector2(x, size.y - 92), "%d HIT COMBO" % f.combo, HORIZONTAL_ALIGNMENT_LEFT, -1, 22, COLORS[i])

	var seconds_left: int = max(0, cfg.time_limit_frames - game.get_frame()) / 60
	draw_string(font, Vector2(size.x / 2 - 30, 30), "%d:%02d" % [seconds_left / 60, seconds_left % 60], HORIZONTAL_ALIGNMENT_LEFT, -1, 24)
	var dim := Color(1, 1, 1, 0.6)
	if replay.is_empty():
		# No help text during a fight (it's in the menu's Controls screen); a bot's
		# line only shows while it loads, or if something went wrong.
		for p in 2:
			if bot_spec[p] == "" or bot_state[p] == "ready":
				continue
			var status: String = bot_state[p]
			if bot_state[p] == "connecting" or bot_state[p] == "handshake":
				status = "waiting for the bot to load... (%d s)" % int(bot_wait[p])
			draw_string(font, Vector2(16, 24 + 20 * p), "P%d: %s (%s)" % [p + 1, bot_name(bot_spec[p]), status],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 14, dim if bot_state[p] in ["connecting", "handshake"] else Color(1, 0.3, 0.3))
	else:
		var status := "PAUSED" if paused else "%sx" % SPEEDS[speed_i]
		var line := "REPLAY  frame %d / %d   %s" % [replay_frame, replay.inputs.size(), status]
		draw_string(font, Vector2(16, 24), line, HORIZONTAL_ALIGNMENT_LEFT, -1, 14)
		draw_string(font, Vector2(16, 44), "Space: pause   Right: next frame (paused)   Up/Down: speed   R: restart   Esc: quit", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, dim)
		draw_string(font, Vector2(16, 64), "%s   %s" % [replay_name, JSON.stringify(replay.meta)], HORIZONTAL_ALIGNMENT_LEFT, -1, 14, dim)
		if replay_result == "verified":
			draw_string(font, Vector2(16, 84), "Replay verified: final state hash matches the recording", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0.5, 1, 0.5))
		elif replay_result == "DESYNC":
			draw_string(font, Vector2(16, 84), "DESYNC: final state differs from the recording (see docs/setup.md)", HORIZONTAL_ALIGNMENT_LEFT, -1, 16, Color(1, 0.3, 0.3))

	if load_error != "":
		draw_string(font, Vector2(16, 80), "Move file error: " + load_error, HORIZONTAL_ALIGNMENT_LEFT, size.x - 32, 18, Color(1, 0.3, 0.3))
	if frozen():
		draw_string(font, Vector2(0, size.y / 2 - 100), winner_text() + "   (R to restart, Esc for %s)" % ("the menu" if Session.from_menu else "quit"), HORIZONTAL_ALIGNMENT_CENTER, size.x, 40)


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


# A bot's display name: the difficulty it belongs to, or the file / bot name.
func bot_name(spec: String) -> String:
	for d in Session.DIFFICULTIES:
		if d.bot == spec:
			return d.name if d.name.ends_with("bot") else d.name + " bot"
	return "bot " + spec.get_file().get_basename()


func winner_text() -> String:
	var w: int = game.get_winner()  # same rule the RL side uses, from core/sim.cpp
	return "Draw" if w < 0 else "P%d wins!" % (w + 1)
