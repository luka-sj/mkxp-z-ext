#ifndef CONSOLE_INPUT_H
#define CONSOLE_INPUT_H

#include <SDL_mutex.h>
#include <SDL_thread.h>

#include <queue>
#include <string>
#include <vector>

class ConsoleInput
{
public:
	ConsoleInput();
	~ConsoleInput();

	void start();
	void stop();

	/* Returns the next queued command from the user, or false if none */
	bool poll(std::string &out);

	/* Write a line to the console immediately.
	 * highlight: apply Ruby syntax highlighting when printing. */
	void writeLine(const std::string &line, bool highlight = false);

private:
	static int consoleThreadFun(void *data);

	void redrawInput();
	void submitLine();
	void handleArrowKey(char code);
	void insertChar(char c);
	void backspace();
	void deleteAtCursor();

	SDL_Thread *thread;
	SDL_mutex *mutex;

	/* Serializes all terminal writes (prompt redraws vs. writeLine from
	 * other threads) and guards the render-geometry state below. */
	SDL_mutex *writeMutex;

	/* Physical rows occupied by the last prompt render, and which of
	 * those rows the cursor was left on (both 0 when no prompt is up). */
	size_t lastRenderRows;
	size_t lastCursorRow;

	std::queue<std::string> inputQueue;
	bool needsRedraw;

	std::string inputLine;
	size_t cursorPos;

	std::vector<std::string> history;
	int historyIndex;
	std::string savedInput;

	bool running;

#ifndef __WIN32__
	bool rawModeSet;
#endif
};

/* When set, Debug() routes output here instead of stderr directly. */
extern void (*debugOutputHandler)(const std::string &line);

#endif // CONSOLE_INPUT_H
