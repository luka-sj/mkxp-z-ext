#ifndef STREAM_CAPTURE_H
#define STREAM_CAPTURE_H

#include "etc-internal.h"
#include "gl-util.h"

#include <cstdint>
#include <vector>

class StreamServer;

class StreamCapture
{
public:
	explicit StreamCapture(StreamServer &server);
	~StreamCapture();

	void capture(TEXFBO &source, const Vec2i &sourceSize, const Vec2i &frameSize);

private:
	struct Slot
	{
		GLuint pbo;
		GLsync fence;
		uint64_t timeUs;
		uint64_t order;
	};

	static const int SLOT_COUNT = 3;

	bool asyncReadback() const;
	void ensureTarget(const Vec2i &size);
	void releaseTarget();
	void releaseSlot(Slot &slot);
	Slot *freeSlot();
	void collect();
	size_t frameBytes() const;

	StreamServer &server;
	TEXFBO target;
	Vec2i targetSize;
	Slot slots[SLOT_COUNT];
	uint64_t nextOrder;
	std::vector<uint8_t> pixels;
};

#endif // STREAM_CAPTURE_H
