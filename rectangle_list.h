#ifndef RECTANGLE_LIST_H
#define RECTANGLE_LIST_H

#include <windows.h>
#include <stdlib.h>

typedef struct RectangleNode {
	RECT data;
	struct RectangleNode *prev;
	struct RectangleNode *next;
} RectangleNode;

static void RectangleListFree(RectangleNode *node) {
	if (node) {
		RectangleNode *next = node->next;
		free(node);
		RectangleListFree(next);
	}
}

static RectangleNode *RectangleListInsertAfter(RectangleNode *prev, RECT data) {
	if (prev && RectangleEqual(prev->data, data)) return prev;

	// TODO: Remove first node and try inserting again if out of memory
	RectangleNode *node = malloc(sizeof(RectangleNode));
	if (!node) return prev;

	node->data = data;
	node->prev = prev;
	node->next = NULL;

	if (prev) {
		node->next = prev->next;
		prev->next = node;
	}

	return node;
}

static RectangleNode *RectangleListAdd(RectangleNode *prev, RECT data) {
	// Prevent duplicate from being added
	if (prev && RectangleEqual(prev->data, data)) return prev;

	// TODO: Remove first node and add try adding again if out of memory
	RectangleNode *node = malloc(sizeof(RectangleNode));
	if (!node) return prev;

	node->data = data;
	node->prev = prev;
	node->next = NULL;

	if (prev) {
		RectangleListFree(prev->next);
		prev->next = node;
	}

	return node;
}

static RectangleNode *RectangleListFirst(RectangleNode *node) {
	if (!node || !node->prev) return node;
	return RectangleListFirst(node->prev);
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