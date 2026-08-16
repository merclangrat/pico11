#ifndef TERMINAL_H
#define TERMINAL_H

#define KEY_UP       256
#define KEY_DOWN     257
#define KEY_LEFT     258
#define KEY_RIGHT    259
#define KEY_HOME     260
#define KEY_END      261
#define KEY_PGUP     262
#define KEY_PGDN     263
#define KEY_DELETE   264

int t_open();
int t_close();
t_size();
int t_key();
t_put();
t_write();
t_move();
int t_clear();

#endif
