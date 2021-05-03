
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"



#define E EMPTY_CELL
#define O WHITE_CELL
#define I BLACK_CELL

// STD OPENING
// enum Cell board[8][8] = {
// 	{E,E,E,E,E,E,E,E},
// 	{E,E,E,E,E,E,E,E},
// 	{E,E,E,E,E,E,E,E},
// 	{E,E,E,O,I,E,E,E},
// 	{E,E,E,I,O,E,E,E},
// 	{E,E,E,E,E,E,E,E},
// 	{E,E,E,E,E,E,E,E},
// 	{E,E,E,E,E,E,E,E},
// };

// RANDOM MIDGAME
enum Cell board[8][8] = {
	{E,E,E,E,E,E,E,E},
	{E,E,I,E,E,E,E,E},
	{E,E,I,O,E,O,E,E},
	{E,E,E,O,I,E,E,E},
	{E,E,E,I,O,O,E,E},
	{E,E,E,E,I,E,I,E},
	{E,E,E,E,E,O,O,E},
	{E,E,E,E,E,E,E,E},
};

// FINISHED GAME
// enum Cell board[8][8] = {
// 	{I,O,O,O,O,O,O,O},
// 	{I,I,O,O,O,O,O,O},
// 	{I,I,I,O,O,O,O,O},
// 	{I,I,O,I,O,O,O,O},
// 	{E,I,I,I,I,O,O,I},
// 	{O,O,I,O,I,O,O,I},
// 	{O,O,O,I,I,O,O,I},
// 	{O,O,O,O,I,O,O,O},
// };

// 1 O O O O O O O 
// 1 1 O O O O O O 
// 1 1 1 O O O O O 
// 1 1 O 1 O O O O 
// ∘ 1 1 1 1 O O 1 
// O O 1 O 1 O O 1 
// O O O 1 1 O O 1 
// O O O O 1 O O O 


enum Cell prevBoard[8][8] = {
	{E,E,E,E,E,E,E,E},
	{E,E,E,E,E,E,E,E},
	{E,E,E,E,E,E,E,E},
	{E,E,E,O,I,E,E,E},
	{E,E,E,I,O,E,E,E},
	{E,E,E,E,E,E,E,E},
	{E,E,E,E,E,E,E,E},
	{E,E,E,E,E,E,E,E},
};

enum Cell startingPositionBoard[8][8] = {
	{E,E,E,E,E,E,E,E},
	{E,E,E,E,E,E,E,E},
	{E,E,E,E,E,E,E,E},
	{E,E,E,O,I,E,E,E},
	{E,E,E,I,O,E,E,E},
	{E,E,E,E,E,E,E,E},
	{E,E,E,E,E,E,E,E},
	{E,E,E,E,E,E,E,E},
};



void draw_( enum Cell b[8][8], enum Player p ) {
	printf("\nBoard:\n");
	for (int y = 0; y<8; y++) {
		for (int x = 0; x<8; x++) {
			if ( b[y][x] == EMPTY_CELL ) {
				if ( moveIsLegal(b,(struct Move){x,y},p) ) {
					printf("∘ ");
				}
				else {
					printf("· ");

				}
			}
			else if ( b[y][x] == WHITE_CELL ) {
				printf("O ");
			}
			else if ( b[y][x] == BLACK_CELL ) {
				printf("1 ");
			}
		}
		puts("");
	}
	puts("");
}


// doesn't assume move is to an empty cell. Will simply give false, if cell is full.
// assumes moves are (0 <= x < 8) and (0 <= y < 8)
bool moveIsLegal( enum Cell b[8][8], struct Move m, enum Player p ) {
	
	const int x = m.x;
	const int y = m.y;
	if (b[y][x] != EMPTY_CELL) return false;

	const enum Cell OWN_CELL = (p == WHITE_PLAYER) ? WHITE_CELL : BLACK_CELL;
	const enum Cell ENEMY_CELL = (p == WHITE_PLAYER) ? BLACK_CELL : WHITE_CELL;

	enum SearchState {SEARCHING_ENEMY_DISK, SEARCHING_OWN_DISK };
	enum SearchState state = SEARCHING_ENEMY_DISK;

	

	#define LEGAL_MOVE_CHECKER(x,y)				\
	{											\
		if (b[y][x]==EMPTY_CELL) break;			\
		if (state==SEARCHING_ENEMY_DISK) {		\
			if (b[y][x]==OWN_CELL) break;		\
			else state = SEARCHING_OWN_DISK;	\
		}										\
		else if (state==SEARCHING_OWN_DISK) {	\
			if (b[y][x]==ENEMY_CELL) continue;	\
			else return true;					\
		}										\
	}											\
	state = SEARCHING_ENEMY_DISK;				\

	// up, left, down, right
	for (int cy=y-1; cy>=0; cy--) LEGAL_MOVE_CHECKER(x,cy)
	for (int cx=x-1; cx>=0; cx--) LEGAL_MOVE_CHECKER(cx,y)
	for (int cy=y+1; cy<=7; cy++) LEGAL_MOVE_CHECKER(x,cy)
	for (int cx=x+1; cx<=7; cx++) LEGAL_MOVE_CHECKER(cx,y)

	// up-left, up-right, down-left, down-right
	for (int cy=y-1, cx=x-1; cy>=0 && cx>=0; cy--, cx--) LEGAL_MOVE_CHECKER(cx,cy)
	for (int cy=y-1, cx=x+1; cy>=0 && cx<=7; cy--, cx++) LEGAL_MOVE_CHECKER(cx,cy)
	for (int cy=y+1, cx=x-1; cy<=7 && cx>=0; cy++, cx--) LEGAL_MOVE_CHECKER(cx,cy)
	for (int cy=y+1, cx=x+1; cy<=7 && cx<=7; cy++, cx++) LEGAL_MOVE_CHECKER(cx,cy)

	#undef LEGAL_MOVE_CHECKER
	return false;
}

// moves in-place.
void registerMove( enum Cell b[8][8], struct Move m, enum Player p ) {
	
	const int x = m.x;
	const int y = m.y;

	const enum Cell OWN_CELL = (p == WHITE_PLAYER) ? WHITE_CELL : BLACK_CELL;
	const enum Cell ENEMY_CELL = (p == WHITE_PLAYER) ? BLACK_CELL : WHITE_CELL;
	
	b[y][x] = OWN_CELL;

	enum SearchState {SEARCHING_ENEMY_DISK, SEARCHING_OWN_DISK, FILLING_DISKS };
	enum SearchState state = SEARCHING_ENEMY_DISK;

	#define LEGAL_MOVE_CHECKER(x,y,CODE)		\
	{											\
		if (b[y][x]==EMPTY_CELL) break;			\
		if (state==FILLING_DISKS) {				\
			if (b[y][x]==ENEMY_CELL) {			\
				b[y][x] = OWN_CELL;				\
			}									\
			else break;							\
		}										\
		else if (state==SEARCHING_ENEMY_DISK) {	\
			if (b[y][x]==OWN_CELL) break;		\
			else state = SEARCHING_OWN_DISK;	\
		}										\
		else if (state==SEARCHING_OWN_DISK) {	\
			if (b[y][x]==ENEMY_CELL) continue;	\
			else {								\
				state = FILLING_DISKS;			\
				CODE							\
			}									\
		}										\
	}											\
	state = SEARCHING_ENEMY_DISK;				\

	// up, left, down, right
	for (int cy=y-1; cy>=0; cy--) LEGAL_MOVE_CHECKER(x,cy,cy=y;)
	for (int cx=x-1; cx>=0; cx--) LEGAL_MOVE_CHECKER(cx,y,cx=x;)
	for (int cy=y+1; cy<=7; cy++) LEGAL_MOVE_CHECKER(x,cy,cy=y;)
	for (int cx=x+1; cx<=7; cx++) LEGAL_MOVE_CHECKER(cx,y,cx=x;)

	// up-left, up-right, down-left, down-right
	for (int cy=y-1, cx=x-1; cy>=0 && cx>=0; cy--, cx--) LEGAL_MOVE_CHECKER(cx,cy,cy=y;cx=x;)
	for (int cy=y-1, cx=x+1; cy>=0 && cx<=7; cy--, cx++) LEGAL_MOVE_CHECKER(cx,cy,cy=y;cx=x;)
	for (int cy=y+1, cx=x-1; cy<=7 && cx>=0; cy++, cx--) LEGAL_MOVE_CHECKER(cx,cy,cy=y;cx=x;)
	for (int cy=y+1, cx=x+1; cy<=7 && cx<=7; cy++, cx++) LEGAL_MOVE_CHECKER(cx,cy,cy=y;cx=x;)
	
	
	#undef LEGAL_MOVE_CHECKER
	return;
}

bool playerMustSkip( enum Cell b[8][8], enum Player p ) {
	for (int y=0; y<8; y++) {
		for (int x=0; x<8; x++) {
			if ( moveIsLegal(b, (struct Move) {x,y}, p) ) return false;
		}
	}
	return true;
}





struct Move hintByZebro ( enum Cell b[8][8], enum Player p ) {

	struct Move best = {0,0};
	int bestScore = 0;
	int score = 0;

	for (int y=0; y<8; y++) {
		for (int x=0; x<8; x++) {

			score = 0;
			if (b[y][x] != EMPTY_CELL) continue;

			const enum Cell OWN_CELL = (p == WHITE_PLAYER) ? WHITE_CELL : BLACK_CELL;
			const enum Cell ENEMY_CELL = (p == WHITE_PLAYER) ? BLACK_CELL : WHITE_CELL;

			enum SearchState {SEARCHING_ENEMY_DISK, SEARCHING_OWN_DISK };
			enum SearchState state = SEARCHING_ENEMY_DISK;


			#define LEGAL_MOVE_CHECKER(cx,cy)				\
			{												\
				if (b[cy][cx]==EMPTY_CELL) break;			\
				if (state==SEARCHING_ENEMY_DISK) {			\
					if (b[cy][cx]==OWN_CELL) break;			\
					else {									\
						state = SEARCHING_OWN_DISK;			\
					}										\
				}											\
				else if (state==SEARCHING_OWN_DISK) {		\
					if (b[cy][cx]==ENEMY_CELL) continue;	\
					else {									\
						if (y==cy) score += abs(x-cx)*3-3;	\
						else score += abs(y-cy)*3-3;		\
						break;								\
					}										\
				}											\
			}												\
			state = SEARCHING_ENEMY_DISK;					\

			// up, left, down, right
			for (int cy=y-1; cy>=0; cy--) LEGAL_MOVE_CHECKER(x,cy)
			for (int cx=x-1; cx>=0; cx--) LEGAL_MOVE_CHECKER(cx,y)
			for (int cy=y+1; cy<=7; cy++) LEGAL_MOVE_CHECKER(x,cy)
			for (int cx=x+1; cx<=7; cx++) LEGAL_MOVE_CHECKER(cx,y)

			// up-left, up-right, down-left, down-right
			for (int cy=y-1, cx=x-1; cy>=0 && cx>=0; cy--, cx--) LEGAL_MOVE_CHECKER(cx,cy)
			for (int cy=y-1, cx=x+1; cy>=0 && cx<=7; cy--, cx++) LEGAL_MOVE_CHECKER(cx,cy)
			for (int cy=y+1, cx=x-1; cy<=7 && cx>=0; cy++, cx--) LEGAL_MOVE_CHECKER(cx,cy)
			for (int cy=y+1, cx=x+1; cy<=7 && cx<=7; cy++, cx++) LEGAL_MOVE_CHECKER(cx,cy)

			#undef LEGAL_MOVE_CHECKER

			// if no score, it means it's not a legal move
			if (score == 0) continue;

			// near the corners = more score
			if (y<4) score += (3-y);
			else score += (y-4);
			if (x<4) score += (3-x);
			else score += (x-4);

			// the cell the disc is put
			score+=3;
			
			if (score>bestScore) {
				bestScore = score;
				best.x = x;
				best.y = y;
			}

		}
	}

	// printf("Zebro's best: %d%d (%d pts)\n", best.x+1, 8-best.y, bestScore);
	return best;
}


int countDiscs( enum Cell b[8][8], enum Player p) {
	int total = 0;
	const enum Cell OWN_CELL = (p == WHITE_PLAYER) ? WHITE_CELL : BLACK_CELL;
	
	for (int y = 0; y<8; y++) {
		for (int x = 0; x<8; x++) {
			if (b[y][x] == OWN_CELL) total++;
		}
	}
	return total;
}



void copyBoard( enum Cell from[8][8], enum Cell to[8][8] ) {
	memcpy(to,from, sizeof(enum Cell)*64 );
}


int main_(void) {

	printf("O stuck: %d, I stuck: %d\n", playerMustSkip(board,WHITE_PLAYER), playerMustSkip(board,BLACK_PLAYER) );
	while(false) {
		draw_(board,WHITE_PLAYER);
		int inpt;
		scanf("%d", &inpt);
		registerMove(board,(struct Move){inpt/10-1, 8-inpt%10},WHITE_PLAYER);

		draw_(board,BLACK_PLAYER);
		registerMove(board,hintByZebro(board,BLACK_PLAYER),BLACK_PLAYER);
		// sleep(2);

	}
	// registerMove(board,(struct Move){5,3},WHITE_PLAYER);

	return 0;
}