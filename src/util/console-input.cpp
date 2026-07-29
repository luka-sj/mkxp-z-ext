#include "console-input.h"

#include <SDL_timer.h>

#include <cstdio>
#include <cstring>

#ifdef __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define CLOSESOCK closesocket
#else
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define CLOSESOCK ::close
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

ConsoleInput::ConsoleInput(int agentPort)
    : queueMutex(SDL_CreateMutex()),
      writeMutex(SDL_CreateMutex()),
      clientMutex(SDL_CreateMutex()),
      stdinThread(nullptr),
      agentThread(nullptr),
      agentPort(agentPort),
      listenFd(-1),
      clientFd(-1),
      running(false)
{}

ConsoleInput::~ConsoleInput()
{
	stop();
	SDL_DestroyMutex(queueMutex);
	SDL_DestroyMutex(writeMutex);
	SDL_DestroyMutex(clientMutex);
}

void ConsoleInput::start()
{
	if (running)
		return;

	running = true;
	stdinThread = SDL_CreateThread(stdinThreadFun, "console-stdin", this);
	if (agentPort > 0)
		agentThread = SDL_CreateThread(agentThreadFun, "console-agent", this);
}

void ConsoleInput::stop()
{
	if (!running)
		return;

	running = false;

	/* Wake the agent thread out of a blocking accept()/recv(). On POSIX,
	 * close() never wakes a blocked peer — only shutdown() does — so the
	 * thread is shaken loose here and closes its own fds on the way out.
	 * Windows closesocket() does cancel blocking calls. */
	SDL_LockMutex(clientMutex);
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
		::shutdown((sock_t)clientFd, SHUT_RDWR);
	if (listenFd != -1)
		::shutdown((sock_t)listenFd, SHUT_RDWR);
#endif
	SDL_UnlockMutex(clientMutex);

	if (agentThread)
	{
		SDL_WaitThread(agentThread, nullptr);
		agentThread = nullptr;
	}

	/* The stdin thread may be parked in a cooked-mode read that cannot be
	 * interrupted portably. On Unix the poll timeout lets it exit, so we
	 * join; on Windows we detach and let process exit reap it. */
	if (stdinThread)
	{
#ifdef __WIN32__
		SDL_DetachThread(stdinThread);
#else
		SDL_WaitThread(stdinThread, nullptr);
#endif
		stdinThread = nullptr;
	}
}

bool ConsoleInput::poll(std::string &out)
{
	SDL_LockMutex(queueMutex);
	if (inputQueue.empty())
	{
		SDL_UnlockMutex(queueMutex);
		return false;
	}
	out = inputQueue.front();
	inputQueue.pop();
	SDL_UnlockMutex(queueMutex);
	return true;
}

bool ConsoleInput::pollAgent(std::string &out)
{
	SDL_LockMutex(queueMutex);
	if (agentQueue.empty())
	{
		SDL_UnlockMutex(queueMutex);
		return false;
	}
	out = agentQueue.front();
	agentQueue.pop();
	SDL_UnlockMutex(queueMutex);
	return true;
}

void ConsoleInput::write(const std::string &line)
{
	SDL_LockMutex(writeMutex);
	fputs(line.c_str(), stderr);
	fputc('\n', stderr);
	fflush(stderr);
	SDL_UnlockMutex(writeMutex);
}

void ConsoleInput::agentWrite(const std::string &line)
{
	std::string buf = line;
	buf += '\n';

	SDL_LockMutex(clientMutex);
	if (clientFd != -1)
	{
		sock_t cfd = (sock_t)clientFd;
		size_t off = 0;
		while (off < buf.size())
		{
			int s = (int)::send(cfd, buf.data() + off,
			                    (int)(buf.size() - off), MSG_NOSIGNAL);
			if (s <= 0)
				break;
			off += (size_t)s;
		}
	}
	SDL_UnlockMutex(clientMutex);
}

int ConsoleInput::stdinThreadFun(void *data)
{
	static_cast<ConsoleInput *>(data)->runStdin();
	return 0;
}

int ConsoleInput::agentThreadFun(void *data)
{
	static_cast<ConsoleInput *>(data)->runAgent();
	return 0;
}

/* Splits accumulated bytes into complete lines and queues them. */
static void drainLines(std::string &pending, std::queue<std::string> &q,
                       SDL_mutex *mutex)
{
	size_t nl;
	while ((nl = pending.find('\n')) != std::string::npos)
	{
		std::string line = pending.substr(0, nl);
		pending.erase(0, nl + 1);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;

		SDL_LockMutex(mutex);
		q.push(line);
		SDL_UnlockMutex(mutex);
	}
}

void ConsoleInput::runStdin()
{
	std::string pending;
	char buf[512];

#ifdef __WIN32__
	HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
	/* Force cooked line input so the console handles echo/editing/cursor. */
	DWORD mode = 0;
	if (GetConsoleMode(hIn, &mode))
		SetConsoleMode(hIn, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
		                        ENABLE_PROCESSED_INPUT);

	while (running)
	{
		if (WaitForSingleObject(hIn, 200) != WAIT_OBJECT_0)
			continue;

		DWORD n = 0;
		if (!ReadFile(hIn, buf, sizeof(buf), &n, nullptr) || n == 0)
		{
			SDL_Delay(100);
			continue;
		}
		pending.append(buf, n);
		drainLines(pending, inputQueue, queueMutex);
	}
#else
	while (running)
	{
		struct pollfd pfd;
		pfd.fd = STDIN_FILENO;
		pfd.events = POLLIN;
		pfd.revents = 0;

		if (::poll(&pfd, 1, 200) <= 0 || !(pfd.revents & POLLIN))
			continue;

		ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
		if (n <= 0)
		{
			SDL_Delay(100);
			continue;
		}
		pending.append(buf, (size_t)n);
		drainLines(pending, inputQueue, queueMutex);
	}
#endif
}

void ConsoleInput::runAgent()
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
	addr.sin_port = htons((unsigned short)agentPort);

	if (::bind(lfd, (sockaddr *)&addr, sizeof(addr)) != 0 ||
	    ::listen(lfd, 1) != 0)
	{
		CLOSESOCK(lfd);
		return;
	}

	SDL_LockMutex(clientMutex);
	listenFd = (intptr_t)lfd;
	SDL_UnlockMutex(clientMutex);

	while (running)
	{
		sock_t cfd = ::accept(lfd, nullptr, nullptr);
		if (cfd == BAD_SOCK)
		{
			if (!running)
				break;
			SDL_Delay(50);
			continue;
		}

#if !defined(__WIN32__) && defined(SO_NOSIGPIPE)
		int one = 1;
		setsockopt(cfd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

		SDL_LockMutex(clientMutex);
		clientFd = (intptr_t)cfd;
		SDL_UnlockMutex(clientMutex);

		std::string pending;
		char buf[1024];
		while (running)
		{
			int n = (int)::recv(cfd, buf, sizeof(buf), 0);
			if (n <= 0)
				break;
			pending.append(buf, (size_t)n);
			drainLines(pending, agentQueue, queueMutex);
		}

		SDL_LockMutex(clientMutex);
		if (clientFd != -1)
		{
			CLOSESOCK((sock_t)clientFd);
			clientFd = -1;
		}
		SDL_UnlockMutex(clientMutex);
	}

	SDL_LockMutex(clientMutex);
	if (listenFd != -1)
	{
		CLOSESOCK((sock_t)listenFd);
		listenFd = -1;
	}
	SDL_UnlockMutex(clientMutex);
}
