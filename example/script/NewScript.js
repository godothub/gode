const godot = require('godot');

class NewScript extends godot.Node2D {
	_ready() {
		GD.print("NewScript _ready");
	}
}

module.exports = { default: NewScript };
