extends Control
## The main menu (the first scene Godot opens). Each screen is built in code
## by one show_*() function: a column of buttons in the middle of the window.
## Works with the mouse, the keyboard (arrows + Enter) and controllers (d-pad
## or stick + Cross): Godot's buttons handle that by themselves.
##
## Starting a game goes through Session.start_game() with the same options
## play.bat passes on the command line (see session.gd).

const BG := Color(0.12, 0.13, 0.16)
const ACCENT := Color(0.35, 0.6, 1.0)
const SUB := Color(1, 1, 1, 0.6)

var screen: VBoxContainer  # the current screen's content
var back_action := Callable()  # what Esc (or the controller's Circle) does on this screen


func _ready() -> void:
	# play.bat with options (replay, --vs, --p1...) skips the menu and goes straight to the game.
	if OS.get_cmdline_user_args().size() > 0 and not Session.from_menu:
		get_tree().change_scene_to_file.call_deferred("res://main.tscn")
		return
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = BG
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	# Debug hook for screenshots/tests: MENU_SCREEN=difficulty|bots|characters|controls|replays|training|run opens that screen.
	var screens := {difficulty = show_difficulty, bots = show_bot_vs_bot, characters = show_characters,
		controls = show_controls, replays = show_replays, training = show_runs,
		run = func(): show_run(find_runs()[0].path)}
	var start: String = OS.get_environment("MENU_SCREEN")
	if screens.has(start):
		screens[start].call()
	else:
		show_main()


func _unhandled_input(event: InputEvent) -> void:
	var back: bool = (event is InputEventKey and event.pressed and event.physical_keycode == KEY_ESCAPE) \
		or (event is InputEventJoypadButton and event.pressed and event.button_index == JOY_BUTTON_B)
	if back and back_action.is_valid():
		back_action.call()
		get_viewport().set_input_as_handled()


# --- building blocks ---------------------------------------------------------

# Clears the window and starts a new screen with a title. fill: the screen
# takes the whole window (minus a margin) instead of sitting in the middle.
func new_screen(title: String, subtitle := "", fill := false) -> VBoxContainer:
	if screen != null:
		screen.get_parent().queue_free()
	var holder: Container = MarginContainer.new() if fill else CenterContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		holder.add_theme_constant_override("margin_" + side, 24)
	holder.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(holder)
	screen = VBoxContainer.new()
	screen.add_theme_constant_override("separation", 10)
	holder.add_child(screen)
	add_label(title, 40, ACCENT)
	if subtitle != "":
		add_label(subtitle, 16, SUB)
	screen.add_child(spacer(10))
	return screen


func add_label(text: String, size: int, color := Color.WHITE, parent: Control = null) -> Label:
	var l := Label.new()
	l.text = text
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	(parent if parent != null else screen).add_child(l)
	return l


func add_button(text: String, action: Callable, parent: Control = null) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(340, 44)
	b.add_theme_font_size_override("font_size", 20)
	b.pressed.connect(action)
	(parent if parent != null else screen).add_child(b)
	return b


func mono_label(text: String, size: int) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	var mono := SystemFont.new()
	mono.font_names = PackedStringArray(["Consolas", "Courier New", "monospace"])
	l.add_theme_font_override("font", mono)
	return l


func spacer(h: int) -> Control:
	var c := Control.new()
	c.custom_minimum_size = Vector2(0, h)
	return c


# --- screens -------------------------------------------------------------------

func show_main() -> void:
	new_screen("RL-BRAWL", "P1: %s    P2: %s" % [char_name(Session.p1_char), char_name(Session.p2_char)])
	add_button("Player vs Bot", show_difficulty).grab_focus()
	add_button("Player vs Player", func(): Session.start_game(PackedStringArray()))
	add_button("Bot vs Bot", show_bot_vs_bot)
	add_button("Character Select", show_characters)
	add_button("Controls", show_controls)
	screen.add_child(spacer(14))
	add_button("Replays", show_replays)
	add_button("Training", show_runs)
	add_button("Quit", func(): get_tree().quit())
	back_action = func(): get_tree().quit()


func show_difficulty() -> void:
	new_screen("Player vs Bot", "You are P1. Choose the bot's level:")
	var first := true
	for d in Session.DIFFICULTIES:
		var b := add_button(d.name, func(): Session.start_game(PackedStringArray(["--bot2=" + d.bot])))
		if first:
			b.grab_focus()
			first = false
	screen.add_child(spacer(10))
	add_button("Back", show_main)
	back_action = show_main


func show_bot_vs_bot() -> void:
	new_screen("Bot vs Bot", "Choose each bot's level, then Start.")
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 30)
	screen.add_child(row)
	var picks: Array[OptionButton] = []
	for p in 2:
		var col := VBoxContainer.new()
		row.add_child(col)
		add_label("P%d bot" % (p + 1), 20, Color(0.35, 0.6, 1.0) if p == 0 else Color(1.0, 0.45, 0.35), col)
		var opt := OptionButton.new()
		opt.custom_minimum_size = Vector2(220, 40)
		opt.add_theme_font_size_override("font_size", 18)
		for d in Session.DIFFICULTIES:
			opt.add_item(d.name)
		opt.select(Session.DIFFICULTIES.size() - 1)  # the latest bot by default
		col.add_child(opt)
		picks.append(opt)
	screen.add_child(spacer(10))
	add_button("Start", func():
		Session.start_game(PackedStringArray([
			"--bot1=" + Session.DIFFICULTIES[picks[0].selected].bot,
			"--bot2=" + Session.DIFFICULTIES[picks[1].selected].bot]))).grab_focus()
	add_button("Back", show_main)
	back_action = show_main


func show_characters() -> void:
	new_screen("Character Select", "Click a character for each player (saved automatically).")
	var manifest := load_manifest()
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 40)
	screen.add_child(row)
	for p in 2:
		var col := VBoxContainer.new()
		row.add_child(col)
		add_label("P%d" % (p + 1), 24, Color(0.35, 0.6, 1.0) if p == 0 else Color(1.0, 0.45, 0.35), col)
		var grid := GridContainer.new()
		grid.columns = 3
		col.add_child(grid)
		var chosen: String = Session.p1_char if p == 0 else Session.p2_char
		for id in manifest:
			var c: Dictionary = manifest[id]
			var b := Button.new()
			b.text = "%s  %s" % [id, c.name]
			b.custom_minimum_size = Vector2(150, 96)
			b.expand_icon = true
			b.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
			b.vertical_icon_alignment = VERTICAL_ALIGNMENT_TOP
			b.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
			b.icon = character_texture(c.file)
			b.toggle_mode = true
			b.button_pressed = (id == chosen)
			b.pressed.connect(func():
				if p == 0:
					Session.p1_char = id
				else:
					Session.p2_char = id
				Session.save_characters()
				show_characters())
			grid.add_child(b)
	screen.add_child(spacer(10))
	var back := add_button("Back", show_main)
	back.grab_focus()
	back_action = show_main


func show_controls() -> void:
	new_screen("Controls")
	var text := """KEYBOARD                       P1              P2
  Move                          W A S D         arrows
  Jump                          Space           Enter
  Light attack                  J               .
  Heavy attack                  K               /
  Shield / dodge                L               ,
  (keys by position: on AZERTY, WASD is ZQSD)

CONTROLLER (Smash Ultimate layout; PS4 names)
  Move                          left stick (or d-pad)
  Light attack                  Circle
  Heavy attack                  Cross, or the right stick + a direction
  Jump                          Triangle / Square
  Shield / dodge                L2 / R2
  Grab                          L1 / R1
  Restart                       Options

IN GAME
  R restart      Esc back to the menu
  light while shielding = grab, then a direction = throw, light = pummel
  hold heavy = charge a smash      tap jump = short hop
  shield + side = roll, + down = spot dodge, let go as a hit lands = parry"""
	screen.add_child(mono_label(text, 13))
	screen.add_child(spacer(10))
	add_button("Back", show_main).grab_focus()
	back_action = show_main


func show_replays() -> void:
	new_screen("Replays", "Newest first. Enter / double-click to watch. Space pauses, Up/Down change speed, Esc comes back.")
	var files := find_replays()
	var list := ItemList.new()
	list.custom_minimum_size = Vector2(900, 420)
	list.add_theme_font_size_override("font_size", 15)
	for f in files:
		var when := Time.get_datetime_string_from_unix_time(f.time, true)
		list.add_item("%s    %s    %s" % [when, f.label, replay_result(f.path)])
		list.set_item_metadata(list.item_count - 1, f.path)
	list.item_activated.connect(func(i):
		Session.start_game(PackedStringArray(["--replay=" + str(list.get_item_metadata(i))])))
	screen.add_child(list)
	add_label("%d replays (replays/ folder). Recordings from before a game change replay differently and show DESYNC at the end." % files.size(), 13, SUB)
	add_button("Back", show_main)
	if files.size() > 0:
		list.select(0)
		list.grab_focus()
	back_action = show_main


# --- training ------------------------------------------------------------------
# Reads runs/<run>/summary.json, written by tools/run_summary.py (Godot can't
# read TensorBoard files). Opening the list starts that tool in the background
# so summaries of new or still-training runs catch up; Refresh re-reads them.

const PALETTE := [Color(0.35, 0.6, 1.0), Color(1.0, 0.55, 0.3), Color(0.4, 0.85, 0.5),
	Color(0.85, 0.45, 0.85), Color(0.95, 0.8, 0.3), Color(0.5, 0.85, 0.9)]

var summary_pid := 0


func show_runs() -> void:
	if summary_pid == 0:
		summary_pid = OS.create_process("python", [Session.repo_root().path_join("tools/run_summary.py")])
	new_screen("Training", "Every training run in runs/, newest first. Enter / double-click to open one.", true)
	var runs := find_runs()
	var bots := Session.DIFFICULTIES.map(func(d): return d.bot.get_slice("/", 1))
	var list := ItemList.new()
	list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	list.add_theme_font_size_override("font_size", 15)
	for r in runs:
		var s = r.summary
		var name: String = r.path.get_file()
		if s == null:
			list.add_item("%s    (summary being built, press Refresh in a minute)" % name)
		else:
			var best = s.elo.map(func(e): return e[0]).max()
			list.add_item("%s    %-34s %5.0fM decisions   %4.1f h   gen %3d   Elo %s%s" % [
				Time.get_date_string_from_unix_time(s.started), name, s.global_step / 1e6, s.wall_hours,
				s.generation, "-" if best == null else str(int(best)), "   (bot levels)" if name in bots else ""])
		list.set_item_metadata(list.item_count - 1, r.path if s != null else "")
	list.item_activated.connect(func(i):
		if list.get_item_metadata(i) != "":
			show_run(list.get_item_metadata(i)))
	screen.add_child(list)
	add_label("Elo from runs/ladder*.txt (python tools/ladder.py); runs never laddered show -.", 13, SUB)
	add_button("Refresh", show_runs)
	add_button("Back", show_main)
	if runs.size() > 0:
		list.select(0)
		list.grab_focus()
	back_action = show_main


func show_run(path: String) -> void:
	var s: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path.path_join("summary.json")))
	var a: Dictionary = s.args
	var batch: int = a.get("batch_size", 1)
	new_screen(s.name, "%.0fM decisions in %.1f h    %d iterations    %d generations    %d checkpoints    started %s" % [
		s.global_step / 1e6, s.wall_hours, s.iteration, s.generation, s.checkpoints,
		Time.get_datetime_string_from_unix_time(s.started, true)], true)
	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	screen.add_child(tabs)

	# Overview: how it was trained (left), how good it got (middle), all settings (right).
	var ov := HBoxContainer.new()
	ov.name = "Overview"
	ov.add_theme_constant_override("separation", 20)
	tabs.add_child(ov)
	var left := column(ov, 400, 1)
	section("Training regime", left)
	left.add_child(wrapped(regime(a), 400))
	section("Network", left)
	var net := ""
	for part in ["actor", "critic"]:
		var dims := []
		for l in s.network.layers:
			if l[0].begins_with(part):
				if dims.is_empty():
					dims.append(l[1][1])
				dims.append(l[1][0])
		net += "%s: %s\n" % [part, " -> ".join(dims.map(func(d): return str(int(d))))]
	left.add_child(wrapped(net + "separate MLPs, tanh, %.1fk parameters" % (s.network.params / 1000.0), 400))
	section("Final agent vs the ladder (win rate)", left)
	for h in s.head_to_head:
		left.add_child(mono_label("%-22s %s %.2f" % [h[0], "#".repeat(roundi(h[1] * 20)).rpad(20, "."), h[1]], 13))
	if s.head_to_head.is_empty():
		left.add_child(wrapped("Not laddered yet: python tools/ladder.py %s --every 6" % path.get_file(), 400))

	var mid := column(ov, 440, 1.3)
	var elo := []
	for e in s.elo:
		elo.append([(s.iteration if e[1] == "latest" else int(e[1])) * batch, e[0]])
	mid.add_child(chart("Elo of checkpoints", [elo], [], Vector2(0, 200), true))
	var evals: Array = s.curves.keys().filter(func(t): return t.begins_with("eval/"))
	mid.add_child(chart("Eval win rate", evals.map(func(t): return s.curves[t]),
		evals.map(func(t): return t.trim_prefix("eval/win_vs_")), Vector2(0, 200), true))

	var right := column(ov, 340, 0.8)
	section("Settings", right)
	var keys: Array = a.keys()
	keys.sort()
	right.add_child(mono_label("\n".join(keys.map(func(k): return "%-18s %s" % [k, str(int(a[k])) if a[k] is float and a[k] == floorf(a[k]) else str(a[k])])), 12))

	# One tab per TensorBoard group (charts/, losses/, mech/, ...), a grid of every curve.
	var groups := {}
	for tag in s.curves:
		groups.get_or_add(tag.get_slice("/", 0), []).append(tag)
	for g in groups:
		var scroll := ScrollContainer.new()
		scroll.name = g.capitalize()
		tabs.add_child(scroll)
		var grid := GridContainer.new()
		grid.columns = maxi(3, int(get_viewport_rect().size.x / 480))  # more charts per row on a big window
		grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		grid.add_theme_constant_override("h_separation", 16)
		scroll.add_child(grid)
		for tag in groups[g]:
			grid.add_child(chart(tag.get_slice("/", 1), [s.curves[tag]], [], Vector2(0, maxf(165, get_viewport_rect().size.y * 0.24))))

	tabs.current_tab = int(OS.get_environment("MENU_TAB"))  # debug hook, like MENU_SCREEN
	add_button("Back", show_runs)
	tabs.get_tab_bar().grab_focus()  # left/right switch tabs
	back_action = show_runs


# A scrollable column of the overview: at least `width` wide, then shares the
# window's extra width with the other columns in proportion to `ratio`.
func column(parent: Control, width: int, ratio: float) -> VBoxContainer:
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(width, 0)
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_stretch_ratio = ratio
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	parent.add_child(scroll)
	var col := VBoxContainer.new()
	col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	col.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.add_child(col)
	return col


func section(title: String, parent: Control) -> void:
	var l := add_label(title, 18, ACCENT, parent)
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT


func wrapped(text: String, width: int) -> Label:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(width, 0)
	l.add_theme_font_size_override("font_size", 14)
	return l


# The run's settings in words (from the args it was started with).
func regime(a: Dictionary) -> String:
	var t := "Opponent: %s" % a.get("opponent", "?")
	if a.get("opponent") == "selfplay":
		t += ". A frozen copy is replaced when the learner wins %d%% of %d+ games vs it (or after %d iterations). Envs: %d%% retired copies, %d%% scripted bots" % [
			a.gate_win_rate * 100, a.gate_min_games, a.gate_max_iters, a.pool_frac * 100, a.bot_frac * 100]
		if a.get("league", "") != "":
			t += ", %d%% the league %s" % [a.league_frac * 100, a.league.get_file()]
		t += ", the rest the frozen copy."
	t += "\n%.0fM decisions; %d envs x %d steps per update; %d epochs x %d minibatches." % [
		a.total_timesteps / 1e6, a.num_envs, a.num_steps, a.update_epochs, a.num_minibatches]
	t += "\nlr %s%s, gamma %s, lambda %s, clip %s, entropy %s -> %s." % [
		a.learning_rate, " (annealed)" if a.anneal_lr else "", a.gamma, a.gae_lambda, a.clip_coef,
		a.ent_coef, a.get("ent_coef_final", a.ent_coef)]
	t += "\nRewards: KO +-1, damage %s, approach %s, counter %s, timeout +-%s; shaping annealed to %s." % [
		a.get("damage_reward", 0), a.get("approach_reward", 0), a.get("counter_reward", 0),
		a.get("timeout_reward", 0), a.get("shaping_floor", 0)]
	t += "\nReaction delay %d frames, frame skip %d." % [a.get("reaction_delay", 0), a.get("frame_skip", 1)]
	return t


# A line chart: title with the last value(s), then the curves ([[step, value], ...])
# painted by a plain Control's draw signal (Godot has no chart widget).
# Charts always fill their width; tall ones also share the leftover height.
func chart(title: String, series: Array, names := [], size := Vector2(0, 165), tall := false) -> Control:
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	if tall:
		box.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var last := "" if names.size() > 1 else fmt(series[0][-1][1]) if series[0].size() > 0 else ""
	var l := add_label("%s   %s" % [title, last], 14, Color.WHITE, box)
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	var c := Control.new()
	c.custom_minimum_size = size
	c.size_flags_vertical = Control.SIZE_EXPAND_FILL
	c.draw.connect(func(): draw_chart(c, series, names))
	box.add_child(c)
	return box


func draw_chart(c: Control, series: Array, names: Array) -> void:
	var font := ThemeDB.fallback_font
	var r := Rect2(Vector2(46, 4), c.size - Vector2(52, 22 if names.is_empty() else 40))  # room for axis labels, legend
	c.draw_rect(r, Color(1, 1, 1, 0.04))
	var lo := INF
	var hi := -INF
	var x0 := INF
	var x1 := -INF
	for s in series:
		for p in s:
			lo = minf(lo, p[1])
			hi = maxf(hi, p[1])
			x0 = minf(x0, p[0])
			x1 = maxf(x1, p[0])
	if lo == INF:
		return
	if hi - lo < 1e-9:
		lo -= 0.5
		hi += 0.5
	if x1 == x0:
		x1 = x0 + 1
	var to_px := func(p): return Vector2(r.position.x + (p[0] - x0) / (x1 - x0) * r.size.x,
		r.end.y - (p[1] - lo) / (hi - lo) * r.size.y)
	for k in 3:  # gridlines at min, middle, max
		var v := lo + (hi - lo) * k / 2.0
		var y: float = to_px.call([x0, v]).y
		c.draw_line(Vector2(r.position.x, y), Vector2(r.end.x, y), Color(1, 1, 1, 0.08))
		c.draw_string(font, Vector2(0, y + 4), fmt(v), HORIZONTAL_ALIGNMENT_RIGHT, 42, 11, SUB)
	c.draw_string(font, Vector2(r.position.x, r.end.y + 15), "%.0fM" % (x0 / 1e6), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, SUB)
	c.draw_string(font, Vector2(r.end.x - 90, r.end.y + 15), "%.0fM steps" % (x1 / 1e6), HORIZONTAL_ALIGNMENT_RIGHT, 90, 11, SUB)
	var legend_x := r.position.x
	for i in series.size():
		var col: Color = PALETTE[i % PALETTE.size()]
		var pts := PackedVector2Array(series[i].map(to_px))
		if pts.size() > 1:
			c.draw_polyline(pts, col)  # thin lines: thick ones leave gaps on steep zigzags
		if pts.size() < 30:  # sparse data (Elo): mark the points
			for p in pts:
				c.draw_circle(p, 3, col)
		if i < names.size():
			c.draw_string(font, Vector2(legend_x, c.size.y - 3), names[i], HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col)
			legend_x += font.get_string_size(names[i], HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x + 16


func fmt(v: float) -> String:
	if absf(v) >= 1e6:
		return "%.1fM" % (v / 1e6)
	if absf(v) >= 1000:
		return "%.0f" % v
	if absf(v) >= 10:
		return "%.1f" % v
	if absf(v) < 0.01 and v != 0:
		return String.num_scientific(snappedf(v, pow(10, floorf(log(absf(v)) / log(10)) - 1)))
	return "%.3f" % v


# Every run folder that has a TensorBoard log, newest first: [{path, summary or null}].
func find_runs() -> Array:
	var root := Session.repo_root().path_join("runs")
	var out := []
	for d in DirAccess.get_directories_at(root):
		var path := root.path_join(d)
		if Array(DirAccess.get_files_at(path)).any(func(f): return f.begins_with("events.out.tfevents")):
			var json := path.path_join("summary.json")
			out.append({path = path, summary = JSON.parse_string(FileAccess.get_file_as_string(json)) if FileAccess.file_exists(json) else null})
	out.sort_custom(func(x, y): return x.path.get_slice("__", 3) > y.path.get_slice("__", 3))  # run names end in their start time
	return out


# --- data ----------------------------------------------------------------------

func load_manifest() -> Dictionary:
	var path := Session.repo_root().path_join("assets/characters/characters.json")
	var m = JSON.parse_string(FileAccess.get_file_as_string(path))
	return m if typeof(m) == TYPE_DICTIONARY else {}


func char_name(id: String) -> String:
	var m := load_manifest()
	return m[id].name if m.has(id) else "?"


func character_texture(file: String) -> Texture2D:
	var img := Image.load_from_file(Session.repo_root().path_join("assets/characters").path_join(file))
	return ImageTexture.create_from_image(img) if img != null else null


# Who won a recorded game, read from the start of the file (the result comes
# before the long list of inputs, so there's no need to load the whole file).
func replay_result(path: String) -> String:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return ""
	var head := file.get_buffer(mini(4096, file.get_length())).get_string_from_ascii()
	var winner := RegEx.create_from_string("\"winner\":([-0-9]+)").search(head)
	if winner == null:
		return ""
	var names := []
	for key in ["p1", "p2"]:
		var m := RegEx.create_from_string("\"%s\":\"([^\"]*)\"" % key).search(head)
		names.append(m.get_string(1) if m != null else key.to_upper())
	match int(winner.get_string(1)):
		0:
			return "->  P1 %s won" % names[0]
		1:
			return "->  P2 %s won" % names[1]
	return "->  draw"


# Every .json under replays/, newest first: [{path, label, time}].
func find_replays() -> Array:
	var root := Session.repo_root().path_join("replays")
	var out := []
	var stack := [root]
	while not stack.is_empty():
		var dir: String = stack.pop_back()
		for sub in DirAccess.get_directories_at(dir):
			stack.append(dir.path_join(sub))
		for f in DirAccess.get_files_at(dir):
			if f.ends_with(".json"):
				var path := dir.path_join(f)
				out.append({path = path, label = path.trim_prefix(root + "/"), time = FileAccess.get_modified_time(path)})
	out.sort_custom(func(a, b): return a.time > b.time)
	return out
