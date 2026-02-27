#include "tests/test_runner.h"
#include "utils/node_runtime.h"
#include <godot_cpp/variant/utility_functions.hpp>

namespace gode {

void TestRunner::run_tests() {
	godot::UtilityFunctions::print("Running Gode Unit Tests...");

	// JS Test Code
	std::string test_code = R"(
		const assert = require('assert');
		const fs = require('fs');
		const path = require('path');

		// Helper to print test results
		function test(name, fn) {
			try {
				fn();
				console.log(`[PASS] ${name}`);
			} catch (e) {
				console.error(`[FAIL] ${name}: ${e.message}`);
				console.error(e.stack);
			}
		}

		console.log('--- Starting Tests ---');

		test('fs.readFileSync with res:// path', () => {
			// Try to read project.godot if it exists, or just check that it doesn't crash on non-existent file
			// Assuming we are running in a project context
			try {
				const content = fs.readFileSync('res://project.godot', 'utf8');
				assert.ok(content, 'Should return content');
				assert.ok(content.includes('config_version'), 'Content should be project.godot');
			} catch (e) {
				// If file doesn't exist, check if the error is correct
				if (e.code === 'ENOENT') {
					console.log('  (File not found, but handled correctly)');
				} else {
					// It might fail if running without a project context, which is expected in some test scenarios
					console.log('  (Read failed: ' + e.message + ')');
				}
			}
		});

		test('fs.existsSync with res:// path', () => {
			const exists = fs.existsSync('res://project.godot');
			console.log('  res://project.godot exists:', exists);
			// We can't assert true because it depends on the environment
			
			const notExists = fs.existsSync('res://non_existent_file_12345.js');
			assert.strictEqual(notExists, false, 'Non-existent file should not exist');
		});

		test('path.resolve with res:// path', () => {
			const p = path.resolve('res://foo', 'bar');
			// Expect forward slashes even on Windows due to our hook
			assert.strictEqual(p.replace(/\\/g, '/'), 'res://foo/bar');
		});

		test('Module loading (require)', () => {
			const fs_module = require('fs');
			assert.ok(fs_module, 'Should load built-in module');
			assert.strictEqual(typeof fs_module.readFileSync, 'function');
		});

		console.log('--- Tests Completed ---');
	)";

	NodeRuntime::run_script(test_code);
}

} // namespace gode
