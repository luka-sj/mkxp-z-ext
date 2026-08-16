#ifndef CONSOLE_INPUT_H
#define CONSOLE_INPUT_H

#include <SDL_mutex.h>
#include <SDL_thread.h>

#include <cstdint>
#include <queue>
#include <string>

/* Cooked-mode developer console. Reads complete lines from stdin (the terminal
 * owns echo/editing/cursor, so nothing here can desync the prompt) and, when an
 * agent port is configured, from a single loopback TCP client. Runs only in
 * debug mode; lives for the whole process. */
class ConsoleInput
{
public:
	/* agentPort 0 disables the agent TCP channel. */
	explicit ConsoleInput(int agentPort);
	~ConsoleInput();

	void start();
	void stop();

	/* Next queued human command; false if none. */
	bool poll(std::string &out);
	/* Next queued agent command; false if none. */
	bool pollAgent(std::string &out);

	/* Human eval output → terminal (stderr). */
	void write(const std::string &line);
	/* One framed agent response → the connected client socket. */
	void agentWrite(const std::string &line);

private:
	static int stdinThreadFun(void *data);
	static int agentThreadFun(void *data);

	void runStdin();
	void runAgent();

	/* Guards inputQueue / agentQueue. */
	SDL_mutex *queueMutex;
	/* Serializes writes to stderr. */
	SDL_mutex *writeMutex;
	/* Guards clientFd against the accept loop replacing it. */
	SDL_mutex *clientMutex;

	std::queue<std::string> inputQueue;
	std::queue<std::string> agentQueue;

	SDL_Thread *stdinThread;
	SDL_Thread *agentThread;

	int agentPort;
	/* Socket handles held as intptr_t so a Win64 SOCKET fits; -1 == none. */
	intptr_t listenFd;
	intptr_t clientFd;

	bool running;
};

#endif // CONSOLE_INPUT_H
