#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "object_container.h"

struct object_sot {
	int index;
	struct object_sot* next;
};

struct object_container {
	void** mgr;
	int size;
	struct object_sot* slots; 
	struct object_sot* freelist;
};


struct object_container*
container_create(int max) {
	assert(max > 0);

	struct object_container* container = malloc(sizeof(*container));
	memset(container,0,sizeof(*container));

	container->size = max;
	container->mgr = malloc(sizeof(*container->mgr) * container->size);
	memset(container->mgr, 0, sizeof(*container->mgr) * container->size);

	container->slots = malloc(sizeof(*container->slots) * container->size);
	memset(container->slots, 0, sizeof(*container->slots) * container->size);
	int i;
	for(i = 0;i < container->size;i++) {
		struct object_sot* slot = &container->slots[i];
		slot->index = i;
		slot->next = container->freelist;
		container->freelist = slot;
	}

	return container;
}

void
container_release(struct object_container* container) {
	free(container->mgr);
	free(container->slots);
	free(container);
}

void*
container_get(struct object_container* container,int id) {
	void* object = container->mgr[id];
	return object;
}

void
container_foreach(struct object_container* container,foreach_func func) {
	int i;
	for(i = 0;i < container->size;i++) {
		if (container->mgr[i]) {
			func(i,container->mgr[i]);
		}
	}
}

int
container_add(struct object_container* container,void* object) {
	if (!container->freelist)
		return -1;

	struct object_sot* slot = container->freelist;
	container->freelist = slot->next;

	container->mgr[slot->index] = object;
	return slot->index;
}

void
container_remove(struct object_container* container,int id) {
	struct object_sot* slot = &container->slots[id];
	slot->next = container->freelist;
	container->freelist = slot;
	container->mgr[id] = NULL;
}