#include "console-input.h"

#include <string>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <set>

#ifdef __WIN32__
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#endif

void (*debugOutputHandler)(const std::string &line) = nullptr;

static const char *PROMPT = ">> ";
static const size_t PROMPT_LEN = 3;

/* --- ANSI color codes --- */
#define CLR_RESET   "\033[0m"
#define CLR_KEYWORD "\033[1;35m"
#define CLR_LITERAL "\033[36m"
#define CLR_STRING  "\033[32m"
#define CLR_SYMBOL  "\033[33m"
#define CLR_CONST   "\033[1;33m"
#define CLR_IVAR    "\033[31m"
#define CLR_GVAR    "\033[31m"
#define CLR_COMMENT "\033[90m"
#define CLR_NUMBER  "\033[36m"
#define CLR_PROMPT  "\033[1;34m"

static const std::set<std::string> &rubyKeywords()
{
	static const std::set<std::string> kw = {
		"alias", "and", "begin", "break", "case", "class", "def",
		"defined?", "do", "else", "elsif", "end", "ensure", "for",
		"if", "in", "module", "next", "not", "or", "raise", "redo",
		"rescue", "retry", "return", "super", "then", "undef",
		"unless", "until", "when", "while", "yield",
		"require", "require_relative", "include", "extend",
		"attr_reader", "attr_writer", "attr_accessor",
		"public", "private", "protected", "lambda", "proc",
		"puts", "print", "p"
	};
	return kw;
}

static bool isWordChar(char c)
{
	return std::isalnum((unsigned char)c) || c == '_';
}

static std::string highlightRuby(const std::string &src)
{
	std::string out;
	out.reserve(src.size() * 2);

	size_t i = 0;
	size_t len = src.size();

	while (i < len)
	{
		if (src[i] == '#')
		{
			out += CLR_COMMENT;
			while (i < len)
				out += src[i++];
			out += CLR_RESET;
		}
		else if (src[i] == '"')
		{
			out += CLR_STRING;
			out += src[i++];
			while (i < len && src[i] != '"')
			{
				if (src[i] == '\\' && i + 1 < len)
					out += src[i++];
				out += src[i++];
			}
			if (i < len)
				out += src[i++];
			out += CLR_RESET;
		}
		else if (src[i] == '\'')
		{
			out += CLR_STRING;
			out += src[i++];
			while (i < len && src[i] != '\'')
			{
				if (src[i] == '\\' && i + 1 < len)
					out += src[i++];
				out += src[i++];
			}
			if (i < len)
				out += src[i++];
			out += CLR_RESET;
		}
		else if (src[i] == ':' && i + 1 < len
		         && (std::isalpha((unsigned char)src[i + 1]) || src[i + 1] == '_')
		         && (i == 0 || !isWordChar(src[i - 1])))
		{
			out += CLR_SYMBOL;
			out += src[i++];
			while (i < len && (isWordChar(src[i]) || src[i] == '?' || src[i] == '!'))
				out += src[i++];
			out += CLR_RESET;
		}
		else if (src[i] == '@')
		{
			out += CLR_IVAR;
			out += src[i++];
			if (i < len && src[i] == '@')
				out += src[i++];
			while (i < len && isWordChar(src[i]))
				out += src[i++];
			out += CLR_RESET;
		}
		else if (src[i] == '$')
		{
			out += CLR_GVAR;
			out += src[i++];
			while (i < len && (isWordChar(src[i]) || src[i] == '!' || src[i] == '?'))
				out += src[i++];
			out += CLR_RESET;
		}
		else if (std::isdigit((unsigned char)src[i]))
		{
			out += CLR_NUMBER;
			if (src[i] == '0' && i + 1 < len && (src[i + 1] == 'x' || src[i + 1] == 'X'))
			{
				out += src[i++];
				out += src[i++];
				while (i < len && std::isxdigit((unsigned char)src[i]))
					out += src[i++];
			}
			else
			{
				while (i < len && (std::isdigit((unsigned char)src[i])
				                   || src[i] == '.' || src[i] == '_'))
					out += src[i++];
			}
			out += CLR_RESET;
		}
		else if (std::isalpha((unsigned char)src[i]) || src[i] == '_')
		{
			size_t start = i;
			while (i < len && (isWordChar(src[i]) || src[i] == '?' || src[i] == '!'))
				i++;
			std::string word = src.substr(start, i - start);

			if (word == "true" || word == "false" || word == "nil" || word == "self")
			{
				out += CLR_LITERAL;
				out += word;
				out += CLR_RESET;
			}
			else if (rubyKeywords().count(word))
			{
				out += CLR_KEYWORD;
				out += word;
				out += CLR_RESET;
			}
			else if (std::isupper((unsigned char)word[0]))
			{
				out += CLR_CONST;
				out += word;
				out += CLR_RESET;
			}
			else
			{
				out += word;
			}
		}
		else
		{
			out += src[i++];
		}
	}

	return out;
}

/* --- Terminal I/O helpers --- */

/* All console UI output goes through rawWrite to STDERR_FILENO.
 * stdout is left alone so game printf/puts goes straight to the
 * terminal without any pipe capture. */

static void rawWrite(const char *str, size_t len)
{
	if (len == 0) return;
#ifdef __WIN32__
	DWORD written;
	HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
	WriteFile(h, str, (DWORD)len, &written, NULL);
#else
	ssize_t r;
	while (len > 0)
	{
		r = write(STDERR_FILENO, str, len);
		if (r <= 0) break;
		str += r;
		len -= r;
	}
#endif
}

static void rawWrite(const std::string &s)
{
	rawWrite(s.c_str(), s.size());
}

static void rawWrite(const char *str)
{
	rawWrite(str, strlen(str));
}

/* Current terminal width in columns; wrap math depends on it, so it is
 * re-queried on every render (cheap, and self-corrects after a resize). */
static size_t terminalWidth()
{
#ifdef __WIN32__
	CONSOLE_SCREEN_BUFFER_INFO info;
	HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
	if (GetConsoleScreenBufferInfo(h, &info) && info.dwSize.X > 0)
		return (size_t)info.dwSize.X;
#else
	struct winsize ws;
	if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return (size_t)ws.ws_col;
#endif
	return 80;
}

static void appendCsi(std::string &buf, size_t n, char cmd)
{
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "\033[%u%c", (unsigned)n, cmd);
	buf += tmp;
}

static bool stdinReady(int timeoutMs)
{
#ifdef __WIN32__
	HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
	return WaitForSingleObject(h, timeoutMs) == WAIT_OBJECT_0;
#else
	struct pollfd pfd;
	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;
	pfd.revents = 0;
	return poll(&pfd, 1, timeoutMs) > 0 && (pfd.revents & POLLIN);
#endif
}

static bool stdinReadChar(char &c)
{
#ifdef __WIN32__
	HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
	DWORD bytesRead = 0;
	if (!ReadFile(h, &c, 1, &bytesRead, NULL) || bytesRead == 0)
		return false;
	return true;
#else
	return read(STDIN_FILENO, &c, 1) == 1;
#endif
}

/* --- ConsoleInput implementation --- */

ConsoleInput::ConsoleInput()
    : thread(nullptr),
      mutex(SDL_CreateMutex()),
      writeMutex(SDL_CreateMutex()),
      lastRenderRows(0),
      lastCursorRow(0),
      needsRedraw(false),
      cursorPos(0),
      historyIndex(-1),
      running(false)
#ifndef __WIN32__
      , rawModeSet(false)
#endif
{}

ConsoleInput::~ConsoleInput()
{
	stop();
	SDL_DestroyMutex(mutex);
	SDL_DestroyMutex(writeMutex);
}

void ConsoleInput::start()
{
	if (running)
		return;

	running = true;
	thread = SDL_CreateThread(consoleThreadFun, "console", this);
}

void ConsoleInput::stop()
{
	running = false;

	if (thread)
	{
		SDL_WaitThread(thread, nullptr);
		thread = nullptr;
	}
}

bool ConsoleInput::poll(std::string &out)
{
	SDL_LockMutex(mutex);

	if (inputQueue.empty())
	{
		SDL_UnlockMutex(mutex);
		return false;
	}

	out = inputQueue.front();
	inputQueue.pop();

	SDL_UnlockMutex(mutex);
	return true;
}

void ConsoleInput::writeLine(const std::string &line, bool highlight)
{
	/* Write directly to the terminal from whatever thread calls us.
	 * The whole payload goes out as one buffered write under writeMutex
	 * so it cannot interleave with a concurrent prompt redraw. */
	std::string buf;

	SDL_LockMutex(writeMutex);

	/* Return to the first row of the prompt render and wipe it (the
	 * input may span several wrapped rows), then print the output. */
	if (lastCursorRow > 0)
		appendCsi(buf, lastCursorRow, 'A');
	buf += "\r\033[J";
	if (highlight)
		buf += highlightRuby(line);
	else
		buf += line;
	buf += "\n";
	rawWrite(buf);

	lastRenderRows = 0;
	lastCursorRow = 0;

	SDL_UnlockMutex(writeMutex);

	SDL_LockMutex(mutex);
	needsRedraw = true;
	SDL_UnlockMutex(mutex);
}

void ConsoleInput::redrawInput()
{
	SDL_LockMutex(writeMutex);

	size_t width = terminalWidth();

	std::string buf;

	/* Return to the first row of the previous render and wipe it. */
	if (lastCursorRow > 0)
		appendCsi(buf, lastCursorRow, 'A');
	buf += "\r\033[J";

	buf += CLR_PROMPT;
	buf += PROMPT;
	buf += CLR_RESET;
	buf += inputLine;

	size_t total = PROMPT_LEN + inputLine.size();

	/* When the text ends flush with the right edge the terminal leaves
	 * the cursor in the deferred-wrap state; commit the wrap so the
	 * physical position is unambiguous. */
	if (total % width == 0)
		buf += "\n";

	size_t endRow = total / width;
	size_t curAbs = PROMPT_LEN + cursorPos;
	size_t curRow = curAbs / width;
	size_t curCol = curAbs % width;

	/* Move from the end of the drawn text to the logical cursor. */
	if (endRow > curRow)
		appendCsi(buf, endRow - curRow, 'A');
	buf += "\r";
	if (curCol > 0)
		appendCsi(buf, curCol, 'C');

	rawWrite(buf);

	lastRenderRows = endRow + 1;
	lastCursorRow = curRow;

	SDL_UnlockMutex(writeMutex);
}

void ConsoleInput::submitLine()
{
	SDL_LockMutex(writeMutex);
	{
		/* Step below the whole (possibly wrapped) input block before
		 * opening the line the output/next prompt will land on. */
		std::string buf;
		if (lastRenderRows > 0 && lastCursorRow < lastRenderRows - 1)
			appendCsi(buf, lastRenderRows - 1 - lastCursorRow, 'B');
		buf += "\n";
		rawWrite(buf);

		lastRenderRows = 0;
		lastCursorRow = 0;
	}
	SDL_UnlockMutex(writeMutex);

	if (!inputLine.empty())
	{
		if (history.empty() || history.back() != inputLine)
			history.push_back(inputLine);

		SDL_LockMutex(mutex);
		inputQueue.push(inputLine);
		SDL_UnlockMutex(mutex);

		inputLine.clear();
	}

	cursorPos = 0;
	historyIndex = -1;
	savedInput.clear();

	redrawInput();
}

void ConsoleInput::handleArrowKey(char code)
{
	switch (code)
	{
	case 'D':
		if (cursorPos > 0)
		{
			cursorPos--;
			redrawInput();
		}
		break;

	case 'C':
		if (cursorPos < inputLine.size())
		{
			cursorPos++;
			redrawInput();
		}
		break;

	case 'A':
	{
		if (history.empty())
			break;

		if (historyIndex == -1)
		{
			savedInput = inputLine;
			historyIndex = (int)history.size() - 1;
		}
		else if (historyIndex > 0)
		{
			historyIndex--;
		}
		else
		{
			break;
		}

		inputLine = history[historyIndex];
		cursorPos = inputLine.size();
		redrawInput();
		break;
	}

	case 'B':
	{
		if (historyIndex == -1)
			break;

		if (historyIndex < (int)history.size() - 1)
		{
			historyIndex++;
			inputLine = history[historyIndex];
		}
		else
		{
			historyIndex = -1;
			inputLine = savedInput;
			savedInput.clear();
		}

		cursorPos = inputLine.size();
		redrawInput();
		break;
	}
	}
}

void ConsoleInput::insertChar(char c)
{
	if (cursorPos == inputLine.size())
		inputLine += c;
	else
		inputLine.insert(cursorPos, 1, c);

	cursorPos++;
	redrawInput();
}

void ConsoleInput::backspace()
{
	if (cursorPos == 0)
		return;

	inputLine.erase(cursorPos - 1, 1);
	cursorPos--;
	redrawInput();
}

void ConsoleInput::deleteAtCursor()
{
	if (cursorPos >= inputLine.size())
		return;

	inputLine.erase(cursorPos, 1);
	redrawInput();
}

int ConsoleInput::consoleThreadFun(void *data)
{
	ConsoleInput *self = static_cast<ConsoleInput *>(data);

#ifndef __WIN32__
	struct termios oldTerm, newTerm;

	if (tcgetattr(STDIN_FILENO, &oldTerm) == 0)
	{
		newTerm = oldTerm;
		/* Disable echo and canonical mode so we get raw keypresses.
		 * Keep OPOST enabled so \n is translated to \r\n by the driver. */
		newTerm.c_lflag &= ~((unsigned)ECHO | (unsigned)ICANON);
		newTerm.c_cc[VMIN] = 0;
		newTerm.c_cc[VTIME] = 0;
		tcsetattr(STDIN_FILENO, TCSANOW, &newTerm);
		self->rawModeSet = true;
	}
#endif

	self->redrawInput();

	while (self->running)
	{
		/* If writeLine fired from another thread, redraw the prompt
		 * so it appears below the new output. */
		SDL_LockMutex(self->mutex);
		bool redraw = self->needsRedraw;
		self->needsRedraw = false;
		SDL_UnlockMutex(self->mutex);
		if (redraw)
			self->redrawInput();

		/* Wait for input */
		if (!stdinReady(16))
			continue;

		char c;
		if (!stdinReadChar(c))
			continue;

		if (c == '\n' || c == '\r')
		{
			self->submitLine();
			/* Consume a trailing \r or \n so terminals that
			 * send \r\n don't trigger a double-submit. */
			if (stdinReady(5))
			{
				char next;
				if (stdinReadChar(next))
				{
					if (next != '\n' && next != '\r')
					{
						if (next >= 32)
							self->insertChar(next);
						else if (next == 127 || next == 8)
							self->backspace();
					}
				}
			}
			continue;
		}
		else if (c == 27)
		{
			if (!stdinReady(50))
				continue;

			char seq;
			if (!stdinReadChar(seq) || seq != '[')
				continue;

			if (!stdinReady(50))
				continue;

			char code;
			if (!stdinReadChar(code))
				continue;

			if (code == '3')
			{
				char tilde;
				if (stdinReady(50) && stdinReadChar(tilde)
				    && tilde == '~')
					self->deleteAtCursor();
			}
			else
			{
				self->handleArrowKey(code);
			}
		}
		else if (c == 127 || c == 8)
		{
			self->backspace();
		}
		else if (c == 1)
		{
			self->cursorPos = 0;
			self->redrawInput();
		}
		else if (c == 5)
		{
			self->cursorPos = self->inputLine.size();
			self->redrawInput();
		}
		else if (c == 3)
		{
			self->inputLine.clear();
			self->cursorPos = 0;
			self->historyIndex = -1;
			self->savedInput.clear();
			self->redrawInput();
		}
		else if (c == 21)
		{
			self->inputLine.erase(0, self->cursorPos);
			self->cursorPos = 0;
			self->redrawInput();
		}
		else if (c >= 32)
		{
			self->insertChar(c);
		}
	}

#ifndef __WIN32__
	if (self->rawModeSet)
		tcsetattr(STDIN_FILENO, TCSANOW, &oldTerm);
#endif

	return 0;
}
