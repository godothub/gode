import { Control } from "godot";
import nodeAssert from "node:assert";
import crypto from "node:crypto";
import { EventEmitter } from "node:events";
import fs from "node:fs";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import querystring from "node:querystring";
import { Readable, Transform } from "node:stream";
import util from "node:util";
import { pathToFileURL, URL } from "node:url";
import vm from "node:vm";
import zlib from "node:zlib";
import { getTestCase } from "./test_catalog.js";
import { getSelectedTestId } from "./test_selection.js";

const MODULES = {
	assert: nodeAssert,
	crypto,
	events: { EventEmitter },
	fs,
	http,
	os,
	path,
	querystring,
	stream: { Readable, Transform },
	url: { pathToFileURL, URL },
	util,
	vm,
	zlib,
};

export default class TestWorkspace extends Control {
	_ready() {
		this.titleLabel = this.get_node("Layout/Header/TitleBox/Title");
		this.summaryLabel = this.get_node("Layout/Header/TitleBox/Summary");
		this.editor = this.get_node("Layout/Body/ScriptEditor");
		this.output = this.get_node("Layout/Body/OutputPanel");
		this.runButton = this.get_node("Layout/Header/Actions/RunButton");
		this.backButton = this.get_node("Layout/Header/Actions/BackButton");

		this.testCase = getTestCase(getSelectedTestId());
		this.passCount = 0;
		this.failCount = 0;
		this.titleLabel.text = this.testCase.title;
		this.summaryLabel.text = this.testCase.summary;
		this.editor.text = this.testCase.code;
		this.output.text = "";

		this.runButton.connect("pressed", () => {
			void this.runCurrentScript();
		});
		this.backButton.connect("pressed", () => this.get_tree().change_scene_to_file("res://scenes/main_menu.tscn"));
	}

	append(level, message) {
		const line = `[${level}] ${message}`;
		this.output.text = this.output.text.length > 0 ? `${this.output.text}\n${line}` : line;
	}

	formatValue(value) {
		if (typeof value === "string") {
			return value;
		}
		if (value instanceof Error) {
			return value.stack ?? value.message;
		}
		if (Buffer.isBuffer(value)) {
			return `<Buffer ${value.toString("hex")} (${value.length} bytes)>`;
		}
		return util.inspect(value, {
			depth: 4,
			breakLength: 100,
			compact: false,
			colors: false,
		});
	}

	formatValues(values) {
		return values.map(value => this.formatValue(value)).join(" ");
	}

	assert(label, value) {
		if (value) {
			this.passCount++;
			this.append("PASS", label);
			return;
		}

		this.failCount++;
		this.append("FAIL", label);
	}

	async runCurrentScript() {
		this.passCount = 0;
		this.failCount = 0;
		this.output.text = "";
		this.runButton.disabled = true;
		const startedAt = Date.now();
		this.append("INFO", `Running ${this.testCase.title}`);

		const originalLog = console.log;
		const originalError = console.error;
		console.log = (...args) => this.append("INFO", this.formatValues(args));
		console.error = (...args) => this.append("ERROR", this.formatValues(args));

		try {
			const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor;
			const execute = new AsyncFunction("modules", "scene", "assert", "log", this.editor.text);
			await execute(
				MODULES,
				this,
				(label, value) => this.assert(label, value),
				(...messages) => this.append("INFO", this.formatValues(messages)),
			);
			this.append("INFO", `Result: ${this.passCount} passed, ${this.failCount} failed in ${Date.now() - startedAt} ms`);
		} catch (error) {
			this.failCount++;
			this.append("ERROR", this.formatValue(error));
			this.append("INFO", `Result: ${this.passCount} passed, ${this.failCount} failed in ${Date.now() - startedAt} ms`);
		} finally {
			console.log = originalLog;
			console.error = originalError;
			this.runButton.disabled = false;
		}
	}
}
