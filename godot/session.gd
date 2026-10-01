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
# decisions, then the newest run's final agent. Paths are relative to the repo.
const DIFFICULTIES := [
	{name = "Very easy", bot = "runs/selfplay__league__1__1790809954/agent_00250.pt"},
	{name = "Easy", bot = "runs/selfplay__league__1__1790809954/agent_01500.pt"},
	{name = "Normal", bot = "runs/selfplay__league__1__1790809954/agent_03000.pt"},
	{name = "Hard", bot = "runs/selfplay__league__1__1790809954/agent_06250.pt"},
	{name = "Very hard", bot = "runs/selfplay__league__1__1790809954/agent_10750.pt"},
	{name = "Latest bot", bot = "latest"},
]

const SETTINGS_FILE := "user://settings.cfg"  # in Godot's per-user folder (%APPDATA%\Godot\app_userdata\Bootleg SSM)

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


# The repository folder (the Godot project is its godot/ subfolder).
func repo_root() -> String:
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
