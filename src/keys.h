#define CTRL_KEY(k) ((k)&0x1f)
#define ALT_KEY(k) ((k)+2000) // probably not the best way define alt 


typedef int command;
typedef int key_stroke;
enum specialKeys {   
    BACKSPACE = 127,
    ENTER = '\r',
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
    PAGE_UP,
    PAGE_DOWN,
    HOME,
    END,
    DELETE,
    INSERT,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12
};


enum editorCommands {
  MOVE_UP= 1000,
  MOVE_DOWN,
  MOVE_LEFT,
  MOVE_RIGHT,
  SCROLL_PAGE_UP,
  SCROLL_PAGE_DOWN,
  CURSOR_LINE_START,
  CURSOR_LINE_END,
  REMOVE_BACKWARD,
  REMOVE_FORWARD,
  NEWLINE,
  SAVE,
  SAVE_AS, //TODO
  NOTHING,
  QUIT
};


