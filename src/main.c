#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>

#ifndef TAB_SIZE
#define TAB_SIZE 8
#endif

#ifndef QUIT_TIMES
#define QUIT_TIMES 3
#endif

#define CTRL_KEY(k) ((k)&0x1f)

typedef struct termios termios;

enum editorCommands {
  BACKSPACE = 127,
  MOVE_UP = 1000,
  MOVE_DOWN,
  MOVE_LEFT,
  MOVE_RIGHT,
  PAGE_UP,
  PAGE_DOWN,
  LINE_START,
  LINE_END,
  DELETE,
  QUIT  
};

typedef struct erow {
  int size;
  int rsize;
  char * chars;
  char * render;
} erow;

typedef struct editorState {
  int cx, cy;
  int rx;
  int rowoff, coloff;
  int screen_height;
  int screen_width;
  termios orig_termios;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
}editorState;
editorState State;

void editorSetStatusMessage(const char *fmt, ...);
void editorRefershScreen();
char *editorPrompt(char *prompt);


void fatal_error(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4); //clearScreen
  write(STDOUT_FILENO, "\x1b[H", 3);   // move to (0,0)
  perror(s);
  exit(1);
}

/*** Terminal ***/
void restoreTerminalMode(void)
{  
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &State.orig_termios) == -1)
    fatal_error("restoreTerminalMode");
}

void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &State.orig_termios) == -1)
    fatal_error("enabeRawMode/getattr");

  atexit(restoreTerminalMode);
  
  termios mode = State.orig_termios;
  mode.c_lflag &= ~( ECHO | ICANON | ISIG | IEXTEN);
  mode.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  mode.c_oflag &= ~(OPOST);
  mode.c_cflag |= (CS8);
  mode.c_cc[VMIN] = 0;
  mode.c_cc[VTIME]  = 1;
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &mode) ==-1)
    fatal_error("enabeRawMode/setattr");
}

int editorReadKey()
{
  int nread;
  char c;
  while((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
      if(nread == -1 && errno != EAGAIN) fatal_error("read");
    }
  if( c== CTRL_KEY('q')) return QUIT;
  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {

      if (seq[1] >= '0' &&  seq[1] <= '9') {
	if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
	if(seq[2] == '~') {
	  switch (seq[1]) {
	  case '1': return LINE_START;
	  case '4': return LINE_END;
	  case '3': return DELETE;
	  case '5' : return PAGE_UP;
	  case '6': return PAGE_DOWN;

	  case '7': return LINE_START;
	  case '8': return LINE_END;
	  }
	}
      } else {
      
	switch (seq[1]) {
	case 'A': return MOVE_UP;
	case 'B': return MOVE_DOWN;
	case 'C': return MOVE_RIGHT;
	case 'D': return MOVE_LEFT;
	case 'H': return LINE_START;
	case 'F': return LINE_END;
	}
      }
    }
    return '\x1b';
  } 
  return c;
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while(i < sizeof(buf) - 1) {
    if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if(buf[i] == 'R') {break;}
    i++;
  }
  buf[i] = '\0';

  if(buf[0] != '\x1b' || buf[1] != '[') return -1;
  if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col ==0) {
     if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
     return getCursorPosition(rows, cols);
  } else {
    *cols =  ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}


/*** row operations ***/

int editorRowCxToRx(erow *row, int cx)
{
  int rx =0;
  for (int i=0; i< cx; ++i)
    {
      if(row->chars[i] == '\t')
	rx += (TAB_SIZE -1) - (rx % TAB_SIZE);
      rx++;
    }
  return rx;
}
void editorUpdateRow(erow *row)
{
  int tabs = 0;
  for(int i = 0; i < row->size; ++i)
    if (row->chars[i] == '\t') tabs++;
  
  free(row->render);
  row->render = malloc(row->size+tabs*(TAB_SIZE-1) + 1);

  int idx = 0;
  for (int i=0; i< row->size; ++i){
    if (row->chars[i] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_SIZE != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[i];
    }
  } 
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > State.numrows) return;

  State.row = realloc(State.row, sizeof(erow) + (State.numrows +1));
  memmove(&State.row[at+1], &State.row[at], sizeof(erow) + (State.numrows - at));
  
  State.row = realloc(State.row, sizeof(erow) * (State.numrows +1));

  State.row[at].size = len;
  State.row[at].chars = malloc(len+1);
  memcpy(State.row[at].chars, s, len);
  State.row[at].chars[len] = '\0';

  State.row[at].rsize =0;
  State.row[at].render = NULL;
  editorUpdateRow(&State.row[at]);

  State.numrows++;
  State.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= State.numrows) return;
  editorFreeRow(&State.row[at]);
  memmove(&State.row[at], &State.row[at+1], sizeof(erow) * (State.numrows - at - 1));
  State.numrows--;
  State.dirty++;
}

void editorRowInsertChar(erow * row, int at, int c) {
  if (at < 0 || at > row-> size) at = row-> size;
  row->chars = realloc(row->chars, row->size + 2); //+2 for \0
  memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
}

void editorRowAppendString(erow *row, char*s, size_t len) {
  row->chars = realloc(row->chars, row->size + len +1);
  memcpy(&row->chars[row->size], s, len);
  row->size +=len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  State.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at+1], row->size-at);
  row->size--;
  editorUpdateRow(row);
  State.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
  if (State.cy == State.numrows) { //if on the EOF
    editorInsertRow(State.numrows,"", 0);
  }
  editorRowInsertChar(&State.row[State.cy], State.cx, c);
  State.cx++;
  State.dirty++;
}

void editorInsertNewline() {
  if (State.cx==0) {
    editorInsertRow(State.cy, "", 0);
  } else {
    erow *row = &State.row[State.cy];
    editorInsertRow(State.cy+1, &row->chars[State.cx], row->size - State.cx);
    row = &State.row[State.cy];
    row->size = State.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  State.cy++;
  State.cx=0;
}

void editorDelChar() {
  if(State.cy == State.numrows) return;

  erow *row = &State.row[State.cy];
  if (State.cx > 0) {
    editorRowDelChar(row, State.cx -1);
    State.cx--;
  } else {
    State.cx = State.row[State.cy -1].size;
    editorRowAppendString(&State.row[State.cy-1], row->chars, row->size);
    editorDelRow(State.cy);
    State.cy--;
  }
}
/*** file io ***/

char *editorRowsToString(int *buflen) {
  int totlen =0;
  int j;
  for (j=0; j < State.numrows; ++j)
    totlen += State.row[j].size +1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j =0; j <State.numrows; ++j) {
    memcpy(p, State.row[j].chars, State.row[j].size);
    p += State.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void editorOpen(char *filename)
{
  free(State.filename);
  State.filename = strdup(filename);
  FILE *fp = fopen(filename, "r");
  
  if(!fp) fatal_error("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen >0 && (line[linelen-1] == '\n' ||
			  line[linelen-1] == '\r'))
      linelen--;
    editorInsertRow(State.numrows, line,linelen);
  }
  free(line);
  fclose(fp);
  State.dirty = 0;
}

void editorSave() { // TODO: instead of overwriting data create new file check for erorrs and overwrite after that
  if(State.filename == NULL) {
    State.filename = editorPrompt("Save as: %s");
    if (State.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char * buf = editorRowsToString(&len);
  int fd = open(State.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
      if(ftruncate(fd, len) != -1) {
	if (write(fd, buf, len) == len) {
	  close(fd);
	  free(buf);
	  State.dirty = 0;
	  editorSetStatusMessage("%d bytes writter to disc", len);	  
	  return;
	}
      }
      close(fd);
    }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** appendBuffer ***/

typedef struct abuf {
  char *buf;
  int len;
} abuf;

#define ABUF_INIT                                                              \
  { NULL, 0 }

void abAppend(abuf *ab, const char *s, int len) {
  char * new = realloc(ab->buf, ab->len+len);

  if (new==NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->buf = new;
  ab->len +=len;
}

void abFree(abuf *ab) {
  free(ab->buf);
}

/*** input***/

char *editorPrompt(char *prompt) {
  size_t buf_size = 128;
  char * buf = malloc(buf_size);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefershScreen();

    int c = editorReadKey();
    if (c== DELETE || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    }
    else if(c== '\x1b') {
      editorSetStatusMessage("");
      free(buf);
      return NULL;
    }
    if(c == '\r') {
      if (buflen != 0) {
	editorSetStatusMessage("");
	return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if(buflen== buf_size-1) {
	buf_size *=2;
	buf = realloc(buf, buf_size);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
  }
}

void editorMoveCursorBy(int direction, unsigned moveby) {
  erow *row = (State.cy >= State.numrows) ? NULL : &State.row[State.cy];

  int result = 0;
  switch (direction)
    {
    case MOVE_LEFT:
      result = State.cx - moveby;
      if(result < 0)
	result = 0;
      State.cx = result;
      break;
    case MOVE_RIGHT:
      if(!row) break;
      result = State.cx + moveby;
      if(result > row->size)
	result = row->size;
      State.cx = result;
      break;
    case MOVE_UP:
      result = State.cy - moveby;
      if(result < 0)
	result = 0;
      State.cy = result;
      break;
    case MOVE_DOWN:
      result= State.cy + moveby;
      if (result > State.numrows)
	result = State.numrows;
      State.cy = result;
      break;
      
    }
}
void editorMoveCursor(int key) {
  erow *row = (State.cy >= State.numrows) ? NULL : &State.row[State.cy];
  switch (key) {
  case MOVE_LEFT:
    if(State.cx==0 && State.cy > 0) {
      State.cy--;
      State.cx = State.row[State.cy].size;
    }
    else editorMoveCursorBy(key, 1);

    break;
  case MOVE_RIGHT:
    if(row && State.cx == row->size &&
       State.cy != State.numrows -1) {
      State.cy++;
      State.cx = 0;
    }
    else editorMoveCursorBy(key, 1);
    break;
  case MOVE_UP:
  case MOVE_DOWN:
    editorMoveCursorBy(key, 1);
    break;
  }

  row = (State.cy >= State.numrows) ? NULL : &State.row[State.cy];
  int rowlen = row ? row->size : 0;
  if(State.cx > rowlen) {
    State.cx = rowlen;
  }
}


void editorProcessKeypress() //TODO: replace key strokes with commands
{
  static int quit_times = QUIT_TIMES;
  int c = editorReadKey();
  switch (c) {
  case '\r':
    editorInsertNewline();
    break;
  case QUIT:
    if (State.dirty && quit_times > 0) {
      editorSetStatusMessage("File has unsaved changes. Press CTRL+Q %d nore times to quit", quit_times);
      quit_times--;
      return;
    }
    write(STDOUT_FILENO, "\x1b[2J", 4); //clearScreen
    write(STDOUT_FILENO, "\x1b[H", 3);   // move to (0,0)
    exit(0);
    break;
  case CTRL_KEY('s'):
    editorSave();
    break;
  case LINE_START:
    State.cx = 0;
    break;
  case LINE_END:
    if(State.cy < State.numrows)
      State.cx = State.row[State.cy].size;
    break;
  case PAGE_DOWN:
  case PAGE_UP:
    {
      
      int times = State.screen_height-1;
      editorMoveCursorBy(c== PAGE_UP ? MOVE_UP : MOVE_DOWN, times);
    }
    break;
  case MOVE_DOWN:
  case MOVE_LEFT:
  case MOVE_RIGHT:
  case MOVE_UP:
    editorMoveCursor(c);
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DELETE:
    if (c==DELETE) editorMoveCursor(MOVE_RIGHT);
    editorDelChar();
    break;
  case CTRL_KEY('l'):
  case '\x1b':
    break;
  default:
    editorInsertChar(c);
    break;
    

  }
  quit_times = QUIT_TIMES;
}

/*** output ***/
void editorScroll() {
  State.rx = 0;
  if (State.cy < State.numrows) {
    State.rx = editorRowCxToRx(&State.row[State.cy], State.cx);
  }
  if (State.rx < State.coloff) {  //left
    State.coloff = State.cx;
  }
  if (State.rx >= State.coloff + State.screen_width) { //right
    State.coloff = State.cx - State.screen_width +1;
  }
  if (State.cy < State.rowoff) { //up
    State.rowoff = State.cy;
  }
  if (State.cy >= State.rowoff + State.screen_height) { //down
    State.rowoff = State.cy - State.screen_height + 1;
  }
}


void editorDrawRows(abuf * ab)
{
  int y;
  for (y=0; y< State.screen_height; y++){
    int filerow = y + State.rowoff;
    if (filerow >= State.numrows) {
      if (State.numrows == 0 && y== State.screen_height/3) {
	char welcome[] = "Welcome to Kilo editor";
	size_t welcomelen = sizeof(welcome);
	if (welcomelen > State.screen_width) welcomelen = State.screen_width;
	int padding = (State.screen_width - welcomelen) /2;
	if (padding) {
	  abAppend(ab, "~", 1);
	  padding--;
	}
	while(padding--) abAppend(ab, " ", 1);
	abAppend(ab, welcome, welcomelen);
      } else{    
	abAppend(ab, "~", 1);
      }
    } else {
      int len= State.row[filerow].rsize - State.coloff;
      if(len < 0) len =0;
      if (len > State.screen_width) len = State.screen_width;
      abAppend(ab, &State.row[filerow].render[State.coloff], len);
    }
    abAppend(ab, "\x1b[K", 3); //clear line
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(abuf *ab)
{
  abAppend(ab, "\x1b[7m", 4);
  char status[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines (%d/%d) %s",
		     State.filename ? State.filename : "[No name]",
		     State.numrows, State.cy+1, State.numrows,
		     State.dirty ? "(modified)" : "");
  if(len > State.screen_width) len= State.screen_width;
  abAppend(ab, status, len);
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(abuf *ab)
{
  abAppend(ab, "\x1b[k", 3);
  int msglen = strlen(State.statusmsg);
  if (msglen > State.screen_width) msglen = State.screen_width;
  if (msglen && time(NULL) - State.statusmsg_time < 5)
    abAppend(ab, State.statusmsg, msglen);
}

void editorRefreshScreen() {
  editorScroll();
  
  abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l" , 6); // hide cursor
  abAppend(&ab, "\x1b[H", 3);   // move to (0,0)

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);
  
  char buf[32];
  int cursor_y = (State.cy - State.rowoff) +1; // + 1 beacuse VT-100 statrt from 1
  int cursor_x = (State.rx - State.coloff) +1;
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y, cursor_x);
  abAppend(&ab, buf, strlen(buf));


  abAppend(&ab, "\x1b[?25h" , 6); // show cursor
  write(STDOUT_FILENO, ab.buf, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(State.statusmsg, sizeof(State.statusmsg), fmt, ap);
  va_end(ap);
  State.statusmsg_time = time(NULL);
}

/*** init ***/
void init(void)
{
  State.cx =0;
  State.cy =0;
  State.rx = 0;
  State.numrows=0;
  State.row = NULL;
  State.rowoff = 0;
  State.coloff = 0;
  State.dirty = 0;
  State.filename = NULL;
  State.statusmsg[0] = '\0';
  State.statusmsg_time = 0;
  enableRawMode();

  if (getWindowSize(&State.screen_height, &State.screen_width) == -1) fatal_error("getWindowSize");
  State.screen_height -=2;
}

int main(int argc, char ** argv) // TODO: turn  all special sequences into enums
{
  init();
  if (argc >=2) {    
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("Help : Ctrl-S = save | Ctrl-Q = quit");
  
  while(1)
    {
      editorRefreshScreen();
      editorProcessKeypress();
    }

  return 0;
}
