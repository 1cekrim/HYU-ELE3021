#ifndef __LINKED_LIST_H__
#define __LINKED_LIST_H__

#include "defs.h"

#define container_of(ptr, type, member) \
  ((type *)((char *)(ptr) - ((size_t) &((type *)0)->member)))

struct linked_list
{
  struct linked_list* prev;
  struct linked_list* next;
};

void
linked_list_init(struct linked_list* ll)
{
  ll->prev = ll;
  ll->next = ll;
}

void
linked_list_insert(struct linked_list* node, struct linked_list* prev,
                   struct linked_list* next)
{
  node->next = next;
  node->prev = prev;
  prev->next = node;
  next->prev = node;
}

void
linked_list_push_front(struct linked_list* node, struct linked_list* head)
{
    if (node->next != node || node->prev != node)
    {
        panic("llpf: invalid node");
    }
    linked_list_insert(node, head, head->next);
}

void
linked_list_push_back(struct linked_list* node, struct linked_list* head)
{
    if (node->next != node || node->prev != node)
    {
        panic("llpb: invalid node");
    }
    linked_list_insert(node, head->prev, head);
}

void
linked_list_remove(struct linked_list* node)
{
    node->next->prev = node->prev;
    node->prev->next = node->next;
    node->next = 0;
    node->prev = 0;
}

#endif /* __LINKED_LIST_H__ */