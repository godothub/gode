import { Button, CodeEdit, Control, Label, TextEdit } from "godot";
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
import { getCapabilityDemo, type CapabilityDemo } from "./capability_catalog.js";
import { getSelectedCapabilityId } from "./capability_selection.js";

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

export default class CapabilityWorkspace extends Control {
	titleLabel!: Label;
	summaryLabel!: Label;
	editor!: CodeEdit;
	output!: TextEdit;
	runButton!: Button;
	backButton!: Button;
	demo!: CapabilityDemo;
	passCount = 0;
	failCount = 0;

	_ready() {
		this.titleLabel = this.get_node("Layout/Header/TitleBox/Title") as Label;
		this.summaryLabel = this.get_node("Layout/Header/TitleBox/Summary") as Label;
		this.editor = this.get_node("Layout/Body/ScriptEditor") as CodeEdit;
		this.output = this.get_node("Layout/Body/OutputPanel") as TextEdit;
		this.runButton = this.get_node("Layout/Header/Actions/RunButton") as Button;
		this.backButton = this.get_node("Layout/Header/Actions/BackButton") as Button;

		this.demo = getCapabilityDemo(getSelectedCapabilityId());
		this.passCount = 0;
		this.failCount = 0;
		this.titleLabel.text = this.demo.title;
		this.summaryLabel.text = this.demo.summary;
		this.editor.text = this.demo.code;
		this.output.text = "";

		this.runButton.connect("pressed", () => {
			void this.runCurrentScript();
		});
		this.backButton.connect("pressed", () => this.get_tree().change_scene_to_file("res://scenes/main_menu.tscn"));
	}

	append(level: string, message: string): void {
		const line = `[${level}] ${message}`;
		this.output.text = this.output.text.length > 0 ? `${this.output.text}\n${line}` : line;
	}

	formatValue(value: unknown): string {
		if (typeof value === "string") {
			return value;
		}
		if (value instanceof Error) {
			return value.stack ?? value.message;
		}
		if (Buffer.isBuffer(value)) {
			const buffer = value as { toString(encoding: string): string; length: number };
			return `<Buffer ${buffer.toString("hex")} (${buffer.length} bytes)>`;
		}
		return util.inspect(value, {
			depth: 4,
			breakLength: 100,
			compact: false,
			colors: false,
		});
	}

	formatValues(values: unknown[]): string {
		return values.map(value => this.formatValue(value)).join(" ");
	}

	assert(label: string, value: unknown): void {
		if (value) {
			this.passCount++;
			this.append("PASS", label);
			return;
		}

		this.failCount++;
		this.append("FAIL", label);
	}

	async runCurrentScript(): Promise<void> {
		this.passCount = 0;
		this.failCount = 0;
		this.output.text = "";
		this.runButton.disabled = true;
		const startedAt = Date.now();
		this.append("INFO", `Running demo: ${this.demo.title}`);

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
				(label: string, value: unknown) => this.assert(label, value),
				(...messages: unknown[]) => this.append("INFO", this.formatValues(messages)),
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
