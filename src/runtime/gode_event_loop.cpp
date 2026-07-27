#include "runtime/gode_event_loop.h"
#include "runtime/node_runtime.h"
#include <godot_cpp/core/class_db.hpp>

namespace gode {

void GodeEventLoop::_bind_methods() {
	// No methods to bind for now
}

GodeEventLoop::GodeEventLoop() {
	// Constructor
}

GodeEventLoop::~GodeEventLoop() {
	// Destructor
}

void GodeEventLoop::_process(double delta) {
	// Run the Node.js event loop
	NodeRuntime::spin_loop();
}

} // namespace gode