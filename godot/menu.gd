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
	# Debug hook for screenshots/tests: MENU_SCREEN=difficulty|bots|characters|controls|replays opens that screen.
	var screens := {difficulty = show_difficulty, bots = show_bot_vs_bot, characters = show_characters,
		controls = show_controls, replays = show_replays}
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

# Clears the window and starts a new screen with a title.
func new_screen(title: String, subtitle := "") -> VBoxContainer:
	if screen != null:
		screen.get_parent().queue_free()
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	screen = VBoxContainer.new()
	screen.add_theme_constant_override("separation", 10)
	center.add_child(screen)
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
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", 13)
	var mono := SystemFont.new()
	mono.font_names = PackedStringArray(["Consolas", "Courier New", "monospace"])
	l.add_theme_font_override("font", mono)
	screen.add_child(l)
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
