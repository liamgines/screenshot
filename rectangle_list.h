#ifndef RECTANGLE_LIST_H
#define RECTANGLE_LIST_H

#include <windows.h>
#include <stdlib.h>

typedef struct RectangleNode {
	RECT data;
	struct RectangleNode *prev;
	struct RectangleNode *next;
} RectangleNode;

static void RectangleListFree(RectangleNode *xs) {
	if (xs) {
		RectangleNode *next = xs->next;
		free(xs);
		RectangleListFree(next);
	}
}

static RectangleNode *RectangleListInsertAfter(RectangleNode *prev, RECT data) {
	if (prev && RectangleEqual(prev->data, data)) return prev;

	// TODO: Remove first node and try inserting again if out of memory
	RectangleNode *selection = malloc(sizeof(RectangleNode));
	if (!selection) return prev;

	selection->data = data;
	selection->prev = prev;
	selection->next = NULL;

	if (prev) {
		selection->next = prev->next;
		prev->next = selection;
	}

	return selection;
}

static RectangleNode *RectangleListAdd(RectangleNode *prev, RECT data) {
	// Prevent duplicate from being added
	if (prev && RectangleEqual(prev->data, data)) return prev;

	// TODO: Remove first node and add try adding again if out of memory
	RectangleNode *selection = malloc(sizeof(RectangleNode));
	if (!selection) return prev;

	selection->data = data;
	selection->prev = prev;
	selection->next = NULL;

	if (prev) {
		RectangleListFree(prev->next);
		prev->next = selection;
	}

	return selection;
}

static RectangleNode *RectangleListFirst(RectangleNode *current) {
	if (!current || !current->prev) return current;
	return RectangleListFirst(current->prev);
}

static RectangleNode *RectangleListUndo(RectangleNode *prev) {
	if (!prev || !prev->prev || RectangleHasArea(prev->data)) return prev;
	return RectangleListUndo(prev->prev);
}

static RectangleNode *RectangleListRedo(RectangleNode *next) {
	if (!next || !next->next || RectangleHasArea(next->data)) return next;
	return RectangleListRedo(next->next);
}

#endif // RECTANGLE_LIST_H