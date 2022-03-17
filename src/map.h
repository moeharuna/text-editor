typedef struct {
  int length;
  int allocated_size;
  struct mapNode *nodes;
} map;

void mapInit(map *m);
int mapAddValue(map *m, int key, int value);
int mapGetValue(map *m, int key);
