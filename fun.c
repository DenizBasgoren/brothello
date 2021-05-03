
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include "fun.h"



/*
y - 121
t - 116
up - 27 92 65
down - 27 91 66
right - 27 91 67
left - 27 91 68
backspace - 127
esc - 27
ctrl+d - 4

ctrl+a - 1
...
ctrl+z - 26

*/

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		return -1;
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

struct termios orig_termios;

void disableRawMode(void) {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode(void) {
	tcgetattr(STDIN_FILENO, &orig_termios);
	struct termios raw = orig_termios;

	// ISIG = no ctrl+c, ctrl+z
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}



void setCursorPosition( int x, int y) {
	printf("\x1b[%d;%dH", y, x);
}





void saveCursorPosition() {
	printf("\x1b" "7");
}



void restoreCursorPosition() {
	printf("\x1b" "8");
}



void makeCursorInvisible() {
	printf("\x1b[?%dl", 25);
}



void makeCursorVisible() {
	printf("\x1b[?%dh", 25);
}



void setCursorColor( enum Color color ) {
	if (color == 0) {
		printf("\x1b[%dm", 39);
	}
	else if (color >= 1 && color <= 8) {
		printf("\x1b[%dm", color + 29);
	}
}




void setBackgroundColor( enum Color color ) {
	if (color == 0) {
		printf("\x1b[%dm", 49);
	}
	else if (color >= 1 && color <= 8) {
		printf("\x1b[%dm", color + 39);
	}
}



void setCursorStyle( enum Style style) {
	// if (style == 0) {
	// 	printf("\x1b[%dm", 22);
	// 	printf("\x1b[%dm", 23);
	// 	printf("\x1b[%dm", 24);
	// }
	// else if (style >= 1 && style <= 4) {
		printf("\x1b[%dm", style);
	// }
}



void enterAlternateScreen() {
	saveCursorPosition();
	printf("\x1b[?%dh", 47);
}




void leaveAlternateScreen() {
	printf("\x1b[?%dl", 47);
	restoreCursorPosition();
}



void clearScreen() {
	printf("\x1b[%dJ", 2);
	printf("\x1b[%dJ", 3);
	setCursorPosition(1,1);
}


