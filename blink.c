
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "fun.h"

#define NAME_MAX_LEN 11
char isTop = 1;
char blinks = 0;
char name1[NAME_MAX_LEN] = {0};
char name2[NAME_MAX_LEN] = {0};
char name1x = 0;
char name2x = 0;

int err = 0;

void trigger_redraw(void) {
	// clearScreen();
	for (int i = 1; i<6; i++) {
		setCursorPosition(1,i);
		for (int j = 0; j<30; j++) {
			printf(" ");
		}
	}

	setCursorPosition(5,2);
	setCursorColor(DEFAULT_COLOR);
	setCursorStyle(DEFAULT_STYLE);
	printf("BROTHELLO GAME");

	setCursorPosition(3,4);
	if (isTop) {
		setCursorStyle(BOLD);
		if (blinks) setCursorColor(LIGHTBLUE);
		else setCursorColor(BLUE);
		printf(">  ");
	}
	else {
		setCursorStyle(DEFAULT_STYLE);
		setCursorColor(DEFAULT_COLOR);
		printf("  ");
	}
	printf("Name:    [%-10s]", name1);


	setCursorPosition(3,5);
	if (!isTop) {
		setCursorStyle(BOLD);
		if (blinks) setCursorColor(LIGHTBLUE);
		else setCursorColor(BLUE);
		printf(">  ");
	}
	else {
		setCursorStyle(DEFAULT_STYLE);
		setCursorColor(DEFAULT_COLOR);
		printf("  ");
	}
	printf("Surname: [%-10s]", name2);

	fflush(stdout);
}

void* timer_main( void* _ ) {
	while(1) {
		blinks = !blinks;
		trigger_redraw();
		sleep(1);
	}
}

void program_destructor(void) {
	clearScreen();
	disableRawMode();
	makeCursorVisible();
	leaveAlternateScreen();
	restoreCursorPosition();
}

int main(void) {

	saveCursorPosition();
	enterAlternateScreen();
	clearScreen();
	makeCursorInvisible();
	enableRawMode();
	atexit(program_destructor);

	pthread_t timerThread;
	err = pthread_create(&timerThread, NULL, timer_main, NULL);
	if (err) {
		// puts("No pthreads for us!");
	}
	else {

		char c;
		enum CharParsingStates {
			NORMAL, GOT_ESC, GOT_91
		} cpstate = NORMAL;

		while (read(STDIN_FILENO, &c, 1) == 1 && c != 3) {
			
			if (cpstate == NORMAL) {
				if (c == 27) {
					cpstate = GOT_ESC;
					continue;
				}
				else if (c >= 33 && c <= 126) {
					// char input
					if (isTop==1) {
						if (name1x!=NAME_MAX_LEN-1) {
							// not full
							name1[name1x++] = c;
							name1[name1x] = '\0';
						}
					}
					else {
						if (name2x!=NAME_MAX_LEN-1) {
							// not full
							name2[name2x++] = c;
							name2[name2x] = '\0';
						}
					}
				}
				else if (c == 127) {
					// backspace
					if (isTop==1) {
						if (name1x > 0) name1x--;
						name1[name1x] = '\0';
					}
					else {
						if (name2x > 0) name2x--;
						name2[name2x] = '\0';
					}
				}
				trigger_redraw();
			}
			else if (cpstate == GOT_ESC) {
				if (c == 91) {
					cpstate = GOT_91;
					continue;
				}
				else {
					cpstate = NORMAL;
					continue;
				}
			}
			else if (cpstate == GOT_91) {
				if (c == 65) {
					//up
					isTop=1;
				}
				else if (c == 66) {
					//down
					isTop=0;
				}
				else if (c == 67) {
					// right
				}
				else if (c == 68) {
					// left
				}
				cpstate = NORMAL;
				trigger_redraw();
			}

		}
	}



	// int row,col;
	// int er = getWindowSize(&row,&col);
	// if (er) {
	// 	puts("Error");
	// }
	// else {
	// 	printf("%d x %d \n", col, row);
	// }

	// printf("Enter anything to check again or q to exit: ");
	// char inpt;
	// scanf("%1s", &inpt);

	// if (inpt != 'q') main();
	// return 0;




}