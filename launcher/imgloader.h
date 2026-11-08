#ifndef IMGLOADER_H
#define IMGLOADER_H

// Background image loading and the art cache's decoding on worker
// threads, and the selection pill animation worker. Also holds the render state those
// workers share with the main loop.

#include <stdbool.h>
#include "api.h"

enum {
	ANIM_NONE = 0,
	SLIDE_LEFT = 1,
	SLIDE_RIGHT = 2,
};

typedef void (*BackgroundLoadedCallback)(SDL_Surface* surface);

typedef struct {
    char imagePath[MAX_PATH];
    BackgroundLoadedCallback callback;
    void* userData;
} LoadBackgroundTask;

typedef struct finishedTask {
	int startX;
	int targetX;
	int startY;
	int targetY;
	int targetTextY;
	int move_y;
	int move_w;
	int move_h;
	int frames;
	int done;
	void* userData;
	  char *entry_name;
	SDL_Rect dst;
} finishedTask;

typedef void (*AnimTaskCallback)(finishedTask *task);
typedef struct AnimTask {
	int startX;
	int targetX;
	int startY;
	int targetY;
	int targetTextY;
	int move_w;
	int move_h;
	int frames;
	AnimTaskCallback callback;
	void* userData;
	 char *entry_name;
	SDL_Rect dst;
} AnimTask;

// A load for the art cache's worker (see artloader.h).
typedef struct ArtLoad {
	char path[MAX_PATH];
	int kind;           // how ArtLoader_decode decodes it
	SDL_Surface* image; // what it decoded, or NULL
	size_t bytes;       // what image takes
	struct ArtLoad* next;
} ArtLoad;

typedef struct TaskNode {
    LoadBackgroundTask* task;
    struct TaskNode* next;
} TaskNode;
typedef struct AnimTaskNode {
	AnimTask* task;
    struct AnimTaskNode* next;
} AnimTaskNode;

extern SDL_mutex* bgqueueMutex;
extern SDL_mutex* thumbqueueMutex;
extern SDL_mutex* animqueueMutex;

extern SDL_mutex* bgMutex;
extern SDL_mutex* thumbMutex;
extern SDL_mutex* animMutex;
extern SDL_mutex* frameMutex;
extern SDL_mutex* fontMutex;
extern SDL_cond* flipCond;

extern SDL_Surface* folderbgbmp;
extern SDL_Surface* thumbbmp;
extern SDL_Surface* screen; // Must be assigned externally
extern SDL_Surface* globalpill;
extern SDL_Surface* globalText;

extern int had_thumb;
extern int ox;
extern int oy;
extern SDL_atomic_t animationDrawAtomic;
extern SDL_atomic_t needDrawAtomic;

static inline void setAnimationDraw(int v) { SDL_AtomicSet(&animationDrawAtomic, v); }
static inline int getAnimationDraw(void) { return SDL_AtomicGet(&animationDrawAtomic); }
static inline void setNeedDraw(int v) { SDL_AtomicSet(&needDrawAtomic, v); }
static inline int getNeedDraw(void) { return SDL_AtomicGet(&needDrawAtomic); }

extern int folderbgchanged;
extern unsigned folderbgserial; // counts changes to folderbgbmp, under bgMutex
extern int thumbchanged;
extern int currentAnimQueueSize;

extern SDL_Rect pillRect;
extern int pilltargetY;
extern int pilltargetTextY;
extern bool frameReady;
extern bool pillanimdone;

void updatePillTextSurface(const char* entry_name, int move_w, SDL_Color text_color);
void startLoadFolderBackground(const char* imagePath, BackgroundLoadedCallback callback, void* userData);
void onBackgroundLoaded(SDL_Surface* surface);
// Hands path to the art worker, decoded as kind by ArtLoader_decode.
// Returns 0, or -1 while it still has the last one or isn't running.
int startLoadArt(const char* path, int kind);
// The loads the art worker finished since the last call, oldest first,
// each to free (and its image too, unless kept).
ArtLoad* takeLoadedArt(void);
void animPill(AnimTask *task);
void initImageLoaderPool(void);
void cleanupImageLoaderPool(void);

#endif
