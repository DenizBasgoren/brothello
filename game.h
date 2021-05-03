#ifndef GAME_H
#define GAME_H

enum Player { WHITE_PLAYER, BLACK_PLAYER };
enum Cell { EMPTY_CELL, WHITE_CELL, BLACK_CELL };

struct Move { int x; int y; };

extern enum Cell board[8][8];
extern enum Cell prevBoard[8][8];
extern enum Cell startingPositionBoard[8][8];


bool moveIsLegal( enum Cell b[8][8], struct Move m, enum Player p );
void registerMove( enum Cell b[8][8], struct Move m, enum Player p );
bool playerMustSkip( enum Cell b[8][8], enum Player p );
struct Move hintByZebro ( enum Cell b[8][8], enum Player p );
int countDiscs( enum Cell b[8][8], enum Player p);
void copyBoard( enum Cell from[8][8], enum Cell to[8][8] );

#endif // GAME_H