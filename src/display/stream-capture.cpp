#include "stream-capture.h"

#include "etc.h"
#include "gl-fun.h"
#include "gl-meta.h"
#include "stream-server.h"

#include <SDL_timer.h>

#include <algorithm>

static uint64_t nowMicros()
{
	return (uint64_t)((double)SDL_GetPerformanceCounter() * 1000000.0 /
	                  (double)SDL_GetPerformanceFrequency());
}

StreamCapture::StreamCapture(StreamServer &server)
    : server(server),
      targetSize(0, 0),
      nextOrder(0)
{
	for (int i = 0; i < SLOT_COUNT; ++i)
		slots[i] = Slot{0, nullptr, 0, 0};
}

StreamCapture::~StreamCapture()
{
	releaseTarget();
}

bool StreamCapture::asyncReadback() const
{
	return gl.MapBufferRange && gl.UnmapBuffer && gl.FenceSync &&
	       gl.ClientWaitSync && gl.DeleteSync;
}

size_t StreamCapture::frameBytes() const
{
	return (size_t)targetSize.x * (size_t)targetSize.y * 4;
}

void StreamCapture::ensureTarget(const Vec2i &size)
{
	if (target.tex != TEX::ID(0) && size == targetSize)
		return;

	releaseTarget();
	targetSize = size;

	TEXFBO::init(target);
	TEXFBO::allocEmpty(target, size.x, size.y);
	TEXFBO::linkFBO(target);

	if (!asyncReadback())
		return;

	for (int i = 0; i < SLOT_COUNT; ++i)
	{
		gl.GenBuffers(1, &slots[i].pbo);
		gl.BindBuffer(GL_PIXEL_PACK_BUFFER, slots[i].pbo);
		gl.BufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)frameBytes(), nullptr, GL_STREAM_READ);
	}
	gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void StreamCapture::releaseTarget()
{
	for (int i = 0; i < SLOT_COUNT; ++i)
	{
		releaseSlot(slots[i]);
		if (slots[i].pbo)
		{
			gl.DeleteBuffers(1, &slots[i].pbo);
			slots[i].pbo = 0;
		}
	}

	if (target.tex != TEX::ID(0))
	{
		TEXFBO::fini(target);
		TEXFBO::clear(target);
	}
}

void StreamCapture::releaseSlot(Slot &slot)
{
	if (slot.fence)
	{
		gl.DeleteSync(slot.fence);
		slot.fence = nullptr;
	}
}

StreamCapture::Slot *StreamCapture::freeSlot()
{
	for (int i = 0; i < SLOT_COUNT; ++i)
		if (slots[i].pbo && !slots[i].fence)
			return &slots[i];

	return nullptr;
}

void StreamCapture::collect()
{
	Slot *pending[SLOT_COUNT];
	int count = 0;

	for (int i = 0; i < SLOT_COUNT; ++i)
		if (slots[i].fence)
			pending[count++] = &slots[i];

	std::sort(pending, pending + count,
	          [](const Slot *a, const Slot *b) { return a->order < b->order; });

	Slot *newest = nullptr;
	for (int i = 0; i < count; ++i)
	{
		GLenum state = gl.ClientWaitSync(pending[i]->fence, 0, 0);
		if (state == GL_WAIT_FAILED)
		{
			releaseSlot(*pending[i]);
			continue;
		}
		if (state != GL_ALREADY_SIGNALED && state != GL_CONDITION_SATISFIED)
			break;

		if (newest)
			releaseSlot(*newest);
		newest = pending[i];
	}

	if (!newest)
		return;

	gl.BindBuffer(GL_PIXEL_PACK_BUFFER, newest->pbo);
	void *data = gl.MapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)frameBytes(), GL_MAP_READ_BIT);
	if (data)
	{
		server.submitFrame(data, targetSize.x, targetSize.y, newest->timeUs);
		gl.UnmapBuffer(GL_PIXEL_PACK_BUFFER);
	}
	gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	releaseSlot(*newest);
}

void StreamCapture::capture(TEXFBO &source, const Vec2i &sourceSize, const Vec2i &frameSize)
{
	bool async = asyncReadback();

	if (async && target.tex != TEX::ID(0))
	{
		collect();
		if (!freeSlot())
			return;
	}

	if (frameSize.x <= 0 || frameSize.y <= 0 || !server.frameDue())
		return;

	ensureTarget(frameSize);

	GLMeta::blitBegin(target, false, SameScale);
	GLMeta::blitSource(source, SameScale);
	GLMeta::blitRectangle(IntRect(0, 0, sourceSize.x, sourceSize.y),
	                      IntRect(0, 0, frameSize.x, frameSize.y), false);
	GLMeta::blitEnd();

	FBO::bind(target.fbo);
	uint64_t timeUs = nowMicros();

	if (!async)
	{
		pixels.resize(frameBytes());
		gl.ReadPixels(0, 0, frameSize.x, frameSize.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		server.submitFrame(pixels.data(), frameSize.x, frameSize.y, timeUs);
		return;
	}

	Slot *slot = freeSlot();
	if (!slot)
		return;

	gl.BindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
	gl.ReadPixels(0, 0, frameSize.x, frameSize.y, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	slot->fence = gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	slot->timeUs = timeUs;
	slot->order = nextOrder++;
}
