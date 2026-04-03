import {
	Button,
	Color,
	Control,
	GraphEdit,
	GraphNode,
	GridContainer,
	Label,
	OptionButton,
	RichTextLabel,
	StyleBoxFlat,
	TextEdit,
	Vector2,
} from "godot";

type Cell = { x: number; y: number };

export default class BattleAiEditorDemo extends Control {

	private readonly BOARD_SIZE = 6;
	private readonly ENEMY_MAX_HP = 3;

	private readonly TILE_EMPTY = 0;
	private readonly TILE_BLOCK = 1;
	private readonly TILE_ALLY = 2;
	private readonly TILE_ENEMY = 3;

	private readonly ACTION_MOVE = "move";
	private readonly ACTION_ATTACK = "attack";
	private readonly ACTION_WAIT = "wait";

	private readonly START_NODE_NAME = "Start";
	private readonly DEFAULT_BLOCKS: Cell[] = [
		{ x: 2, y: 1 },
		{ x: 2, y: 2 },
		{ x: 2, y: 3 },
		{ x: 3, y: 3 },
	];
	private readonly CARDINAL_DIRS: Cell[] = [
		{ x: 1, y: 0 },
		{ x: -1, y: 0 },
		{ x: 0, y: 1 },
		{ x: 0, y: -1 },
	];

	private graph_edit!: GraphEdit;
	private add_move_button!: Button;
	private add_attack_button!: Button;
	private add_wait_button!: Button;
	private export_button!: Button;

	private brush_option!: OptionButton;
	private board_grid!: GridContainer;
	private run_step_button!: Button;
	private reset_button!: Button;
	private status_label!: Label;
	private log_output!: RichTextLabel;
	private json_output!: TextEdit;

	private board: number[][] = [];
	private tile_buttons: any[] = [];
	private action_by_node: Record<string, string> = {};

	private brush_mode = this.TILE_BLOCK;
	private ally_pos: Cell = { x: 0, y: 0 };
	private enemy_pos: Cell = { x: 0, y: 0 };
	private enemy_hp = this.ENEMY_MAX_HP;
	private turn_count = 1;
	private action_serial = 0;

	_ready(): void {
		this.graph_edit = this.get_node(
			"MainSplit/LeftPanel/GraphEdit",
		) as GraphEdit;
		this.add_move_button = this.get_node(
			"MainSplit/LeftPanel/Toolbar/AddMoveButton",
		) as Button;
		this.add_attack_button = this.get_node(
			"MainSplit/LeftPanel/Toolbar/AddAttackButton",
		) as Button;
		this.add_wait_button = this.get_node(
			"MainSplit/LeftPanel/Toolbar/AddWaitButton",
		) as Button;
		this.export_button = this.get_node(
			"MainSplit/LeftPanel/Toolbar/ExportButton",
		) as Button;

		this.brush_option = this.get_node(
			"MainSplit/RightPanel/BrushBar/BrushOption",
		) as OptionButton;
		this.board_grid = this.get_node(
			"MainSplit/RightPanel/BoardScroll/BoardGrid",
		) as GridContainer;
		if (!this.board_grid) {
			this.board_grid = this.get_node(
				"MainSplit/RightPanel/BoardGrid",
			) as GridContainer;
		}
		this.run_step_button = this.get_node(
			"MainSplit/RightPanel/ActionBar/RunStepButton",
		) as Button;
		this.reset_button = this.get_node(
			"MainSplit/RightPanel/ActionBar/ResetButton",
		) as Button;
		this.status_label = this.get_node(
			"MainSplit/RightPanel/StatusLabel",
		) as Label;
		this.log_output = this.get_node(
			"MainSplit/RightPanel/LogOutput",
		) as RichTextLabel;
		this.json_output = this.get_node(
			"MainSplit/RightPanel/JsonOutput",
		) as TextEdit;

		this.add_move_button.pressed.connect(() => this._on_add_move_pressed());
		this.add_attack_button.pressed.connect(() => this._on_add_attack_pressed());
		this.add_wait_button.pressed.connect(() => this._on_add_wait_pressed());
		this.export_button.pressed.connect(() => this._on_export_pressed());

		this.brush_option.item_selected.connect((index: number) =>
			this._on_brush_selected(index),
		);
		this.run_step_button.pressed.connect(() => this._on_run_step_pressed());
		this.reset_button.pressed.connect(() => this._on_reset_pressed());

		this.graph_edit.connection_request.connect(
			(
				from_node: string,
				from_port: number,
				to_node: string,
				to_port: number,
			) => this._on_connection_request(from_node, from_port, to_node, to_port),
		);
		this.graph_edit.disconnection_request.connect(
			(
				from_node: string,
				from_port: number,
				to_node: string,
				to_port: number,
			) =>
				this._on_disconnection_request(from_node, from_port, to_node, to_port),
		);

		this._init_brush_options();
		this._build_board_buttons();
		this._setup_demo_graph();
		this._reset_board_state();
		this._refresh_board_view();
		this._refresh_status();

		this.json_output.editable = false;
		this._append_log(
			"最小 Demo 已加载。先用连线调整 AI，再点击“执行一步 AI”。",
		);
	}

	private _init_brush_options(): void {
		this.brush_option.clear();
		this.brush_option.add_item("空地", this.TILE_EMPTY);
		this.brush_option.add_item("障碍", this.TILE_BLOCK);
		this.brush_option.add_item("友军起点", this.TILE_ALLY);
		this.brush_option.add_item("敌军位置", this.TILE_ENEMY);
		this.brush_option.select(1);
		this.brush_mode = this.brush_option.get_item_id(1);
	}

	private _build_board_buttons(): void {
		for (const child of this.board_grid.get_children()) {
			this.board_grid.remove_child(child);
			child.queue_free();
		}

		this.tile_buttons = [];
		this.board_grid.columns = this.BOARD_SIZE;

		for (let i = 0; i < this.BOARD_SIZE * this.BOARD_SIZE; i++) {
			const tile_button = new Button();
			tile_button.focus_mode = Control.FocusMode.FOCUS_NONE;
			tile_button.custom_minimum_size = new Vector2(56, 56);
			tile_button.pressed.connect(() => this._on_tile_pressed(i));
			this.board_grid.add_child(tile_button);
			this.tile_buttons.push(tile_button);
		}
	}

	private _setup_demo_graph(): void {
		for (const child of this.graph_edit.get_children()) {
			if (!(child instanceof GraphNode)) {
				continue;
			}
			this.graph_edit.remove_child(child);
			child.queue_free();
		}

		this.graph_edit.clear_connections();
		this.action_by_node = {};
		this.action_serial = 0;

		const start_node = this._create_start_node();
		const attack_node = this._create_action_node(
			this.ACTION_ATTACK,
			"Attack If Adjacent",
			new Vector2(300, 90),
		);
		const move_node = this._create_action_node(
			this.ACTION_MOVE,
			"Move To Enemy",
			new Vector2(610, 90),
		);
		const wait_node = this._create_action_node(
			this.ACTION_WAIT,
			"Wait",
			new Vector2(920, 90),
		);

		this._safe_connect(start_node.name, attack_node.name, false);
		this._safe_connect(attack_node.name, move_node.name, false);
		this._safe_connect(move_node.name, wait_node.name, false);
	}

	private _create_start_node(): any {
		const node = new GraphNode();
		node.name = this.START_NODE_NAME;
		node.title = "Start";
		node.position_offset = new Vector2(40, 90);
		node.custom_minimum_size = new Vector2(220, 84);

		const label = new Label();
		label.text = "AI 入口节点";
		label.horizontal_alignment = 1;
		label.vertical_alignment = 1;
		node.add_child(label);

		node.set_slot(
			0,
			false,
			0,
			new Color(1, 1, 1),
			true,
			0,
			new Color(0.9, 0.9, 0.9),
		);
		this.graph_edit.add_child(node);
		return node;
	}

	private _create_action_node(action: string, title: string, offset: any): any {
		this.action_serial += 1;
		const node = new GraphNode();
		node.name = `${this._action_prefix(action)}_${this.action_serial}`;
		node.title = title;
		node.position_offset = offset;
		node.custom_minimum_size = new Vector2(240, 110);

		const body = new Label();
		body.text = this._action_hint(action);
		body.horizontal_alignment = 1;
		body.vertical_alignment = 1;
		body.custom_minimum_size = new Vector2(220, 68);
		node.add_child(body);

		node.set_slot(
			0,
			true,
			0,
			new Color(0.95, 0.95, 0.95),
			true,
			0,
			this._action_color(action),
		);
		this.graph_edit.add_child(node);
		this.action_by_node[node.name] = action;
		return node;
	}

	private _action_prefix(action: string): string {
		if (action === this.ACTION_MOVE) return "Move";
		if (action === this.ACTION_ATTACK) return "Attack";
		if (action === this.ACTION_WAIT) return "Wait";
		return "Action";
	}

	private _action_hint(action: string): string {
		if (action === this.ACTION_MOVE) return "朝敌军最近可达格推进 1 格";
		if (action === this.ACTION_ATTACK)
			return "与敌军相邻时攻击，未相邻则继续往下走";
		if (action === this.ACTION_WAIT) return "本回合不执行动作";
		return "未知动作";
	}

	private _action_color(action: string): any {
		if (action === this.ACTION_MOVE) return new Color(0.25, 0.57, 0.87);
		if (action === this.ACTION_ATTACK) return new Color(0.87, 0.31, 0.26);
		if (action === this.ACTION_WAIT) return new Color(0.53, 0.53, 0.53);
		return new Color(1, 1, 1);
	}

	private _on_connection_request(
		from_node: string,
		from_port: number,
		to_node: string,
		to_port: number,
	): void {
		if (from_port !== 0 || to_port !== 0) {
			return;
		}

		const from_name = String(from_node);
		const to_name = String(to_node);
		if (from_name === to_name) {
			return;
		}

		this._safe_connect(from_name, to_name, true);
	}

	private _on_disconnection_request(
		from_node: string,
		from_port: number,
		to_node: string,
		to_port: number,
	): void {
		this.graph_edit.disconnect_node(
			String(from_node),
			from_port,
			String(to_node),
			to_port,
		);
		this._append_log(`已删除连线: ${String(from_node)} -> ${String(to_node)}`);
	}

	private _safe_connect(
		from_name: string,
		to_name: string,
		log_event = true,
	): void {
		for (const item of this.graph_edit.get_connection_list()) {
			const old_from = String(item.from_node ?? "");
			const old_from_port = Number(item.from_port ?? -1);
			if (old_from === from_name && old_from_port === 0) {
				const old_to = String(item.to_node ?? "");
				const old_to_port = Number(item.to_port ?? 0);
				this.graph_edit.disconnect_node(from_name, 0, old_to, old_to_port);
			}
		}

		if (this.graph_edit.is_node_connected(from_name, 0, to_name, 0)) {
			return;
		}

		const err = this.graph_edit.connect_node(from_name, 0, to_name, 0);
		if (err === 0) {
			if (log_event) {
				this._append_log(`已连接: ${from_name} -> ${to_name}`);
			}
		} else {
			this._append_log(`连接失败: ${from_name} -> ${to_name}`);
		}
	}

	private _on_add_move_pressed(): void {
		const node = this._create_action_node(
			this.ACTION_MOVE,
			"Move To Enemy",
			this._next_node_offset(),
		);
		this._append_log(`新增节点: ${node.name}`);
	}

	private _on_add_attack_pressed(): void {
		const node = this._create_action_node(
			this.ACTION_ATTACK,
			"Attack If Adjacent",
			this._next_node_offset(),
		);
		this._append_log(`新增节点: ${node.name}`);
	}

	private _on_add_wait_pressed(): void {
		const node = this._create_action_node(
			this.ACTION_WAIT,
			"Wait",
			this._next_node_offset(),
		);
		this._append_log(`新增节点: ${node.name}`);
	}

	private _next_node_offset(): any {
		const column = this.action_serial % 3;
		const row = Math.floor(this.action_serial / 3);
		return new Vector2(300 + 300 * column, 260 + 170 * row);
	}

	private _on_export_pressed(): void {
		const graph_nodes: Record<string, any>[] = [];
		for (const child of this.graph_edit.get_children()) {
			if (!(child instanceof GraphNode)) {
				continue;
			}

			graph_nodes.push({
				name: child.name,
				title: child.title,
				action: this.action_by_node[child.name] ?? "start",
				position: {
					x: child.position_offset.x,
					y: child.position_offset.y,
				},
			});
		}

		const connections: Record<string, any>[] = [];
		for (const item of this.graph_edit.get_connection_list()) {
			connections.push({
				from_node: String(item.from_node ?? ""),
				from_port: Number(item.from_port ?? 0),
				to_node: String(item.to_node ?? ""),
				to_port: Number(item.to_port ?? 0),
			});
		}

		const payload = {
			board_size: this.BOARD_SIZE,
			board: this.board,
			ally_pos: this.ally_pos,
			enemy_pos: this.enemy_pos,
			enemy_hp: this.enemy_hp,
			ai_graph: {
				nodes: graph_nodes,
				connections,
			},
		};

		this.json_output.text = JSON.stringify(payload, null, 2);
		this._append_log("已导出当前配置到下方 JSON。");
	}

	private _on_brush_selected(index: number): void {
		this.brush_mode = this.brush_option.get_item_id(index);
		this._refresh_status();
	}

	private _on_tile_pressed(index: number): void {
		const cell = {
			x: index % this.BOARD_SIZE,
			y: Math.floor(index / this.BOARD_SIZE),
		};
		this._apply_brush(cell);
		this._refresh_board_view();
		this._refresh_status();
	}

	private _apply_brush(cell: Cell): void {
		if (!this._is_in_bounds(cell)) {
			return;
		}

		const current_tile = this._tile_at(cell);
		if (
			this.brush_mode === this.TILE_EMPTY ||
			this.brush_mode === this.TILE_BLOCK
		) {
			if (current_tile === this.TILE_ALLY || current_tile === this.TILE_ENEMY) {
				this._append_log("单位格不能直接刷成地形，请改用单位刷子移动。");
				return;
			}
			this._set_tile(cell, this.brush_mode);
			return;
		}

		if (this.brush_mode === this.TILE_ALLY) {
			if (current_tile === this.TILE_BLOCK) {
				this._append_log("友军不能放在障碍格。");
				return;
			}
			if (this._same_cell(cell, this.enemy_pos)) {
				this._append_log("该格已有敌军。");
				return;
			}
			this._set_tile(this.ally_pos, this.TILE_EMPTY);
			this.ally_pos = { x: cell.x, y: cell.y };
			this._set_tile(this.ally_pos, this.TILE_ALLY);
			return;
		}

		if (this.brush_mode === this.TILE_ENEMY) {
			if (current_tile === this.TILE_BLOCK) {
				this._append_log("敌军不能放在障碍格。");
				return;
			}
			if (this._same_cell(cell, this.ally_pos)) {
				this._append_log("该格已有友军。");
				return;
			}
			this._set_tile(this.enemy_pos, this.TILE_EMPTY);
			this.enemy_pos = { x: cell.x, y: cell.y };
			this.enemy_hp = this.ENEMY_MAX_HP;
			this._set_tile(this.enemy_pos, this.TILE_ENEMY);
		}
	}

	private _on_run_step_pressed(): void {
		if (this.enemy_hp <= 0) {
			this._append_log("敌军已被击败，先点击重置。");
			return;
		}

		const action = this._resolve_action_from_graph();
		if (action === this.ACTION_ATTACK) {
			this._do_attack();
		} else if (action === this.ACTION_MOVE) {
			const moved = this._do_move();
			if (!moved) {
				this._append_log(
					`第 ${this.turn_count} 回合: 无法靠近敌军，改为等待。`,
				);
			}
		} else {
			this._append_log(`第 ${this.turn_count} 回合: Wait。`);
		}

		this.turn_count += 1;
		this._refresh_board_view();
		this._refresh_status();
	}

	private _resolve_action_from_graph(): string {
		let cursor = this._get_first_outgoing(this.START_NODE_NAME);
		const visited = new Set<string>();
		let guard = 0;

		while (cursor !== "" && guard < 24) {
			guard += 1;
			if (visited.has(cursor)) {
				break;
			}
			visited.add(cursor);

			const action = this.action_by_node[cursor] ?? "";
			if (action === this.ACTION_ATTACK) {
				if (this._is_adjacent(this.ally_pos, this.enemy_pos)) {
					return this.ACTION_ATTACK;
				}
				cursor = this._get_first_outgoing(cursor);
				continue;
			}
			if (action === this.ACTION_MOVE) {
				return this.ACTION_MOVE;
			}
			if (action === this.ACTION_WAIT) {
				return this.ACTION_WAIT;
			}
			cursor = this._get_first_outgoing(cursor);
		}

		return this.ACTION_WAIT;
	}

	private _get_first_outgoing(node_name: string): string {
		const options: string[] = [];
		for (const item of this.graph_edit.get_connection_list()) {
			const from_node = String(item.from_node ?? "");
			const from_port = Number(item.from_port ?? -1);
			if (from_node === node_name && from_port === 0) {
				options.push(String(item.to_node ?? ""));
			}
		}

		if (options.length === 0) {
			return "";
		}
		options.sort();
		return options[0];
	}

	private _do_attack(): void {
		if (!this._is_adjacent(this.ally_pos, this.enemy_pos)) {
			this._append_log(`第 ${this.turn_count} 回合: Attack 失败（未相邻）。`);
			return;
		}

		this.enemy_hp = Math.max(this.enemy_hp - 1, 0);
		this._append_log(
			`第 ${this.turn_count} 回合: Attack 命中，敌军 HP = ${this.enemy_hp}。`,
		);
		if (this.enemy_hp <= 0) {
			this._set_tile(this.enemy_pos, this.TILE_EMPTY);
			this._append_log("敌军被击败。");
		}
	}

	private _do_move(): boolean {
		const next_step = this._find_next_step_to_enemy();
		if (!next_step || this._same_cell(next_step, this.ally_pos)) {
			return false;
		}

		this._set_tile(this.ally_pos, this.TILE_EMPTY);
		this.ally_pos = next_step;
		this._set_tile(this.ally_pos, this.TILE_ALLY);
		this._append_log(
			`第 ${this.turn_count} 回合: Move 到 (${this.ally_pos.x}, ${this.ally_pos.y})。`,
		);
		return true;
	}

	private _find_next_step_to_enemy(): Cell | null {
		const target_cells: Cell[] = [];
		for (const dir of this.CARDINAL_DIRS) {
			const candidate = {
				x: this.enemy_pos.x + dir.x,
				y: this.enemy_pos.y + dir.y,
			};
			if (
				this._is_in_bounds(candidate) &&
				this._tile_at(candidate) !== this.TILE_BLOCK
			) {
				target_cells.push(candidate);
			}
		}

		if (target_cells.length === 0) {
			return null;
		}

		const target_keys = new Set(target_cells.map((c) => this._cell_key(c)));
		const queue: Cell[] = [{ x: this.ally_pos.x, y: this.ally_pos.y }];
		const visited = new Set<string>([this._cell_key(this.ally_pos)]);
		const parent = new Map<string, string>();
		parent.set(this._cell_key(this.ally_pos), this._cell_key(this.ally_pos));

		while (queue.length > 0) {
			const current = queue.shift() as Cell;
			const current_key = this._cell_key(current);
			if (target_keys.has(current_key)) {
				return this._rebuild_first_step(parent, current_key);
			}

			for (const dir of this.CARDINAL_DIRS) {
				const next = { x: current.x + dir.x, y: current.y + dir.y };
				const next_key = this._cell_key(next);
				if (!this._is_in_bounds(next) || visited.has(next_key)) {
					continue;
				}
				const tile = this._tile_at(next);
				if (tile === this.TILE_BLOCK || tile === this.TILE_ENEMY) {
					continue;
				}
				visited.add(next_key);
				parent.set(next_key, current_key);
				queue.push(next);
			}
		}

		return null;
	}

	private _rebuild_first_step(
		parent: Map<string, string>,
		destination_key: string,
	): Cell | null {
		let cursor = destination_key;
		const ally_key = this._cell_key(this.ally_pos);
		while (
			parent.has(cursor) &&
			parent.get(cursor) !== ally_key &&
			cursor !== ally_key
		) {
			cursor = parent.get(cursor) as string;
		}
		return this._cell_from_key(cursor);
	}

	private _on_reset_pressed(): void {
		this._reset_board_state();
		this._refresh_board_view();
		this._refresh_status();
		this._append_log("棋盘与单位状态已重置。");
	}

	private _reset_board_state(): void {
		this.board = [];
		for (let y = 0; y < this.BOARD_SIZE; y++) {
			const row: number[] = [];
			for (let x = 0; x < this.BOARD_SIZE; x++) {
				row.push(this.TILE_EMPTY);
			}
			this.board.push(row);
		}

		for (const block_cell of this.DEFAULT_BLOCKS) {
			this._set_tile(block_cell, this.TILE_BLOCK);
		}

		this.ally_pos = { x: 0, y: 0 };
		this.enemy_pos = { x: this.BOARD_SIZE - 1, y: this.BOARD_SIZE - 1 };
		this._set_tile(this.ally_pos, this.TILE_ALLY);
		this._set_tile(this.enemy_pos, this.TILE_ENEMY);

		this.enemy_hp = this.ENEMY_MAX_HP;
		this.turn_count = 1;
	}

	private _refresh_board_view(): void {
		for (let y = 0; y < this.BOARD_SIZE; y++) {
			for (let x = 0; x < this.BOARD_SIZE; x++) {
				const index = y * this.BOARD_SIZE + x;
				const tile = this.board[y][x];
				const button = this.tile_buttons[index];
				if (!button) {
					continue;
				}

				button.text = this._tile_symbol(tile);
				button.tooltip_text = `(${x}, ${y}) ${this._tile_name(tile)}`;
				this._apply_tile_style(button, tile);
			}
		}
	}

	private _apply_tile_style(button: any, tile: number): void {
		const style = new StyleBoxFlat();
		style.bg_color = this._tile_color(tile);
		style.border_color = new Color(0.08, 0.08, 0.08);
		style.set_border_width_all(1);
		style.set_corner_radius_all(4);

		button.add_theme_stylebox_override("normal", style);
		button.add_theme_stylebox_override("hover", style);
		button.add_theme_stylebox_override("pressed", style);
		button.add_theme_color_override("font_color", new Color(1, 1, 1));
	}

	private _tile_color(tile: number): any {
		if (tile === this.TILE_EMPTY) return new Color(0.27, 0.3, 0.34);
		if (tile === this.TILE_BLOCK) return new Color(0.12, 0.12, 0.12);
		if (tile === this.TILE_ALLY) return new Color(0.13, 0.42, 0.82);
		if (tile === this.TILE_ENEMY) return new Color(0.82, 0.25, 0.23);
		return new Color(0.25, 0.25, 0.25);
	}

	private _tile_symbol(tile: number): string {
		if (tile === this.TILE_EMPTY) return ".";
		if (tile === this.TILE_BLOCK) return "#";
		if (tile === this.TILE_ALLY) return "A";
		if (tile === this.TILE_ENEMY) return "E";
		return "?";
	}

	private _tile_name(tile: number): string {
		if (tile === this.TILE_EMPTY) return "空地";
		if (tile === this.TILE_BLOCK) return "障碍";
		if (tile === this.TILE_ALLY) return "友军";
		if (tile === this.TILE_ENEMY) return "敌军";
		return "未知";
	}

	private _refresh_status(): void {
		this.status_label.text = `刷子: ${this._tile_name(this.brush_mode)} | 回合: ${this.turn_count} | 敌军 HP: ${this.enemy_hp}`;
	}

	private _append_log(text: string): void {
		this.log_output.text = `${this.log_output.text}${text}\n`;
		this.log_output.scroll_to_line(
			Math.max(this.log_output.get_line_count() - 1, 0),
		);
	}

	private _tile_at(cell: Cell): number {
		return this.board[cell.y][cell.x];
	}

	private _set_tile(cell: Cell, tile: number): void {
		this.board[cell.y][cell.x] = tile;
	}

	private _is_adjacent(a: Cell, b: Cell): boolean {
		return Math.abs(a.x - b.x) + Math.abs(a.y - b.y) === 1;
	}

	private _same_cell(a: Cell, b: Cell): boolean {
		return a.x === b.x && a.y === b.y;
	}

	private _is_in_bounds(cell: Cell): boolean {
		return (
			cell.x >= 0 &&
			cell.x < this.BOARD_SIZE &&
			cell.y >= 0 &&
			cell.y < this.BOARD_SIZE
		);
	}

	private _cell_key(cell: Cell): string {
		return `${cell.x},${cell.y}`;
	}

	private _cell_from_key(key: string): Cell {
		const [x, y] = key.split(",").map((v) => Number(v));
		return { x, y };
	}
}
