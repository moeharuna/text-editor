#include "map.h"
#include <stdlib.h>
struct mapNode
{
  int key;
  int value;
};


void mapInit(map *m)
{
  m->length = 0;
  m->allocated_size =10;
  m->nodes = malloc(10 * sizeof(struct mapNode));
  for(int i=0; i < m->allocated_size; ++i)
    {
      m->nodes[i].key   = 0;
      m->nodes[i].value = 0;
    }
}

int mapAddValue(map *m, int key, int value)
{
  if(m==NULL) return 0;
  for (int i=0; i<m->length; ++i)
    {
      if(m->nodes[i].key == key) {
	m->nodes[i].value = value;
      }
    }
  if(m->allocated_size==m->length)
    {
      m->nodes = realloc(m->nodes, sizeof(struct mapNode)*m->allocated_size*2);
      m->allocated_size*=2;
    }
  m->nodes[m->length].key = key;
  m->nodes[m->length++].value = value;
  return 1;
}

int mapGetValue(map *m, int key)
{
  if(m==NULL) return 0;
  for(int i=0; i< m->length; ++i)
    {
      if(m->nodes[i].key && m->nodes[i].key == key)
	return m->nodes[i].value;
    }
  return 0;
}
