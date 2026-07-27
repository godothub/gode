#ifndef GODE_EVENT_LOOP_H
#define GODE_EVENT_LOOP_H

#include <godot_cpp/classes/node.hpp>

namespace gode {

class GodeEventLoop : public godot::Node {
	GDCLASS(GodeEventLoop, godot::Node);

protected:
	static void _bind_methods();

public:
	GodeEventLoop();
	~GodeEventLoop();

	void _process(double delta) override;
};

} // namespace gode

#endif // GODE_EVENT_LOOP_H