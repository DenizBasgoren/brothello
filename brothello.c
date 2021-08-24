
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

const char* gameOverReasons[] = {
	"You won!",
	"You lost!",
	"Game is tied!",
	"Connection is lost.",
	"Opponent resigned.",
	"You resigned."
};

#define NAME_MAXLEN 15
#define MSG_MAXLEN 49
#define PLAYERS_MAXLEN 10
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
	int sd;
	struct sockaddr_in addr;
	char name[NAME_MAXLEN+1];
};

int onlinePlayersX = 0;
// struct player onlinePlayers[PLAYERS_MAXLEN] = {
// 	{0, {0}, "Aorsan"},
// 	{0, {0}, "Borsan"},
// 	{0, {0}, "Corsan"},
// 	{0, {0}, "Dorsan"},
// };
struct player onlinePlayers[PLAYERS_MAXLEN];


int gameRequestsX = 0;
// struct player gameRequests[PLAYERS_MAXLEN] = {
// 	{1, {0}, "Korsan"},
// 	{2, {0}, "Torsa"},
// 	{3, {0}, "Mammmaaamiii"},
// };
struct player gameRequests[PLAYERS_MAXLEN];

int botsX = 1; // we only have Zebro for now

char opponentName[NAME_MAXLEN+1];
char opponentBubble[MSG_MAXLEN+1];
char myBubble[MSG_MAXLEN+1];
int myBubbleX = 0;
bool myTurn = true;
bool gameOver = false;
int gameOverReason = 0;
bool showHints = false;
bool showLegalMoves = false;
bool opponentIsBot = false;
enum Player mySide = WHITE_PLAYER;

struct SocketAndAddress {
	struct sockaddr_in addr;
	int sd;
};

// bot will wait 2 secs before playing
// countdown is the actual timer. if it's -1, means bot is off.
// if it's 0, means time to make a move for the bot.
// it decreases by 1 every second (in timer thread).
const int BOT_THINKING_TIME = 1;
int botCountdown = -1;

// if countdown > 0 then show transition view of disks (gray)
const int TRANSITION_TIME = 1;
int transitionCountdown = 0;

int udp_sd, tcp_server_sd, tcp_client_sd, currentlyPlayingWith_sd;


// PROTOTYPES
void program_destructor(void);
void* timer_main( void* _ );
void* input_main( void* _ );
void* udp_main( void* _ );
void* tcp_main( void* _ );
void* opponent_main( void* arg );
void draw(void);
void draw_lobby(void);
void draw_game(void);
void draw_input(const char* buffer, const int bufLen, const bool highlighted, const char* placeholder);
void draw_button(const char* text, const bool highlighted, const bool clicked);
void clearCursorFromBoard(void);
void prepareGameWithBot(int reqId);
void prepareGameWithHuman(char* name, enum Player side, int sd);
bool tryToMakeAMove(struct Move m, enum Player side);
void sockaddr_to_str( struct sockaddr a, char* s);
void sockaddr_to_stdout( struct sockaddr a );
bool sockaddr_cmp( struct sockaddr_in a, struct sockaddr_in b);
bool isAProperName( const char* str, int len);
void doAftermoveChecks(void);

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
	close(tcp_server_sd);
	close(udp_sd);
	ssize_t bytesSent;
	if (connectingX > -1) {
		bytesSent = write(tcp_client_sd, "-", 2);
	}
	close(tcp_client_sd);
	for (int i = 0; i<gameRequestsX; i++) {
		bytesSent = write(gameRequests[i].sd, "-", 2);
		close(gameRequests[i].sd);
	}
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
			struct Move m = hintByZebro(board,!mySide);
			if ( !tryToMakeAMove(m, !mySide) ) {
				gameOver = true;
				gameOverReason = 4; // opponent resigned
			}
		}

		// once in 10sec, broadcast 'Im online'
		if (counter % 2 == 0) { ///

			struct sockaddr_in brdcast_addr;
			memset(&brdcast_addr, 0, sizeof(brdcast_addr) );
			brdcast_addr.sin_family = AF_INET;
			brdcast_addr.sin_port = htons(10101);
			brdcast_addr.sin_addr.s_addr = INADDR_BROADCAST;


			int err = sendto(udp_sd, myName, myNameX+1, 0, (struct sockaddr*)&brdcast_addr, sizeof(brdcast_addr));
			if (err==-1) {
				// don't do anything on error
				// setCursorPosition(1,1);
				// fprintf(stdout, strerror(errno) );
				// fflush(stdout);
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

	tcp_client_sd = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_client_sd == -1) {
		fprintf(stderr,"cant create socket. input_main er1");
		exit(1);
	}

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
							reqI = (highlightX-3)/2;
							// TODO accept connection
							char buffer[NAME_MAXLEN+2] = {'!'};
							strcpy(buffer+1, myName);
							fprintf(stderr,"Sent: %s\n",buffer);
							ssize_t bytesSent = write(gameRequests[reqI].sd, buffer, NAME_MAXLEN+2);
							if (bytesSent != NAME_MAXLEN+2) {
								close(gameRequests[reqI].sd);
								for (int i = reqI+1; i<gameRequestsX; i++) {
									memcpy(&gameRequests[i-1], &gameRequests[i], sizeof(struct player) );
									memset(&gameRequests[i], 0, sizeof(struct player) );
								}
								gameRequestsX--;
								connectingX -= 2;
								highlightX -= 2;
								clickEffectX = -1;
								draw();
								continue;
							}

							close(tcp_client_sd);
							connectingX = -1;

							// the one who 'accept's plays as white
							prepareGameWithHuman(gameRequests[reqI].name, WHITE_PLAYER, gameRequests[reqI].sd);

						}
						else {
							reqI = (highlightX-4)/2;
							// reject code
							fprintf(stderr,"Sent: %s\n","-");
							ssize_t bytesSent = write(gameRequests[reqI].sd, "-", 2);
							
							close(gameRequests[reqI].sd);
							for (int i = reqI+1; i<gameRequestsX; i++) {
								memcpy(&gameRequests[i-1], &gameRequests[i], sizeof(struct player) );
								memset(&gameRequests[i], 0, sizeof(struct player) );
							}
							gameRequestsX--;
							connectingX -= 2;
							highlightX -= 2;
							clickEffectX = -1;
						}
					}
					else if (highlightX>=3+2*gameRequestsX && highlightX<=2+2*gameRequestsX+onlinePlayersX) {
						// TODO connect to lan player
						reqI = highlightX - (3 + 2*gameRequestsX);

						if (highlightX==connectingX) {
							// cancel
							fprintf(stderr,"Sent: %s\n","-");
							ssize_t bytesSent = write(tcp_client_sd, "-", 2);
							
							close(tcp_client_sd);
							connectingX = -1;
						}
						else {
							// connect
							close(tcp_client_sd);
							tcp_client_sd = socket(AF_INET, SOCK_STREAM, 0);
							connectingX = -1;
							int err = connect(tcp_client_sd, (struct sockaddr*) &onlinePlayers[reqI].addr, sizeof(struct sockaddr) );
							if (err == -1) {
								// cant connect
								close(tcp_client_sd);
								continue;
							}
							connectingX=highlightX;

							char buffer[NAME_MAXLEN+2] = {'?'};
							strcpy(buffer+1, myName);
							fprintf(stderr,"Sent: %s\n",buffer);
							ssize_t bytesSent = write(tcp_client_sd, buffer, NAME_MAXLEN+2);
							if (bytesSent != NAME_MAXLEN+2) {
								close(tcp_client_sd);
								connectingX = -1;
							}

							// create a thread for reading
							struct SocketAndAddress* bypasser = calloc( sizeof(struct SocketAndAddress), 1 );
							bypasser->sd = tcp_client_sd;
							bypasser->addr = onlinePlayers[reqI].addr;

							pthread_t opponent;
							err = pthread_create(&opponent, NULL, &opponent_main, bypasser);
							if (err) {
								close(bypasser->sd);
								close(tcp_server_sd);
								free(bypasser);
								fprintf(stderr,"tcper5");
								exit(1);
							}
							
						}

					}
					else {
						reqI = highlightX - (3 + 2*gameRequestsX + onlinePlayersX);
						if (reqI==0) { // zebro
							// PLAY WITH ZEBRO
							prepareGameWithBot(reqI);
						}
					}
				}
				else if (view == 1) {
					if (c==32) { // space
						if (myBubbleX!=MSG_MAXLEN) {
							// not full
							myBubble[myBubbleX++] = c;
							myBubble[myBubbleX] = '\0';
						}
					}
					else if (highlightX<64 && c==13) {
						clearCursorFromBoard();
						clickEffectX = highlightX;

						if (!opponentIsBot) {
							char buffer[4] = {'#'};
							buffer[1] = highlightX/8 + '0';
							buffer[2] = highlightX%8 + '0';
							fprintf(stderr,"Sent: %s\n",buffer);
							ssize_t bytesSent = write(currentlyPlayingWith_sd, buffer, 4);
							if (bytesSent != 4) {
								close(tcp_client_sd);
								gameOver = true;
								gameOverReason = 3; // opponent disconnected
							}
						}

						tryToMakeAMove((struct Move){highlightX%8,highlightX/8}, mySide);
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
						if (gameOver) { // TODO Go to main menu
							view = 0;
							highlightX = 0;
							currentlyPlayingWith_sd = 0;
						}
						else {
							gameOver = true;
							gameOverReason = 5; // you resigned

							if (!opponentIsBot) {
								fprintf(stderr,"Sent: %s\n","*");
								ssize_t bytesSent = write(currentlyPlayingWith_sd, "*", 2);
								if (bytesSent != 2) {
									close(tcp_client_sd);
									gameOver = true;
									gameOverReason = 3; // opponent disconnected
								}
							}
						}
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
					if (!opponentIsBot) {
						char buffer[MSG_MAXLEN+2] = {'>'};
						strcpy(buffer+1, myBubble);
						fprintf(stderr,"Sent: %s\n",buffer);
						ssize_t bytesSent = write(currentlyPlayingWith_sd, buffer, MSG_MAXLEN+2);
						if (bytesSent != MSG_MAXLEN+2) {
							close(tcp_client_sd);
							gameOver = true;
							gameOverReason = 3; // opponent disconnected
						}
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
					if (!opponentIsBot) {
						fprintf(stderr,"Sent: %s\n",">");
						ssize_t bytesSent = write(currentlyPlayingWith_sd, ">", 2);
						if (bytesSent != 2) {
							close(tcp_client_sd);
							gameOver = true;
							gameOverReason = 3; // opponent disconnected
						}
					}
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

					// char input tcp (NOTE: this is a copy from above - char input section)
					if (!opponentIsBot) {
						char buffer[MSG_MAXLEN+2] = {'>'};
						strcpy(buffer+1, myBubble);
						fprintf(stderr,"Sent: %s\n",buffer);
						ssize_t bytesSent = write(currentlyPlayingWith_sd, buffer, MSG_MAXLEN+2);
						if (bytesSent != MSG_MAXLEN+2) {
							close(tcp_client_sd);
							gameOver = true;
							gameOverReason = 3; // opponent disconnected
						}
					}
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
		fprintf(stderr,"No UDP for us");
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
		fprintf(stderr,"udper2");
		exit(1);
	}



	// we only get names by UDP, so length is name_length + null byte
	char response[NAME_MAXLEN+1] = {0};
	while(true) {
		struct sockaddr_in bypasser_addr;
		memset(&bypasser_addr, 0, sizeof(bypasser_addr) );
		socklen_t bypasser_addrlen = sizeof(struct sockaddr_in);

		int result = recvfrom(udp_sd, response, NAME_MAXLEN+1, 0, (struct sockaddr*)&bypasser_addr, &bypasser_addrlen);
		if (result == -1) {
			close(udp_sd);
			fprintf(stderr,"udper3");
			exit(1);
		}

		// check if it's a proper name: null terminated and all chars [33,125]
		if (!isAProperName(response, NAME_MAXLEN+1)) continue;

		bool found = false;
		for (int i = 0; i<onlinePlayersX; i++) {
			bool sameAddr = sockaddr_cmp(onlinePlayers[i].addr, bypasser_addr);
			bool sameName = strcmp(onlinePlayers[i].name, response) == 0;

			if (sameAddr && sameName) {
				found = true;
				break;
			}
			else if (sameAddr) {
				strcpy(onlinePlayers[i].name, response);
				found = true;
				break;
			}
		}

		if (found) continue;

		if (onlinePlayersX==PLAYERS_MAXLEN) {

			if (highlightX>=3+2*gameRequestsX+onlinePlayersX) {
				highlightX -= PLAYERS_MAXLEN;
			}
			else if (highlightX>=3+2*gameRequestsX) {
				highlightX = 2+2*gameRequestsX;
			}

			onlinePlayersX = 0;
		}

		memcpy( &onlinePlayers[onlinePlayersX].addr, &bypasser_addr, sizeof(struct sockaddr_in) );
		strcpy( onlinePlayers[onlinePlayersX].name, response);

		// we use >= here because when we stand at the Zebro entry, after onlinePlayersX++, we stand
		// on the prev entry now, and need to be advanced.
		if (highlightX>2+2*gameRequestsX+onlinePlayersX) {
			highlightX++;
		}
		
		onlinePlayersX++;
	}
}

// check if it's a proper name: null terminated and all chars [33,125]
bool isAProperName( const char* str, int len) {
	bool endsWithZero = false;
	bool allCharsAreAlphanumeric = true;
	for (int i = 0; i<len; i++) {
		if (str[i] == '\0') {
			endsWithZero = true;
			break;
		}
		if (str[i] < 33 || str[i] > 125) {
			allCharsAreAlphanumeric = false;
			break;
		}
	}
	if (!endsWithZero || !allCharsAreAlphanumeric) {
		return false;
	}
	return true;
}


void sockaddr_to_str( struct sockaddr a, char* s) {
	sprintf(s,"%hhu.%hhu.%hhu.%hhu:%hu",
	a.sa_data[2],a.sa_data[3],a.sa_data[4],a.sa_data[5],ntohs(*(short*)&a.sa_data[0]));
}

void sockaddr_to_stdout( struct sockaddr a ) {
	printf("%hhu.%hhu.%hhu.%hhu:%hu",
	a.sa_data[2],a.sa_data[3],a.sa_data[4],a.sa_data[5],ntohs(*(short*)&a.sa_data[0]));
}

bool sockaddr_cmp( struct sockaddr_in a, struct sockaddr_in b) {
	return memcmp( &a, &b, 8) == 0;
}



void* tcp_main( void* _ ) {
	tcp_server_sd = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_server_sd == -1) {
		fprintf(stderr,"No TCP for us");
		exit(1);
	}

	struct sockaddr_in tcp_addr;
	memset(&tcp_addr, 0, sizeof(tcp_addr) );
	tcp_addr.sin_family = AF_INET;
	tcp_addr.sin_port = htons(10101);
	tcp_addr.sin_addr.s_addr = INADDR_ANY;

	int err = bind(tcp_server_sd, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr));
	if (err == -1) {
		close(tcp_server_sd);
		fprintf(stderr,"tcper2");
		exit(1);
	}

	err = listen(tcp_server_sd, 3);
	if (err == -1) {
		close(tcp_server_sd);
		fprintf(stderr,"tcper3");
		exit(1);
	}

	while (true) {
		
		struct SocketAndAddress* bypasser = calloc( sizeof(struct SocketAndAddress), 1 );

		socklen_t bypasser_addrlen = sizeof(struct sockaddr);
		
		bypasser->sd = accept(tcp_server_sd, (struct sockaddr*)&bypasser->addr, &bypasser_addrlen);
		if (!bypasser->sd) {
			close(tcp_server_sd);
			fprintf(stderr,"tcper4");
			exit(1);
		}

		pthread_t opponent;
		err = pthread_create(&opponent, NULL, &opponent_main, bypasser);
		if (err) {
			close(bypasser->sd);
			close(tcp_server_sd);
			free(bypasser);
			fprintf(stderr,"tcper5");
			exit(1);
		}
	}
}

void* opponent_main( void* arg ) {
	struct SocketAndAddress* opponent = arg;

	// msglen because it's the longest payload ('>' + about 50 characters + '\0')
	char response[MSG_MAXLEN+2] = {0};
	while(true) {

		memset(response, 0, MSG_MAXLEN+2);
		int len = read(opponent->sd, response, MSG_MAXLEN+2);
		fprintf(stderr,"Recv (len %d): %s\n",len,response);

		if (len == 0 || len == -1) {
			// TODO disconnected?
			break;
		}

		bool nullTerminated = false;
		for (int i = 0; i<MSG_MAXLEN+2; i++) {
			if (response[i] == '\0') {
				nullTerminated = true;
				break;
			}
		}

		if (!nullTerminated) continue;

		// len > 0, which means we need to check the payload
		if (response[0] == '>') {
			// >msg
			if (view!=1 || opponent->sd != currentlyPlayingWith_sd) continue;
			strcpy(opponentBubble, response+1);
		}
		else if (response[0] == '!') {
			// !mansiya
			if (opponent->sd != tcp_client_sd) continue;

			if( !isAProperName(response+1, NAME_MAXLEN+1) ) continue;
			prepareGameWithHuman(response+1, BLACK_PLAYER, opponent->sd);
		}
		else if (response[0] == '#') {
			// #34 (yx) y increasing downwards, x rightwards
			if (view!=1 || opponent->sd != currentlyPlayingWith_sd) continue;
			tryToMakeAMove((struct Move){response[2]-'0',response[1]-'0'}, !mySide);
		}
		else if (response[0] == '?') {
			// ?korsan
			if ( opponent->sd == tcp_client_sd) continue;

			if( !isAProperName(response+1, NAME_MAXLEN+1) ) continue;

			bool found = false;
			for (int i = 0; i<gameRequestsX; i++) {
				bool sameAddr = sockaddr_cmp(gameRequests[i].addr, opponent->addr);

				if (sameAddr) {
					strcpy(gameRequests[i].name, response+1);
					found = true;
					break;
				}
			}

			if(!found && gameRequestsX != PLAYERS_MAXLEN) {
				memcpy( &gameRequests[gameRequestsX].addr, &opponent->addr, sizeof(struct sockaddr_in) );
				strcpy( gameRequests[gameRequestsX].name, response+1);
				gameRequests[gameRequestsX].sd = opponent->sd;
				gameRequestsX++;
				if (connectingX != -1) connectingX += 2;
				if (highlightX>=3+2*gameRequestsX) highlightX += 2;
			}

		}
		else if (response[0] == '*') {
			// *
			if (view!=1 || opponent->sd != currentlyPlayingWith_sd) continue;

			gameOver = true;
			gameOverReason = 4; // opponent resigned
		}
		else if (response[0] == '-') {
			if ( opponent->sd == tcp_client_sd) {
				// they rejected. do "cancel"
				close(opponent->sd);
				connectingX = -1;
			}
			else {
				// they disconnected. do "reject"
				int reqI = -1; // not found
				for (int i = 0; i<gameRequestsX; i++) {
					if (gameRequests[i].sd == opponent->sd) {
						reqI = i;
						break;
					}
				}

				close(opponent->sd);

				if (reqI == -1) {
					continue;
				}

				for (int i = reqI+1; i<gameRequestsX; i++) {
					memcpy(&gameRequests[i-1], &gameRequests[i], sizeof(struct player) );
					memset(&gameRequests[i], 0, sizeof(struct player) );
				}
				gameRequestsX--;
				connectingX -= 2;
				
				if (highlightX >= 3+2*reqI) highlightX -= 2;
			}
		}
		else {
			// ignore if no message patterns match against this string
		}
	}

	free(arg);
}

void draw(void) {
	static int prev_view = 0;
	// if (view != prev_view) clearScreen();
	clearScreen();
	
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
	printf("%s","BROTHELLO GAME");

	setCursorPosition(4,4);
	printf("%s","Name:           ");
	draw_input(myName, NAME_MAXLEN, highlightX==currentX++, "John");

	setCursorPosition(4,5);
	printf("%s","Connect to IP:  ");
	draw_input(connectToIp, NAME_MAXLEN, highlightX==currentX++, "127.0.0.1");

	draw_button(connectingX==currentX ? "CANCEL" : "CONNECT", highlightX==currentX, clickEffectX==currentX);
	
	if (connectingX==currentX++) printf("%s","Connecting...");

	setCursorPosition(4,6);
	setCursorStyle(FAINT);
	printf("%s","If doesn\'t work, connect to each other at the same time.");
	setCursorStyle(DEFAULT_STYLE);

	setCursorPosition(2,8);
	printf("%s","GAME REQUESTS");

	for (int i = 0; i < gameRequestsX; i++) {
		setCursorPosition(4, 10+i);
		char ipAddr[22] = {0};
		sockaddr_to_str( *(struct sockaddr*) &gameRequests[i].addr, ipAddr);
		printf("%-22s", ipAddr);
		printf("%-" TOSTR(NAME_MAXLEN) "s",gameRequests[i].name);
		draw_button("ACCEPT",highlightX==currentX, clickEffectX==currentX);
		currentX++;
		draw_button("REJECT",highlightX==currentX, clickEffectX==currentX);
		currentX++;
	}

	setCursorPosition(2,11+gameRequestsX);
	printf("%s","ONLINE PLAYERS");

	for (int i = 0; i < onlinePlayersX; i++) {
		setCursorPosition(4, 13+gameRequestsX+i);
		char ipAddr[22] = {0};
		sockaddr_to_str( *(struct sockaddr*) &onlinePlayers[i].addr, ipAddr);
		printf("%-22s", ipAddr);
		printf("%-" TOSTR(NAME_MAXLEN) "s",onlinePlayers[i].name);
		draw_button(connectingX==currentX ? "CANCEL" : "CONNECT", highlightX==currentX, clickEffectX==currentX);
		if (connectingX==currentX++) printf("%s","Connecting...");
	}

	setCursorPosition(2,14+gameRequestsX+onlinePlayersX);
	printf("%s","PLAY AGAINST THE MACHINE");
	
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
	printf("%s","BROTHELLO GAME");

	setCursorPosition(8,4);
	printf("%s",empty_space);
	printf("%s",empty_space);
	setCursorPosition(8,4);
	setCursorColor(GREEN);
	if (!myTurn) setCursorStyle(BOLD);
	printf("%s (%d pts):", opponentName, countDiscs(board, !mySide));
	
	setCursorStyle(DEFAULT_STYLE);
	setCursorColor(DEFAULT_COLOR);
	printf(" %s", opponentBubble);

	setCursorPosition(8,23);
	printf("%s",empty_space);
	printf("%s",empty_space);
	setCursorPosition(8,23);
	setCursorColor(GREEN);
	if (myTurn) setCursorStyle(BOLD);
	int myPts = countDiscs(board, mySide);
	printf("%s (%d pts):", myName, myPts);
	
	setCursorStyle(DEFAULT_STYLE);
	setCursorColor(DEFAULT_COLOR);
	printf(" [%-" TOSTR(MSG_MAXLEN) "s]", myBubble);
	if (myBubbleX == 0) {
		setCursorPosition(8+myNameX+(myPts>=10?2:1)+10 ,23);
		setCursorStyle(FAINT);
		printf("%s","Say \"Hello\" to your opponent!");
		setCursorStyle(DEFAULT_STYLE);
	}

	const char* blackDisk = "┌─┐\n\b\b\b└─┘";
	const char* whiteDisk = "▗▄▖\n\b\b\b▝▀▘";
	setCursorPosition(3,4);
	if (mySide==WHITE_PLAYER) printf("%s",blackDisk);
	else printf("%s",whiteDisk);
	setCursorPosition(3,23);
	if (mySide==WHITE_PLAYER) printf("%s",whiteDisk);
	else printf("%s",blackDisk);
	
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
				printf("%s",whiteDisk);

			}
			else if (board[y][x]==BLACK_CELL && prevBoard[y][x]==BLACK_CELL ||
					board[y][x]==BLACK_CELL && prevBoard[y][x]==EMPTY_CELL) {
				// black
				printf("%s",blackDisk);

			}
			else if (transitionCountdown && board[y][x]==BLACK_CELL && prevBoard[y][x]==WHITE_CELL ||
					transitionCountdown && board[y][x]==WHITE_CELL && prevBoard[y][x]==BLACK_CELL) {
				// gray
				setCursorStyle(FAINT);
				printf("%s",whiteDisk);
				setCursorStyle(DEFAULT_STYLE);
			}
			else if (board[y][x]==BLACK_CELL) {
				printf("%s",blackDisk);
			}
			else if (board[y][x]==WHITE_CELL) {
				printf("%s",whiteDisk);
			}
			else {
				// dot
				if (myTurn && showHints && y==bestMove.y && x==bestMove.x) {
					setCursorColor(RED);
					printf("%s"," x");
					setCursorColor(DEFAULT_COLOR);
				}
				else if (myTurn && showLegalMoves && moveIsLegal(board,(struct Move){x,y},mySide)) {
					setCursorColor(YELLOW);
					printf("%s"," o");
					setCursorColor(DEFAULT_COLOR);
				}
				else {
					printf("%s"," .");
				}
			}
		}
	}

	// buttons here
	setCursorPosition(40,10);
	printf("%s",empty_space);
	setCursorPosition(40,10);
	printf("Hints are %s.", showHints?"on":"off");
	setCursorPosition(61,10);
	draw_button(showHints?"TURN OFF":"TURN ON", highlightX==64, clickEffectX==64);

	setCursorPosition(40,12);
	printf("%s",empty_space);
	setCursorPosition(40,12);
	printf("Moves are %s.", showLegalMoves?"shown":"not shown");
	setCursorPosition(61,12);
	draw_button(showLegalMoves?"HIDE":"SHOW", highlightX==65, clickEffectX==65);
	
	setCursorPosition(40,14);
	printf("%s",empty_space);
	setCursorPosition(40,14);
	printf("%s",gameOver?gameOverReasons[gameOverReason]:"Game is on.");
	setCursorPosition(61,14);
	draw_button(gameOver?"BACK TO MAIN MENU":"RESIGN", highlightX==66, clickEffectX==66);
	
	// setCursorPosition(61,16);
	// draw_button("REFRESH SCREEN", highlightX==67, clickEffectX==67);

	int hx = highlightX%8, hy = highlightX/8;
	if (clickEffectX!=highlightX && highlightX<64) {
		setCursorStyle(DEFAULT_STYLE);
		setCursorStyle(BOLD);
		if (blinks) setCursorColor(LIGHTBLUE);
		else setCursorColor(BLUE);
		setCursorPosition(4+4*hx, 6+2*hy);
		printf("%s","/\n\b\\");
		setCursorPosition(8+4*hx, 6+2*hy);
		printf("%s","\\\n\b/");
	}

	// draw_input(myName, NAME_MAXLEN, highlightX==currentX++, "John");

}





void draw_input(const char* buffer, const int bufLen, const bool highlighted, const char* placeholder) {
	if (highlighted) {
		setCursorStyle(BOLD);
		if (blinks) setCursorColor(LIGHTBLUE);
		else setCursorColor(BLUE);
	}
	printf("%s","[");

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
		printf("%s","]");
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
	printf("%s"," \n\b ");
	setCursorPosition(8+4*hx, 6+2*hy);
	printf("%s"," \n\b ");
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
	myTurn = !blinks; // black first
	if (!myTurn) botCountdown = BOT_THINKING_TIME;
	gameOver = false;
	opponentIsBot = true;
	strcpy(opponentName, "Zebro");
}

void prepareGameWithHuman(char* name, enum Player side, int sd) {
	view = 1;
	highlightX = 0;
	connectingX = -1;
	memset(myBubble,'\0',MSG_MAXLEN);
	myBubbleX = 0;
	memset(opponentBubble,'\0',MSG_MAXLEN);
	copyBoard(startingPositionBoard, board);
	copyBoard(startingPositionBoard, prevBoard);
	mySide = side;
	myTurn = side == BLACK_PLAYER;
	currentlyPlayingWith_sd = sd;
	for (int i = 0; i<gameRequestsX; i++) {
		if ( gameRequests[i].sd != sd) close( gameRequests[i].sd );
	}
	gameRequestsX = 0;
	gameOver = false;
	opponentIsBot = false;
	strcpy(opponentName, name);
}


bool tryToMakeAMove(struct Move m, enum Player side) {
	if (gameOver) return false;
	if (side==mySide && !myTurn) return false;
	if (side!=mySide && myTurn) return false;

	if (m.x < 0 || m.x >= 8 || m.y < 0 || m.y >= 8) return false;

	if (moveIsLegal(board,m,side)) {
		copyBoard(board,prevBoard);
		registerMove(board,m,side);

		transitionCountdown = TRANSITION_TIME;
		doAftermoveChecks();
		return true;
	}
	return false;
}


void doAftermoveChecks(void) {
	enum Player justPlayedSide, toPlaySide;
	justPlayedSide = myTurn ? mySide : !mySide;
	toPlaySide = !justPlayedSide;

	bool justPlayedStuck = playerMustSkip(board,justPlayedSide);
	bool toPlayStuck = playerMustSkip(board,toPlaySide);
	if (justPlayedStuck && toPlayStuck) {
		// gameover
		gameOver = true;
		int myPts = countDiscs(board, mySide);
		int hisPts = countDiscs(board, !mySide);
		if (myPts > hisPts) gameOverReason = 0; // you won
		else if (myPts < hisPts) gameOverReason = 1; // you lost
		else gameOverReason = 2; // you tied

		botCountdown = -1;
		return;
	}

	if (toPlaySide!=mySide && opponentIsBot && !toPlayStuck) {
		botCountdown = BOT_THINKING_TIME;
	}
	else {
		botCountdown = -1;
	}

	if (!toPlayStuck) {
		myTurn = !myTurn;
	}
}