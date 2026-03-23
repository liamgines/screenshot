#ifndef RECTANGLE_LIST_H
#define RECTANGLE_LIST_H

#include <windows.h>
#include <stdlib.h>
#include "rectangle.h"

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

static RectangleNode *RectangleListUndoHelper(RectangleNode *prev, RECT initialSelection) {
	if (!prev || !prev->prev || (RectangleHasArea(prev->data) && !RectangleEqual(prev->data, initialSelection))) return prev;
	return RectangleListUndoHelper(prev->prev, initialSelection);
}

static RectangleNode *RectangleListRedoHelper(RectangleNode *next, RECT initialSelection) {
	if (!next || !next->next || (RectangleHasArea(next->data) && !RectangleEqual(next->data, initialSelection))) return next;
	return RectangleListRedoHelper(next->next, initialSelection);
}

static RectangleNode *RectangleListUndo(RectangleNode *node) {
	if (node && node->prev) {
		node = RectangleListUndoHelper(node->prev, node->data);
		// Prevent current selection from being empty if possible when the first element is reached and empty
		if (!RectangleHasArea(node->data)) node = RectangleListRedoHelper(node->next, node->data);
	}
	return node;
}

static RectangleNode *RectangleListRedo(RectangleNode *node) {
	if (node && node->next) {
		node = RectangleListRedoHelper(node->next, node->data);

		if (!RectangleHasArea(node->data)) node = RectangleListUndoHelper(node->prev, node->data);
	}
	return node;
}

#endif // RECTANGLE_LIST_H