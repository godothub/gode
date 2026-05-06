const trim = value => value.trim();

export const TEST_CASES = [
	{
		id: "godot-signal",
		title: "Godot signal bridge",
		summary: "Create a runtime signal, send structured payloads, and await the next emission.",
		code: trim(`
const signalName = "inventory_changed_" + Date.now();
scene.add_user_signal(signalName);

const received = [];
scene.connect(signalName, payload => {
	received.push(payload);
	log("signal payload", payload);
});

scene.emit_signal(signalName, { item: "laser_key", count: 1 });
setTimeout(() => scene.emit_signal(signalName, { item: "energy_cell", count: 3 }), 0);

await scene.to_signal(signalName);
assert("two payloads crossed Godot signal boundary", received.length === 2);
assert("object payload preserved", received[1].item === "energy_cell" && received[1].count === 3);
`),
	},
	{
		id: "godot-scene-tree",
		title: "Scene tree inspector",
		summary: "Inspect the live scene tree, node paths, viewport, and visible editor workspace.",
		code: trim(`
const tree = scene.get_tree();
const current = tree.current_scene;
const viewport = scene.get_viewport();
const children = scene.get_children().map(child => child.name);

log("current scene", {
	name: current.name,
	path: String(current.get_path()),
	childCount: scene.get_child_count(),
	children,
	viewportSize: viewport.get_visible_rect().size,
});

assert("tree exists", !!tree);
assert("current scene is this workspace", current.name === scene.name);
assert("workspace is inside tree", scene.is_inside_tree());
assert("script editor and output panel are present", children.includes("Layout"));
`),
	},
	{
		id: "godot-node-meta",
		title: "Node metadata store",
		summary: "Attach structured JavaScript state to a Godot Node and read it back.",
		code: trim(`
const key = "gode_demo_state";
const state = {
	player: "Ada",
	stats: { hp: 84, shield: 27 },
	inventory: ["laser_key", "energy_cell", "map_fragment"],
};

scene.set_meta(key, state);
const restored = scene.get_meta(key);
log("metadata roundtrip", restored);

assert("metadata exists", scene.has_meta(key));
assert("nested object survives", restored.stats.shield === 27);
assert("array survives", restored.inventory.includes("map_fragment"));
assert("metadata list includes key", scene.get_meta_list().includes(key));

scene.remove_meta(key);
assert("metadata cleanup", !scene.has_meta(key));
`),
	},
	{
		id: "godot-timer",
		title: "Async timer choreography",
		summary: "Drive Godot-compatible async work with JavaScript timers and promises.",
		code: trim(`
const signalName = "timer_complete_" + Date.now();
scene.add_user_signal(signalName);

const startedAt = Date.now();
const jobs = [8, 16, 4].map((delay, index) => new Promise(resolve => {
	setTimeout(() => {
		const event = { index, delay, elapsed: Date.now() - startedAt };
		log("timer fired", event);
		resolve(event);
		if (index === 1) {
			scene.emit_signal(signalName, event);
		}
	}, delay);
}));

const signalEvent = await scene.to_signal(signalName);
const results = await Promise.all(jobs);

assert("three timers completed", results.length === 3);
assert("signal carried timer event", signalEvent.index === 1);
assert("timers ran asynchronously", Math.max(...results.map(item => item.elapsed)) >= 8);
`),
	},
	{
		id: "js-json",
		title: "JavaScript data pipeline",
		summary: "Transform gameplay data with modern JS collections, JSON, and reducers.",
		code: trim(`
const enemies = [
	{ kind: "drone", hp: 12, score: 40 },
	{ kind: "sentinel", hp: 32, score: 120 },
	{ kind: "drone", hp: 8, score: 40 },
	{ kind: "warden", hp: 70, score: 400 },
];

const report = enemies
	.filter(enemy => enemy.hp > 10)
	.map(enemy => ({ ...enemy, threat: enemy.hp * enemy.score }))
	.sort((a, b) => b.threat - a.threat);
const totalScore = enemies.reduce((sum, enemy) => sum + enemy.score, 0);
const json = JSON.stringify({ totalScore, report }, null, 2);
const parsed = JSON.parse(json);

log("battle report", parsed);
assert("total score calculated", parsed.totalScore === 600);
assert("highest threat first", parsed.report[0].kind === "warden");
assert("json contains formatted lines", json.includes("\\n  \\"report\\""));
`),
	},
	{
		id: "node-path",
		title: "Cross-platform path tools",
		summary: "Normalize Godot project paths and Node-style paths without leaving JavaScript.",
		code: trim(`
const path = modules.path;
const scriptPath = "res://scripts/test_catalog.js";
const normalized = path.join("res://", "scripts", "..", "scripts", "tests", "demo.js").replace(/\\\\/g, "/");
const parsed = path.parse(scriptPath);
const relative = path.relative("res://scripts", "res://addons/gode/types/godot.d.ts").replace(/\\\\/g, "/");

log("path analysis", { scriptPath, parsed, normalized, relative });
assert("basename", path.basename(scriptPath) === "test_catalog.js");
assert("extension", parsed.ext === ".js");
assert("normalized project path", normalized.endsWith("scripts/tests/demo.js"));
assert("relative path crosses addon folder", relative.includes("addons/gode/types"));
`),
	},
	{
		id: "node-url",
		title: "URL request composer",
		summary: "Build URLs, edit query strings, and convert local files to file URLs.",
		code: trim(`
const { URL, pathToFileURL } = modules.url;
const request = new URL("https://example.com/gode/run");
request.searchParams.set("scene", "main_menu");
request.searchParams.set("case", "node:url");
request.searchParams.set("debug", "1");

const fileUrl = pathToFileURL(modules.path.join(process.cwd(), "package.json"));
log("request url", request.href);
log("package file url", fileUrl.href);

assert("https protocol", request.protocol === "https:");
assert("query parameter set", request.searchParams.get("case") === "node:url");
assert("file URL points at package", fileUrl.href.endsWith("package.json"));
`),
	},
	{
		id: "node-fs",
		title: "Project filesystem access",
		summary: "Read the Godot project directory, parse package.json, and inspect script files.",
		code: trim(`
const fs = modules.fs;
const path = modules.path;
const cwd = process.cwd();
const packagePath = path.join(cwd, "package.json");
const scriptsDir = path.join(cwd, "scripts");
const packageJson = JSON.parse(fs.readFileSync(packagePath, "utf8"));
const scripts = fs.readdirSync(scriptsDir).filter(file => file.endsWith(".js")).sort();
const stats = fs.statSync(packagePath);

log("project package", packageJson);
log("script files", scripts);
log("package size", stats.size + " bytes");

assert("cwd exists", fs.existsSync(cwd));
assert("package parsed", packageJson.name === "gode-example");
assert("script catalog visible", scripts.includes("test_catalog.js"));
assert("package file has content", stats.size > 0);
`),
	},
	{
		id: "node-crypto",
		title: "Crypto asset manifest",
		summary: "Hash project content, sign a manifest, and generate random session bytes.",
		code: trim(`
const crypto = modules.crypto;
const fs = modules.fs;
const path = modules.path;
const packageText = fs.readFileSync(path.join(process.cwd(), "package.json"), "utf8");
const digest = crypto.createHash("sha256").update(packageText).digest("hex");
const hmac = crypto.createHmac("sha256", "gode-demo-key").update(digest).digest("hex");
const session = crypto.randomBytes(12);

log("manifest", {
	packageHash: digest,
	signature: hmac,
	sessionHex: session.toString("hex"),
});

assert("sha256 length", digest.length === 64);
assert("hmac length", hmac.length === 64);
assert("random session bytes", session.length === 12);
assert("hash is deterministic", crypto.createHash("sha256").update(packageText).digest("hex") === digest);
`),
	},
	{
		id: "node-zlib",
		title: "Compressed save payload",
		summary: "Serialize a save-game payload, gzip it, and verify lossless recovery.",
		code: trim(`
const zlib = modules.zlib;
const save = {
	slot: 3,
	player: "Ada",
	checkpoint: "reactor_deck",
	flags: Array.from({ length: 16 }, (_, index) => "flag_" + index),
};
const raw = Buffer.from(JSON.stringify(save));
const zipped = zlib.gzipSync(raw);
const restored = JSON.parse(zlib.gunzipSync(zipped).toString("utf8"));

log("compression", {
	rawBytes: raw.length,
	gzipBytes: zipped.length,
	restored,
});

assert("roundtrip player", restored.player === save.player);
assert("roundtrip flags", restored.flags.length === 16);
assert("gzip produced bytes", zipped.length > 0);
`),
	},
	{
		id: "node-os",
		title: "Runtime environment snapshot",
		summary: "Inspect CPU, memory, platform, temp paths, and Node version from inside Godot.",
		code: trim(`
const os = modules.os;
const snapshot = {
	platform: os.platform(),
	arch: os.arch(),
	release: os.release(),
	node: process.versions.node,
	cpus: os.cpus().slice(0, 2).map(cpu => cpu.model),
	totalMemoryMb: Math.round(os.totalmem() / 1024 / 1024),
	freeMemoryMb: Math.round(os.freemem() / 1024 / 1024),
	tmpdir: os.tmpdir(),
};

log("environment", snapshot);
assert("platform detected", snapshot.platform.length > 0);
assert("node version available", /^\\d+\\.\\d+\\.\\d+/.test(snapshot.node));
assert("memory visible", snapshot.totalMemoryMb > 0);
assert("temp directory visible", snapshot.tmpdir.length > 0);
`),
	},
	{
		id: "node-events",
		title: "EventEmitter gameplay bus",
		summary: "Model a small command bus with persistent listeners and one-shot events.",
		code: trim(`
const { EventEmitter } = modules.events;
const bus = new EventEmitter();
const state = { score: 0, opened: [] };

bus.on("score", value => {
	state.score += value;
	log("score changed", state.score);
});
bus.on("open", door => state.opened.push(door));
bus.once("boss", name => log("boss spawned once", name));

bus.emit("score", 25);
bus.emit("open", "north_gate");
bus.emit("score", 75);
bus.emit("boss", "warden");
bus.emit("boss", "ignored");

assert("score accumulated", state.score === 100);
assert("door event stored", state.opened[0] === "north_gate");
assert("one-shot listener removed", bus.listenerCount("boss") === 0);
`),
	},
	{
		id: "node-stream",
		title: "Streaming text transform",
		summary: "Process streaming chunks with a Node Transform and async iteration.",
		code: trim(`
const { Readable, Transform } = modules.stream;
const upper = new Transform({
	transform(chunk, _encoding, callback) {
		callback(null, String(chunk).toUpperCase());
	},
});

const chunks = [];
const stream = Readable.from(["godot", " + ", "node", " + ", "javascript"]).pipe(upper);
for await (const chunk of stream) {
	chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(String(chunk)));
}

const output = Buffer.concat(chunks).toString("utf8");
log("stream output", output);
assert("transformed text", output === "GODOT + NODE + JAVASCRIPT");
assert("multiple chunks processed", chunks.length >= 1);
`),
	},
	{
		id: "node-vm",
		title: "Sandboxed gameplay rules",
		summary: "Evaluate user-authored rule code in an isolated VM context.",
		code: trim(`
const vm = modules.vm;
const context = {
	player: { hp: 40, shield: 10 },
	damage: 18,
	result: null,
};

vm.createContext(context);
vm.runInContext(\`
	const absorbed = Math.min(player.shield, damage);
	player.shield -= absorbed;
	player.hp -= damage - absorbed;
	result = { hp: player.hp, shield: player.shield, absorbed };
\`, context);

log("rule result", context.result);
assert("shield absorbed damage", context.result.absorbed === 10);
assert("hp reduced inside sandbox", context.result.hp === 32);
assert("sandbox did not leak player", typeof globalThis.player === "undefined");
`),
	},
	{
		id: "node-http",
		title: "Loopback HTTP service",
		summary: "Start a local HTTP server in Godot, call it, parse JSON, and close it.",
		code: trim(`
const http = modules.http;
const server = http.createServer((request, response) => {
	response.setHeader("content-type", "application/json");
	response.end(JSON.stringify({
		method: request.method,
		url: request.url,
		engine: "gode",
	}));
});

await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const address = server.address();
const body = await new Promise((resolve, reject) => {
	const request = http.get({
		host: "127.0.0.1",
		port: address.port,
		path: "/status?from=godot",
	}, response => {
		const chunks = [];
		response.on("data", chunk => chunks.push(chunk));
		response.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
	});
	request.on("error", reject);
});
server.close();

const parsed = JSON.parse(body);
log("http response", parsed);
assert("server bound dynamic port", address.port > 0);
assert("response parsed", parsed.engine === "gode");
assert("request path preserved", parsed.url.includes("from=godot"));
`),
	},
	{
		id: "node-buffer",
		title: "Binary packet builder",
		summary: "Pack and unpack binary data with Buffer just like a Node service.",
		code: trim(`
const packet = Buffer.alloc(12);
packet.writeUInt32LE(0xdecafbad, 0);
packet.writeUInt16LE(2026, 4);
packet.write("GD", 6, "ascii");
packet.writeFloatLE(3.5, 8);

const decoded = {
	magic: packet.readUInt32LE(0).toString(16),
	version: packet.readUInt16LE(4),
	tag: packet.toString("ascii", 6, 8),
	speed: packet.readFloatLE(8),
};

log("packet", packet);
log("decoded", decoded);
assert("magic decoded", decoded.magic === "decafbad");
assert("version decoded", decoded.version === 2026);
assert("tag decoded", decoded.tag === "GD");
assert("float decoded", decoded.speed === 3.5);
`),
	},
	{
		id: "node-querystring",
		title: "Querystring filters",
		summary: "Parse UI filter state, normalize it, and serialize it for a backend request.",
		code: trim(`
const querystring = modules.querystring;
const incoming = "tag=ai&tag=godot&page=2&search=script%20runtime";
const parsed = querystring.parse(incoming);
const normalized = {
	tags: Array.isArray(parsed.tag) ? parsed.tag : [parsed.tag],
	page: Number(parsed.page),
	search: parsed.search,
};
const outgoing = querystring.stringify({
	tag: normalized.tags.join(","),
	page: normalized.page + 1,
	search: normalized.search,
});

log("filters", { parsed, normalized, outgoing });
assert("multi-value tag parsed", normalized.tags.length === 2);
assert("page normalized", normalized.page === 2);
assert("serialized next page", outgoing.includes("page=3"));
`),
	},
	{
		id: "node-util",
		title: "Utility helpers",
		summary: "Use util.format, util.inspect, and promisify for clean async APIs.",
		code: trim(`
const util = modules.util;
const asyncAdd = util.promisify((a, b, callback) => {
	setTimeout(() => callback(null, a + b), 1);
});
const sum = await asyncAdd(20, 22);
const label = util.format("%s:%d", "score", sum);
const inspected = util.inspect({ label, nested: { ok: true } }, { depth: 2 });

log("formatted label", label);
log("inspect output", inspected);
assert("promisify returned sum", sum === 42);
assert("format produced label", label === "score:42");
assert("inspect includes nested data", inspected.includes("nested"));
`),
	},
	{
		id: "node-assert",
		title: "Rich assertions",
		summary: "Use Node assertions for deep equality and readable failure diagnostics.",
		code: trim(`
const nodeAssert = modules.assert;
const actual = {
	name: "gode",
	capabilities: ["godot", "node", "javascript"],
	metrics: { tests: 20, editable: true },
};
const expected = JSON.parse(JSON.stringify(actual));

nodeAssert.deepStrictEqual(actual, expected);
nodeAssert.match(actual.capabilities.join(","), /godot,node,javascript/);
nodeAssert.ok(actual.metrics.editable);

log("asserted object", actual);
assert("deepStrictEqual passed", true);
assert("match passed", true);
assert("ok passed", actual.metrics.tests === 20);
`),
	},
	{
		id: "node-process",
		title: "Process and microtasks",
		summary: "Inspect process state and prove Node microtasks run inside the Godot scene.",
		code: trim(`
const events = [];
process.nextTick(() => events.push("nextTick"));
queueMicrotask(() => events.push("microtask"));
await Promise.resolve();
await new Promise(resolve => setTimeout(resolve, 0));

const snapshot = {
	cwd: process.cwd(),
	node: process.versions.node,
	v8: process.versions.v8,
	events,
};

log("process snapshot", snapshot);
assert("cwd available", snapshot.cwd.length > 0);
assert("node version available", typeof snapshot.node === "string");
assert("microtasks ran", events.includes("nextTick") && events.includes("microtask"));
`),
	},
];

export function getTestCase(id) {
	return TEST_CASES.find(test => test.id === id) ?? TEST_CASES[0];
}
