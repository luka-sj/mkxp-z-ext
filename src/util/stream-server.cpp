#include "stream-server.h"

#include "debugwriter.h"
#include "eventthread.h"

#include <SDL_keyboard.h>
#include <SDL_timer.h>
#include <SDL_video.h>

#include <cmath>
#include <cstring>

#ifdef __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define CLOSESOCK closesocket
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define CLOSESOCK ::close
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static const size_t HEADER_SIZE = 24;
static const size_t RECORD_SIZE = 12;
static const int SEND_TIMEOUT_MS = 2000;

enum RecordKind
{
	RecordKey = 1,
	RecordMotion = 2,
	RecordButton = 3,
	RecordWheel = 4,
	RecordReleaseAll = 5
};

static void putU32(uint8_t *out, uint32_t value)
{
	for (int i = 0; i < 4; ++i)
		out[i] = (uint8_t)(value >> (8 * i));
}

static void putU64(uint8_t *out, uint64_t value)
{
	for (int i = 0; i < 8; ++i)
		out[i] = (uint8_t)(value >> (8 * i));
}

static int32_t getI32(const uint8_t *in)
{
	uint32_t value = (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
	                 ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
	return (int32_t)value;
}

static bool sendAll(sock_t fd, const uint8_t *data, size_t size)
{
	size_t off = 0;
	while (off < size)
	{
		size_t chunk = size - off;
		if (chunk > (1 << 20))
			chunk = 1 << 20;

		int sent = (int)::send(fd, (const char *)data + off, (int)chunk, MSG_NOSIGNAL);
		if (sent <= 0)
			return false;
		off += (size_t)sent;
	}
	return true;
}

static void shutdownSocket(sock_t fd)
{
#ifdef __WIN32__
	::shutdown(fd, SD_BOTH);
#else
	::shutdown(fd, SHUT_RDWR);
#endif
}

static void setSendTimeout(sock_t fd)
{
#ifdef __WIN32__
	DWORD ms = SEND_TIMEOUT_MS;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
#else
	struct timeval tv;
	tv.tv_sec = SEND_TIMEOUT_MS / 1000;
	tv.tv_usec = (SEND_TIMEOUT_MS % 1000) * 1000;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

StreamServer::StreamServer(int port, int frameRate, RGSSThreadData &rtData)
    : rtData(rtData),
      windowID(SDL_GetWindowID(rtData.window)),
      port(port),
      frameTicks(SDL_GetPerformanceFrequency() / (uint64_t)(frameRate > 0 ? frameRate : 60)),
      nextFrameTick(0),
      frameMutex(SDL_CreateMutex()),
      frameCond(SDL_CreateCond()),
      pendingWidth(0),
      pendingHeight(0),
      pendingTime(0),
      pendingSeq(0),
      sentSeq(0),
      socketMutex(SDL_CreateMutex()),
      listenFd(-1),
      clientFd(-1),
      acceptThread(nullptr),
      sendThread(nullptr),
      pointerX(0),
      pointerY(0)
{
	SDL_AtomicSet(&running, 0);
	SDL_AtomicSet(&clientConnected, 0);
	memset(heldKeys, 0, sizeof(heldKeys));
	memset(heldButtons, 0, sizeof(heldButtons));
}

StreamServer::~StreamServer()
{
	stop();
	SDL_DestroyCond(frameCond);
	SDL_DestroyMutex(frameMutex);
	SDL_DestroyMutex(socketMutex);
}

void StreamServer::start()
{
	if (SDL_AtomicGet(&running))
		return;

	SDL_AtomicSet(&running, 1);
	acceptThread = SDL_CreateThread(acceptThreadFun, "stream-accept", this);
	sendThread = SDL_CreateThread(sendThreadFun, "stream-send", this);
}

void StreamServer::stop()
{
	if (!SDL_AtomicGet(&running))
		return;

	SDL_AtomicSet(&running, 0);

	SDL_LockMutex(frameMutex);
	SDL_CondBroadcast(frameCond);
	SDL_UnlockMutex(frameMutex);

	SDL_LockMutex(socketMutex);
#ifdef __WIN32__
	if (clientFd != -1)
	{
		CLOSESOCK((sock_t)clientFd);
		clientFd = -1;
	}
	if (listenFd != -1)
	{
		CLOSESOCK((sock_t)listenFd);
		listenFd = -1;
	}
#else
	if (clientFd != -1)
		shutdownSocket((sock_t)clientFd);
	if (listenFd != -1)
		shutdownSocket((sock_t)listenFd);
#endif
	SDL_UnlockMutex(socketMutex);

	if (sendThread)
	{
		SDL_WaitThread(sendThread, nullptr);
		sendThread = nullptr;
	}
	if (acceptThread)
	{
		SDL_WaitThread(acceptThread, nullptr);
		acceptThread = nullptr;
	}
}

bool StreamServer::connected()
{
	return SDL_AtomicGet(&clientConnected) != 0;
}

bool StreamServer::frameDue()
{
	if (!connected())
		return false;

	uint64_t now = SDL_GetPerformanceCounter();
	if (now < nextFrameTick)
		return false;

	nextFrameTick += frameTicks;
	if (nextFrameTick < now)
		nextFrameTick = now + frameTicks;

	return true;
}

void StreamServer::submitFrame(const void *pixels, int width, int height, uint64_t timeUs)
{
	size_t size = (size_t)width * (size_t)height * 4;

	SDL_LockMutex(frameMutex);
	pendingFrame.resize(size);
	memcpy(pendingFrame.data(), pixels, size);
	pendingWidth = width;
	pendingHeight = height;
	pendingTime = timeUs;
	++pendingSeq;
	SDL_CondSignal(frameCond);
	SDL_UnlockMutex(frameMutex);
}

int StreamServer::acceptThreadFun(void *data)
{
	static_cast<StreamServer *>(data)->runAccept();
	return 0;
}

int StreamServer::sendThreadFun(void *data)
{
	static_cast<StreamServer *>(data)->runSend();
	return 0;
}

void StreamServer::runSend()
{
	uint8_t header[HEADER_SIZE];

	while (SDL_AtomicGet(&running))
	{
		SDL_LockMutex(frameMutex);
		while (SDL_AtomicGet(&running) && pendingSeq == sentSeq)
			SDL_CondWaitTimeout(frameCond, frameMutex, 200);

		if (!SDL_AtomicGet(&running))
		{
			SDL_UnlockMutex(frameMutex);
			break;
		}

		sendingFrame.swap(pendingFrame);
		int width = pendingWidth;
		int height = pendingHeight;
		uint64_t timeUs = pendingTime;
		uint32_t seq = pendingSeq;
		sentSeq = pendingSeq;
		SDL_UnlockMutex(frameMutex);

		memcpy(header, "MKXF", 4);
		putU32(header + 4, (uint32_t)width);
		putU32(header + 8, (uint32_t)height);
		putU32(header + 12, seq);
		putU64(header + 16, timeUs);

		SDL_LockMutex(socketMutex);
		if (clientFd != -1)
		{
			sock_t fd = (sock_t)clientFd;
			if (!sendAll(fd, header, HEADER_SIZE) ||
			    !sendAll(fd, sendingFrame.data(), sendingFrame.size()))
				shutdownSocket(fd);
		}
		SDL_UnlockMutex(socketMutex);
	}
}

void StreamServer::runAccept()
{
	sock_t lfd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == BAD_SOCK)
		return;

	int yes = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((unsigned short)port);

	if (::bind(lfd, (sockaddr *)&addr, sizeof(addr)) != 0 || ::listen(lfd, 1) != 0)
	{
		Debug() << "StreamServer: cannot listen on port" << port;
		CLOSESOCK(lfd);
		return;
	}

	SDL_LockMutex(socketMutex);
	listenFd = (intptr_t)lfd;
	SDL_UnlockMutex(socketMutex);

	Debug() << "StreamServer: listening on port" << port;

	while (SDL_AtomicGet(&running))
	{
		sock_t cfd = ::accept(lfd, nullptr, nullptr);
		if (cfd == BAD_SOCK)
		{
			if (!SDL_AtomicGet(&running))
				break;
			SDL_Delay(50);
			continue;
		}

#if !defined(__WIN32__) && defined(SO_NOSIGPIPE)
		int one = 1;
		setsockopt(cfd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
		setSendTimeout(cfd);

		SDL_LockMutex(socketMutex);
		clientFd = (intptr_t)cfd;
		SDL_UnlockMutex(socketMutex);

		SDL_AtomicSet(&clientConnected, 1);
		Debug() << "StreamServer: client connected";

		uint8_t buf[RECORD_SIZE * 128];
		size_t have = 0;
		while (SDL_AtomicGet(&running))
		{
			int n = (int)::recv(cfd, (char *)buf + have, (int)(sizeof(buf) - have), 0);
			if (n <= 0)
				break;

			have += (size_t)n;
			size_t off = 0;
			while (have - off >= RECORD_SIZE)
			{
				handleRecord(buf + off);
				off += RECORD_SIZE;
			}
			memmove(buf, buf + off, have - off);
			have -= off;
		}

		SDL_AtomicSet(&clientConnected, 0);
		shutdownSocket(cfd);

		SDL_LockMutex(socketMutex);
		bool owned = clientFd == (intptr_t)cfd;
		if (owned)
			clientFd = -1;
		SDL_UnlockMutex(socketMutex);

		if (owned)
			CLOSESOCK(cfd);

		releaseHeld();
		Debug() << "StreamServer: client disconnected";
	}

	SDL_LockMutex(socketMutex);
	if (listenFd != -1)
	{
		CLOSESOCK((sock_t)listenFd);
		listenFd = -1;
	}
	SDL_UnlockMutex(socketMutex);
}

void StreamServer::handleRecord(const uint8_t *record)
{
	bool down = record[1] != 0;
	int code = record[2] | (record[3] << 8);
	int x = getI32(record + 4);
	int y = getI32(record + 8);

	switch (record[0])
	{
	case RecordKey:
		injectKey(code, down);
		break;
	case RecordMotion:
		injectMotion(x, y);
		break;
	case RecordButton:
		injectMotion(x, y);
		injectButton(code, down);
		break;
	case RecordWheel:
		injectWheel(y);
		break;
	case RecordReleaseAll:
		releaseHeld();
		break;
	default:
		break;
	}
}

void StreamServer::injectKey(int scancode, bool down)
{
	if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_NUM_SCANCODES)
		return;

	heldKeys[scancode] = down;

	SDL_Event event;
	SDL_zero(event);
	event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.windowID = windowID;
	event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	event.key.keysym.scancode = (SDL_Scancode)scancode;
	event.key.keysym.sym = SDL_GetKeyFromScancode((SDL_Scancode)scancode);
	pushEvent(event);
}

void StreamServer::injectMotion(int x, int y)
{
	Vec2 ratio = rtData.sizeResoRatio;
	if (ratio.x <= 0 || ratio.y <= 0)
		return;

	pointerX = rtData.screenOffset.x + (int)std::lround(x / ratio.x);
	pointerY = rtData.screenOffset.y + (int)std::lround(y / ratio.y);

	if (!EventThread::mouseState.inWindow)
	{
		SDL_Event enter;
		SDL_zero(enter);
		enter.type = SDL_WINDOWEVENT;
		enter.window.windowID = windowID;
		enter.window.event = SDL_WINDOWEVENT_ENTER;
		pushEvent(enter);
	}

	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_MOUSEMOTION;
	event.motion.windowID = windowID;
	event.motion.which = 0;
	event.motion.x = pointerX;
	event.motion.y = pointerY;
	pushEvent(event);
}

void StreamServer::injectButton(int button, bool down)
{
	if (button < SDL_BUTTON_LEFT || button > SDL_BUTTON_X2)
		return;

	heldButtons[button] = down;

	SDL_Event event;
	SDL_zero(event);
	event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	event.button.windowID = windowID;
	event.button.which = 0;
	event.button.button = (Uint8)button;
	event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	event.button.clicks = 1;
	event.button.x = pointerX;
	event.button.y = pointerY;
	pushEvent(event);
}

void StreamServer::injectWheel(int steps)
{
	if (steps == 0)
		return;

	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_MOUSEWHEEL;
	event.wheel.windowID = windowID;
	event.wheel.which = 0;
	event.wheel.y = steps;
	event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
	pushEvent(event);
}

void StreamServer::releaseHeld()
{
	for (int scancode = 0; scancode < SDL_NUM_SCANCODES; ++scancode)
		if (heldKeys[scancode])
			injectKey(scancode, false);

	for (int button = 0; button < 8; ++button)
		if (heldButtons[button])
			injectButton(button, false);
}

void StreamServer::pushEvent(SDL_Event &event)
{
	SDL_PushEvent(&event);
}
