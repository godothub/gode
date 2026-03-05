const godot = require('godot');
const isOdd = require('is-odd');

class MyNode extends godot.Node2D {
	_ready() {
		GD.print("ready");
		console.log('Is 1 odd?', isOdd(1));
		console.log('Is 2 odd?', isOdd(2));
	}
}

module.exports = { default: MyNode };
