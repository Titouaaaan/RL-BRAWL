extends Node
## Session: what the menu and the game share. It's an *autoload* (see
## project.godot): Godot creates one instance at startup and keeps it alive
## across scene changes, so the menu can leave choices here for the game.
##
## The menu fills `args` with the same options play.bat passes on the command
## line (--bot1=..., --bot2=..., --p1=..., --replay=...), then opens the game;
## main.gd reads them from here instead of the command line.

# Bot difficulties: checkpoints of the best run so far, spaced along its Elo
# ladder (runs/ladder_league.txt): about 8M, 50M, 100M, 200M and 350M training
# decisions, then the newest run's final agent. Paths are relative to the repo;
# the weights are exported into data/bots/ (tools/export_bot.py), since runs/
# isn't in git.
const DIFFICULTIES := [
	{name = "Very easy", bot = "data/bots/agent_00250.bin"},
	{name = "Easy", bot = "data/bots/agent_01500.bin"},
	{name = "Normal", bot = "data/bots/agent_03000.bin"},
	{name = "Hard", bot = "data/bots/agent_06250.bin"},
	{name = "Very hard", bot = "data/bots/agent_10750.bin"},
	{name = "Latest bot", bot = "latest"},
]

const SETTINGS_FILE := "user://settings.cfg"  # in Godot's per-user folder (%APPDATA%\Godot\app_userdata\RL-BRAWL)

var from_menu := false  # the game was opened from the menu: Esc goes back to it
var args := PackedStringArray()
var p1_char := "1"  # saved character choices (Character Select)
var p2_char := "2"


func _ready() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(SETTINGS_FILE) == OK:
		p1_char = str(cfg.get_value("characters", "p1", "1"))
		p2_char = str(cfg.get_value("characters", "p2", "2"))


func save_characters() -> void:
	var cfg := ConfigFile.new()
	cfg.set_value("characters", "p1", p1_char)
	cfg.set_value("characters", "p2", p2_char)
	cfg.save(SETTINGS_FILE)


# The repository folder (the Godot project is its godot/ subfolder). A web
# export has no repo: tools/web_export.py packs the files the game reads
# into res://repo/, in the same layout.
func repo_root() -> String:
	if OS.has_feature("web"):
		return "res://repo"
	return ProjectSettings.globalize_path("res://").path_join("..").simplify_path()


# Opens the game with these options (same format as the command line).
func start_game(options: PackedStringArray) -> void:
	from_menu = true
	args = options
	args.append("--p1=" + p1_char)
	args.append("--p2=" + p2_char)
	get_tree().change_scene_to_file("res://main.tscn")


func back_to_menu() -> void:
	args = PackedStringArray()
	get_tree().change_scene_to_file("res://menu.tscn")
