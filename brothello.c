
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "fun.h"
#include "game.h"

#define TOSTR(NUM) TOSTR_(NUM)
#define TOSTR_(NUM) #NUM

const char* empty_space = "                                           ";


#define NAME_MAXLEN 15
#define MSG_MAXLEN 49
int view = 0; // 0 lobby, 1 game
int highlightX = 0;

// this variable will go true-false-true-.. every second, and is used for visual
// blinking effect (true=lightblue, false=darkblue)
bool blinks = false;

// in order to achieve clicking visual effect, a var is needed to know whether
// a button was clicked on in last second. Also, we need to know which button was
// clicked on. When button click happens, input thread detects, and prints by
// setting this var. After ~1 second, timer thread resets the var and redraws.
// -1=no button, N=Nth highlightable
int clickEffectX = -1;

// again, for visual purposes, to know whether to display 'Connect' or 'Cancel',
// we hold index of clickable, or -1 if there are none. A single number is enough,
// since we don't let player to connect to multiple IPs at the same time.
int connectingX = -1;


// +1 for the last \0 byte. myNameX is needed to know the length of the string.
// myNameX-th index is the first empty byte. To add a letter, we insert, then increment.
char myName[NAME_MAXLEN+1];
int myNameX = 0;

char connectToIp[NAME_MAXLEN+1];
int connectToIpX = 0;

struct player {
	struct sockaddr_in addr;
	char name[NAME_MAXLEN+1];
};

int onlinePlayersX = 0;
// struct player onlinePlayers[10] = {
// 	{{0}, "Aorsan"},
// 	{{0}, "Borsan"},
// 	{{0}, "Corsan"},
// 	{{0}, "Dorsan"},
// };
struct player onlinePlayers[10];


int gameRequestsX = 3;
struct player gameRequests[10] = {
	{{0}, "Korsan"},
	{{0}, "Torsa"},
	{{0}, "Mammmaaamiii"},
};

int botsX = 1; // we only have Zebro for now

char opponentName[NAME_MAXLEN+1];
char opponentBubble[MSG_MAXLEN+1];
char myBubble[MSG_MAXLEN+1];
int myBubbleX = 0;
bool myTurn = true;
bool gameOver = false;
bool showHints = false;
bool showLegalMoves = false;
bool opponentIsBot = false;
enum Player mySide = WHITE_PLAYER;


// bot will wait 2 secs before playing
// countdown is the actual timer. if it's -1, means bot is off.
// if it's 0, means time to make a move for the bot.
// it decreases by 1 every second (in timer thread).
const int BOT_THINKING_TIME = 5;
int botCountdown = -1;

// if countdown > 0 then show transition view of disks (gray)
const int TRANSITION_TIME = 1;
int transitionCountdown = 0;

int udp_sd;


// PROTOTYPES
void program_destructor(void);
void* timer_main( void* _ );
void* input_main( void* _ );
void* udp_main( void* _ );
void* tcp_main( void* _ );
void draw(void);
void draw_lobby(void);
void draw_game(void);
void draw_input(const char* buffer, const int bufLen, const bool highlighted, const char* placeholder);
void draw_button(const char* text, const bool highlighted, const bool clicked);
void clearCursorFromBoard(void);
void prepareGameWithBot(int reqId);
void tryToMakeAMove(struct Move m);
void sockaddr_to_str( struct sockaddr a, char* s);

int main(void) {

	saveCursorPosition();
	enterAlternateScreen();
	clearScreen();
	makeCursorInvisible();
	enableRawMode();
	atexit(program_destructor);

	pthread_t timer, input, udp, tcp;

	int err = 0;
	err |= pthread_create(&timer, NULL, timer_main, NULL);
	err |= pthread_create(&input, NULL, input_main, NULL);
	err |= pthread_create(&udp, NULL, udp_main, NULL);
	err |= pthread_create(&tcp, NULL, tcp_main, NULL);

	if (err) {
		fprintf(stderr, "Pthread failed\n");
		return 1;
	}

	draw();

	pthread_join(input, NULL);
	return 0;
}

void program_destructor(void) {
	clearScreen();
	disableRawMode();
	makeCursorVisible();
	leaveAlternateScreen();
	restoreCursorPosition();
}

void* timer_main( void* _ ) {

	unsigned int counter = 0;

	while(1) {
		sleep(1);
		blinks = !blinks;
		clickEffectX = -1;
		counter++;

		if (transitionCountdown>0) transitionCountdown--;

		if (botCountdown>0) botCountdown--;
		else if (botCountdown==0) {
			copyBoard(board,prevBoard);
			registerMove(board,hintByZebro(board,!mySide),!mySide);
			transitionCountdown = TRANSITION_TIME;
			bool imStuck = playerMustSkip(board,mySide);
			bool botIsStuck = playerMustSkip(board,!mySide);
			if (imStuck && botIsStuck) {
				// gameover
				gameOver = true;
				botCountdown = -1;
			}
			else if (imStuck) {
				// bot plays again
				botCountdown = BOT_THINKING_TIME;
			}
			else {
				myTurn = true;
				botCountdown = -1;
			}
		}

		// once in 10sec, broadcast 'Im online'
		if (counter % 20 == 0) {

			struct sockaddr_in brdcast_addr;
			memset(&brdcast_addr, 0, sizeof(brdcast_addr) );
			brdcast_addr.sin_family = AF_INET;
			brdcast_addr.sin_port = htons(10101);
			brdcast_addr.sin_addr.s_addr = INADDR_BROADCAST;


			int err = sendto(udp_sd, myName, myNameX+1, 0, (struct sockaddr*)&brdcast_addr, sizeof(brdcast_addr));
			if (err==-1) {
				setCursorPosition(1,1);
				fprintf(stdout, strerror(errno) );
				fflush(stdout);
			}
		}

		draw();
	}
	
}
void* input_main( void* _ ) {
	clickEffectX = -1;

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
			else if (c == 13 || c == 32) {
				// enter or space
				if (view == 0) {
					int reqI;
					clickEffectX=highlightX;

					if (highlightX==2) {
						// TODO connect to IP code
					}
					else if (highlightX>=3 && highlightX<=2+2*gameRequestsX) {
						clickEffectX=highlightX;
						if (highlightX%2==1) {
							reqI = highlightX-3;
							// TODO accept connection
						}
						else {
							reqI = highlightX-4;
							// TODO reject code
						}
					}
					else if (highlightX>=3+2*gameRequestsX && highlightX<=2+2*gameRequestsX+onlinePlayersX) {
						// TODO connect to lan player
						reqI = highlightX - (3 + 2*gameRequestsX);

					}
					else {
						// TODO play with bots
						reqI = highlightX - (3 + 2*gameRequestsX + onlinePlayersX);
						if (reqI==0) { // zebro
							// PLAY WITH ZEBRO
							prepareGameWithBot(reqI);						}
					}
				}
				else if (view == 1) {
					 if (c==32) {
						if (myBubbleX!=MSG_MAXLEN) {
							// not full
							myBubble[myBubbleX++] = c;
							myBubble[myBubbleX] = '\0';
						}
						// TODO tcp msg change
					}
					else if (highlightX<64 && c==13) {
						clearCursorFromBoard();
						clickEffectX = highlightX;
						tryToMakeAMove((struct Move){highlightX%8,highlightX/8});						
					}
					else if (highlightX==64 && c==13) {
						clickEffectX = highlightX;
						showHints = !showHints;
					}
					else if (highlightX==65 && c==13) {
						clickEffectX = highlightX;
						showLegalMoves = !showLegalMoves;
					}
					else if (highlightX==66 && c==13) {
						clickEffectX = highlightX;
						// Resign
						view = 0;
						highlightX = 0;
						// TODO let other party know via tcp
					}
				}
			}
			else if (c >= 33 && c <= 125) {
				// char input
				if (view == 0) {
					if (highlightX==0) {
						// my name
						if (myNameX!=NAME_MAXLEN) {
							// not full
							myName[myNameX++] = c;
							myName[myNameX] = '\0';
						}
					}
					else if (highlightX==1) {
						// ip
						if (connectToIpX!=NAME_MAXLEN) {
							// not full
							connectToIp[connectToIpX++] = c;
							connectToIp[connectToIpX] = '\0';
						}
					}
				}
				else if (view == 1) {
					if (myBubbleX!=MSG_MAXLEN) {
						// not full
						myBubble[myBubbleX++] = c;
						myBubble[myBubbleX] = '\0';
					}
				}
			}
			else if (c == 126) {
				// delete
				if (view == 0) {
					if (highlightX==0) {
						memset(myName,'\0',NAME_MAXLEN);
						myNameX = 0;
					}
					else if (highlightX==1) {
						memset(connectToIp,'\0',NAME_MAXLEN);
						connectToIpX = 0;
					}
				}
				else if (view == 1) {
					memset(myBubble,'\0',MSG_MAXLEN);
					myBubbleX = 0;
					// TODO tcp req
				}
			}
			else if (c == 127) {
				// backspace

				if (view == 0) {
					if (highlightX==0) {
						// my name
						if (myNameX > 0) myNameX--;
						myName[myNameX] = '\0';
					}
					else if (highlightX==1) {
						// ip
						if (connectToIpX > 0) connectToIpX--;
						connectToIp[connectToIpX] = '\0';
					}
				}
				else if (view == 1) {
					if (myBubbleX > 0) myBubbleX--;
					myBubble[myBubbleX] = '\0';
				}
			}
			draw();
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
				if (view == 0) {
					if (highlightX==0) {}
					else if (highlightX==1 || highlightX==2) highlightX=0;
					else if (highlightX>=3 && highlightX<=2+2*gameRequestsX) {
						if (highlightX==3 || highlightX==4) highlightX=1;
						else highlightX-=2;
					}
					else if (highlightX>=3+2*gameRequestsX && highlightX<=2+2*gameRequestsX+onlinePlayersX) {
						if (highlightX==3+2*gameRequestsX) highlightX-=2;
						else highlightX--;
					}
					else {
						highlightX--;
					}
				}
				else if (view == 1) {
					clearCursorFromBoard();
					if (highlightX<8 || highlightX==64) {}
					else if (highlightX>64) highlightX--;
					else highlightX-=8;
				}
			}
			else if (c == 66) {
				//down
				if (view == 0) {
					if (highlightX==0) highlightX=1;
					else if (highlightX==1 || highlightX==2) highlightX=3;
					else if (highlightX>=3 && highlightX<=2+2*gameRequestsX) {
						if (highlightX==2+2*gameRequestsX) highlightX++;
						else highlightX+=2;
					}
					else if (highlightX>=3+2*gameRequestsX && highlightX<=2+2*gameRequestsX+onlinePlayersX) {
						highlightX++;
					}
					else {
						if (highlightX != 2+2*gameRequestsX+onlinePlayersX+botsX) {
							highlightX++;
						}
					}
				}
				else if (view == 1) {
					clearCursorFromBoard();
					if (highlightX>=56 && highlightX<64 || highlightX==66) {}
					else if (highlightX>=64) highlightX++;
					else highlightX+=8;
				}
			}
			else if (c == 67) {
				// right
				if (view == 0) {
					if (highlightX==1) highlightX=2;
					else if (highlightX>=3 && highlightX<=2+2*gameRequestsX) {
						if (highlightX%2==1) highlightX++;
					}
				}
				else if (view == 1) {
					clearCursorFromBoard();
					if (highlightX>=64) {}
					else if (highlightX%8==7) {
						if (highlightX/8<=2) highlightX=64;
						else if (highlightX/8==3) highlightX=65;
						else highlightX=66;
					}
					else highlightX++;
				}
			}
			else if (c == 68) {
				// left
				if (view == 0) {
					if (highlightX==2) highlightX=1;
					else if (highlightX>=3 && highlightX<=2+2*gameRequestsX) {
						if (highlightX%2==0) highlightX--;
					}
				}
				else if (view == 1) {
					clearCursorFromBoard();
					if (highlightX==64) highlightX=23;
					else if (highlightX==65) highlightX=31;
					else if (highlightX==66) highlightX=39;
					else if (highlightX%8==0) {}
					else highlightX--;
				}
			}
			cpstate = NORMAL;
			draw();
		}

	}
}



void* udp_main( void* _ ) {

	udp_sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp_sd == -1) {
		puts("No UDP for us");
		exit(1);
	}

	const int one = 1;
	setsockopt(udp_sd,SOL_SOCKET,SO_BROADCAST,&one, sizeof(one));

	struct sockaddr_in udp_addr;
	memset(&udp_addr, 0, sizeof(udp_addr) );
	udp_addr.sin_family = AF_INET;
	udp_addr.sin_port = htons(10101);
	udp_addr.sin_addr.s_addr = INADDR_ANY;

	int err = bind(udp_sd, (struct sockaddr*)&udp_addr, sizeof(udp_addr));
	if (err == -1) {
		close(udp_sd);
		puts("udper2");
		exit(1);
	}




	char response[128] = {0};
	while(true) {
		struct sockaddr bypasser_addr;
		memset(&bypasser_addr, 0, sizeof(bypasser_addr) );
		socklen_t bypasser_addrlen = sizeof(struct sockaddr);

		int result = recvfrom(udp_sd, response, 128, 0, &bypasser_addr, &bypasser_addrlen);
		if (result == -1) {
			close(udp_sd);
			puts("udper3");
			exit(1);
		}
		
		// TODO
		// if (onlinePlayersX<10) {
		// 	// strcpy(onlinePlayers[onlinePlayersX].name, response);
		// 	sockaddr_to_str(bypasser_addr,onlinePlayers[onlinePlayersX].name);
		// 	onlinePlayersX++;
		// }
		///
	}
}


void sockaddr_to_str( struct sockaddr a, char* s) {
	sprintf(s,"%hhu.%hhu.%hhu.%hhu:%hu",
	a.sa_data[2],a.sa_data[3],a.sa_data[4],a.sa_data[5],ntohs(a.sa_data[0]));
}



void* tcp_main( void* _ ) {
	
}

void draw(void) {
	static int prev_view = 0;
	if (view != prev_view) clearScreen();

	if (view == 0) draw_lobby();
	else if (view == 1) draw_game();

	prev_view = view;
	fflush(stdout);
}

void draw_lobby(void) {
	int currentX = 0;
	setCursorPosition(2,2);
	setCursorColor(DEFAULT_COLOR);
	setCursorStyle(DEFAULT_STYLE);
	printf("BROTHELLO GAME");

	setCursorPosition(4,4);
	printf("Name:           ");
	draw_input(myName, NAME_MAXLEN, highlightX==currentX++, "John");

	setCursorPosition(4,5);
	printf("Connect to IP:  ");
	draw_input(connectToIp, NAME_MAXLEN, highlightX==currentX++, "127.0.0.1");

	draw_button(connectingX==currentX ? "CANCEL" : "CONNECT", highlightX==currentX, clickEffectX==currentX);
	
	if (connectingX==currentX++) printf("Connecting...");

	setCursorPosition(4,6);
	setCursorStyle(FAINT);
	printf("If doesn\'t work, connect to each other at the same time.");
	setCursorStyle(DEFAULT_STYLE);

	setCursorPosition(2,8);
	printf("GAME REQUESTS");

	for (int i = 0; i < gameRequestsX; i++) {
		setCursorPosition(4, 10+i);
		printf("%-" TOSTR(NAME_MAXLEN) "s",gameRequests[i].name);
		draw_button("ACCEPT",highlightX==currentX, clickEffectX==currentX);
		currentX++;
		draw_button("REJECT",highlightX==currentX, clickEffectX==currentX);
		currentX++;
	}

	setCursorPosition(2,11+gameRequestsX);
	printf("ONLINE PLAYERS");

	for (int i = 0; i < onlinePlayersX; i++) {
		setCursorPosition(4, 13+gameRequestsX+i);
		printf("%-" TOSTR(NAME_MAXLEN) "s",onlinePlayers[i].name);
		draw_button(connectingX==currentX ? "CANCEL" : "CONNECT", highlightX==currentX, clickEffectX==currentX);
		if (connectingX==currentX++) printf("Connecting...");
	}

	setCursorPosition(2,14+gameRequestsX+onlinePlayersX);
	printf("PLAY AGAINST THE MACHINE");
	
	// hardcoded, because in the near future I'm not planning to add other bots
	setCursorPosition(4, 16+gameRequestsX+onlinePlayersX);
	printf("%-" TOSTR(NAME_MAXLEN) "s","Zebro");
	draw_button("PLAY", highlightX==currentX, clickEffectX==currentX);
	currentX++;
}




void draw_game(void) {
	int currentX = 0;
	setCursorPosition(2,2);
	setCursorColor(DEFAULT_COLOR);
	setCursorStyle(DEFAULT_STYLE);
	printf("BROTHELLO GAME");

	setCursorPosition(8,4);
	printf(empty_space);
	printf(empty_space);
	setCursorPosition(8,4);
	setCursorColor(GREEN);
	if (!myTurn) setCursorStyle(BOLD);
	printf("%s (%d pts):", opponentName, countDiscs(board, !mySide));
	
	setCursorStyle(DEFAULT_STYLE);
	setCursorColor(DEFAULT_COLOR);
	printf(" %s", opponentBubble);

	setCursorPosition(8,23);
	printf(empty_space);
	printf(empty_space);
	setCursorPosition(8,23);
	setCursorColor(GREEN);
	if (myTurn) setCursorStyle(BOLD);
	printf("%s (%d pts):", myName, countDiscs(board, mySide));
	
	setCursorStyle(DEFAULT_STYLE);
	setCursorColor(DEFAULT_COLOR);
	printf(" [%-" TOSTR(MSG_MAXLEN) "s]", myBubble);

	const char* blackDisk = "┌─┐\n\b\b\b└─┘";
	const char* whiteDisk = "▗▄▖\n\b\b\b▝▀▘";
	setCursorPosition(3,4);
	if (mySide==WHITE_PLAYER) printf(blackDisk);
	else printf(whiteDisk);
	setCursorPosition(3,23);
	if (mySide==WHITE_PLAYER) printf(whiteDisk);
	else printf(blackDisk);
	
	struct Move bestMove;
	if (showHints && myTurn) {
		bestMove = hintByZebro(board,mySide);
	}

	for (int y=0; y<8; y++) {
		for (int x=0; x<8; x++) {
			setCursorPosition(5+4*x, 6+2*y);

			if (board[y][x]==WHITE_CELL && prevBoard[y][x]==WHITE_CELL ||
				board[y][x]==WHITE_CELL && prevBoard[y][x]==EMPTY_CELL) {
				// white
				printf(whiteDisk);

			}
			else if (board[y][x]==BLACK_CELL && prevBoard[y][x]==BLACK_CELL ||
					board[y][x]==BLACK_CELL && prevBoard[y][x]==EMPTY_CELL) {
				// black
				printf(blackDisk);

			}
			else if (transitionCountdown && board[y][x]==BLACK_CELL && prevBoard[y][x]==WHITE_CELL ||
					transitionCountdown && board[y][x]==WHITE_CELL && prevBoard[y][x]==BLACK_CELL) {
				// gray
				setCursorStyle(FAINT);
				printf(whiteDisk);
				setCursorStyle(DEFAULT_STYLE);
			}
			else if (board[y][x]==BLACK_CELL) {
				printf(blackDisk);
			}
			else if (board[y][x]==WHITE_CELL) {
				printf(whiteDisk);
			}
			else {
				// dot
				if (myTurn && showHints && y==bestMove.y && x==bestMove.x) {
					printf(" x");
				}
				else if (myTurn && showLegalMoves && moveIsLegal(board,(struct Move){x,y},mySide)) {
					printf(" o");
				}
				else {
					printf(" .");
				}
			}
		}
	}

	// buttons here
	setCursorPosition(40,10);
	printf(empty_space);
	setCursorPosition(40,10);
	printf("Hints are %s.", showHints?"on":"off");
	setCursorPosition(61,10);
	draw_button(showHints?"TURN OFF":"TURN ON", highlightX==64, clickEffectX==64);

	setCursorPosition(40,12);
	printf(empty_space);
	setCursorPosition(40,12);
	printf("Moves are %s.", showLegalMoves?"shown":"not shown");
	setCursorPosition(61,12);
	draw_button(showLegalMoves?"HIDE":"SHOW", highlightX==65, clickEffectX==65);
	
	setCursorPosition(40,14);
	printf(empty_space);
	setCursorPosition(40,14);
	printf("Game is %s.", gameOver?"over":"on");
	setCursorPosition(61,14);
	draw_button(gameOver?"BACK TO MAIN MENU":"RESIGN", highlightX==66, clickEffectX==66);
	

	int hx = highlightX%8, hy = highlightX/8;
	if (clickEffectX!=highlightX && highlightX<64) {
		setCursorStyle(DEFAULT_STYLE);
		setCursorStyle(BOLD);
		if (blinks) setCursorColor(LIGHTBLUE);
		else setCursorColor(BLUE);
		setCursorPosition(4+4*hx, 6+2*hy);
		printf("/\n\b\\");
		setCursorPosition(8+4*hx, 6+2*hy);
		printf("\\\n\b/");
	}

	// draw_input(myName, NAME_MAXLEN, highlightX==currentX++, "John");

}





void draw_input(const char* buffer, const int bufLen, const bool highlighted, const char* placeholder) {
	if (highlighted) {
		setCursorStyle(BOLD);
		if (blinks) setCursorColor(LIGHTBLUE);
		else setCursorColor(BLUE);
	}
	printf("[");

	if (*buffer) {
		printf("%-" TOSTR(NAME_MAXLEN) "s]"  , buffer);
	}
	else {
		setCursorColor(DEFAULT_COLOR);
		setCursorStyle(DEFAULT_STYLE);
		setCursorStyle(FAINT);
		printf("%-" TOSTR(NAME_MAXLEN) "s"  , placeholder);

		setCursorStyle(DEFAULT_STYLE);
		if (highlighted) {
			setCursorStyle(BOLD);
			if (blinks) setCursorColor(LIGHTBLUE);
			else setCursorColor(BLUE);
		}
		printf("]");
	}

	setCursorStyle(DEFAULT_STYLE);
	setCursorColor(DEFAULT_COLOR);
}

void draw_button(const char* text, const bool highlighted, const bool clicked) {
	if (highlighted) {
		setCursorStyle(BOLD);
	}
	if (highlighted && !clicked) {
		if (blinks) setCursorColor(LIGHTBLUE);
		else setCursorColor(BLUE);
	}

	if (clicked) {
		printf("(%s)", text);
	}
	else {
		printf(" %s ", text);
	}

	setCursorStyle(DEFAULT_STYLE);
	setCursorColor(DEFAULT_COLOR);
}


void clearCursorFromBoard(void) {
	int hx = highlightX%8, hy = highlightX/8;
	setCursorPosition(4+4*hx, 6+2*hy);
	printf(" \n\b ");
	setCursorPosition(8+4*hx, 6+2*hy);
	printf(" \n\b ");
}


void prepareGameWithBot(int reqId) {
	view = 1;
	highlightX = 0;
	connectingX = -1;
	memset(myBubble,'\0',MSG_MAXLEN);
	myBubbleX = 0;
	memset(opponentBubble,'\0',MSG_MAXLEN);
	copyBoard(startingPositionBoard, board);
	copyBoard(startingPositionBoard, prevBoard);
	mySide = blinks ? WHITE_PLAYER : BLACK_PLAYER;
	myTurn = !blinks;
	if (!myTurn) botCountdown = 2;
	gameOver = false;
	opponentIsBot = true;
	strcpy(opponentName, "Zebro");


}

void tryToMakeAMove(struct Move m) {
	if (gameOver) return;

	if (moveIsLegal(board,m,mySide)) {
		copyBoard(board,prevBoard);
		registerMove(board,m,mySide);

		transitionCountdown = TRANSITION_TIME;
		bool imStuck = playerMustSkip(board,mySide);
		bool hesStuck = playerMustSkip(board,!mySide);
		if (imStuck && hesStuck) {
			// gameover
			gameOver = true;
		}
		else if (hesStuck) {
			// i play again
		}
		else {
			// bot's turn
			myTurn = false;
			botCountdown = BOT_THINKING_TIME;
		}

		// inform the other party via tcp
		if (!opponentIsBot) {
		// TODO tcp move
		}
	
	}

	
}