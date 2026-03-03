const godot = require('godot');
const TestExport = require('res://script/TestExport.js');

class MyNode extends godot.Node2D {
	_ready() {
		GD.print("NewScript _ready");
	}
}

module.exports = { default: MyNode };
