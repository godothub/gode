const gode = require('gode');

class NewScript extends gode.Node2D {
	_ready() {
		GD.print("NewScript _ready");
	}
}

module.exports = NewScript;
