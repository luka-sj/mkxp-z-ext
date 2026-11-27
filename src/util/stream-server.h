#ifndef STREAM_SERVER_H
#define STREAM_SERVER_H

#include <SDL_atomic.h>
#include <SDL_events.h>
#include <SDL_mutex.h>
#include <SDL_scancode.h>
#include <SDL_thread.h>

#include <cstdint>
#include <vector>

struct RGSSThreadData;

class StreamServer
{
public:
	StreamServer(int port, int frameRate, RGSSThreadData &rtData);
	~StreamServer();

	void start();
	void stop();

	bool connected();
	bool frameDue();
	void submitFrame(const void *pixels, int width, int height, uint64_t timeUs);

private:
	static int acceptThreadFun(void *data);
	static int sendThreadFun(void *data);

	void runAccept();
	void runSend();

	void handleRecord(const uint8_t *record);
	void injectKey(int scancode, bool down);
	void injectMotion(int x, int y);
	void injectButton(int button, bool down);
	void injectWheel(int steps);
	void releaseHeld();
	void pushEvent(SDL_Event &event);

	RGSSThreadData &rtData;
	uint32_t windowID;

	int port;
	uint64_t frameTicks;
	uint64_t nextFrameTick;

	SDL_atomic_t running;
	SDL_atomic_t clientConnected;

	SDL_mutex *frameMutex;
	SDL_cond *frameCond;
	std::vector<uint8_t> pendingFrame;
	std::vector<uint8_t> sendingFrame;
	int pendingWidth;
	int pendingHeight;
	uint64_t pendingTime;
	uint32_t pendingSeq;
	uint32_t sentSeq;

	SDL_mutex *socketMutex;
	intptr_t listenFd;
	intptr_t clientFd;

	SDL_Thread *acceptThread;
	SDL_Thread *sendThread;

	bool heldKeys[SDL_NUM_SCANCODES];
	bool heldButtons[8];
	int pointerX;
	int pointerY;
};

#endif // STREAM_SERVER_H
