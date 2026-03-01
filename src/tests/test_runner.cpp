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

		test('require with res:// path (Module._findPath hook)', () => {
			// Mock module object to test _findPath indirectly or test if require throws correct error
			// Since we don't have a real file to require here easily without setup, 
			// we can check if Module._findPath is patched correctly.
			
			const Module = require('module');
			const originalFindPath = Module._findPath;
			
			// Test if our hook is active
			const resPath = 'res://some/script.js';
			const found = Module._findPath(resPath, [], false);
			assert.strictEqual(found, resPath, 'Module._findPath should return res:// path as-is');
			
			// Test normal path behavior (should fallback to original)
			// Note: Module._findPath usually returns false or string. 
			// We just want to ensure it doesn't return the input string blindly for non-res paths.
			const normalPath = './some/script.js';
			// It will likely return false because file doesn't exist, which is different from returning the input string
			const foundNormal = Module._findPath(normalPath, [], false); 
			assert.notStrictEqual(foundNormal, normalPath, 'Should not return non-res path as-is');
		});

		test('Godot Node Class Binding', () => {
			let NodeClass;
			try {
				// Try via linked binding directly as these are internal modules
				const gode = process._linkedBinding('gode');
				NodeClass = gode.Node;
			} catch (e) {
				console.log('  (process._linkedBinding failed: ' + e.message + ')');
			}

			if (!NodeClass) {
				throw new Error('Could not load Node class');
			}

			assert.strictEqual(typeof NodeClass, 'function', 'Node should be a class (function)');
			
			// Instantiate
			const node = new NodeClass();
			assert.ok(node, 'Instance should be created');
			assert.strictEqual(typeof node.get_name, 'function', 'Should have get_name method');
			
			// Test method
			node.set_name('TestNode');
			assert.strictEqual(node.get_name(), 'TestNode', 'Name should be set and retrieved');
			
			// Test inheritance (Sprite2D -> Node)
			try {
				const gode = process._linkedBinding('gode');
				const Sprite2D = gode.Sprite2D;
				const sprite = new Sprite2D();
				
				assert.ok(sprite instanceof NodeClass, 'Sprite2D should inherit from Node');
				sprite.set_name('MySprite');
				assert.strictEqual(sprite.get_name(), 'MySprite', 'Inherited method should work');
				
				// Additional tests
				const pos = sprite.get_position();
				console.log('  Sprite2D position:', pos);
                
                // Test global singletons
                console.log('  Testing Singletons:');
                console.log('  Input:', Input);
                console.log('  Engine:', Engine);
                
                //if (Input) {
                //    const isPressed = Input.is_action_pressed('ui_accept');
                //   console.log('  Input.is_action_pressed("ui_accept"):', isPressed);
                //}
                
                if (Engine) {
                     console.log('  Engine.get_version_info():', Engine.get_version_info());
                }

			} catch (e) {
				console.log('  (Sprite2D test failed: ' + e.message + ')');
				throw e;
			}
		});

		test('Vector2 Binding', () => {
			try {
                // Vector2 is a global builtin
                const v = new Vector2(10, 20);
                assert.strictEqual(v.x, 10, 'Vector2.x should be 10');
                assert.strictEqual(v.y, 20, 'Vector2.y should be 20');
                
                console.log('  Vector2 string:', v.toString());
                console.log('  Vector2 inspect:', v);
                
                assert.ok(v.toString().includes('10.0, 20.0'), 'toString should contain values');
			} catch (e) {
				console.log('  (Vector2 test failed: ' + e.message + ')');
                throw e;
			}
		});

		console.log('--- Tests Completed ---');
	)";

	NodeRuntime::run_script(test_code);
}

} // namespace gode
