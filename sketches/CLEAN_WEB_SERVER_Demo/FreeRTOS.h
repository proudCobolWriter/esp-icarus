#ifndef TASK_SNAPSHOTS_WRAPPER_H
#define TASK_SNAPSHOTS_WRAPPER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	void *pxTCB;               // Pointer to Task Control Block (matches xHandle)
	StackType_t *pxTopOfStack; // Stack latest (top) pointer when task suspended
	StackType_t *pxEndOfStack; // Highest memory boundary of the stack allocation (basically the maximum stack pointer used since runtime)
} TaskSnapshot_t;

UBaseType_t uxTaskGetSnapshotAll(TaskSnapshot_t *const pxTaskSnapshotArray, const UBaseType_t uxArrayLength,
                                 UBaseType_t *const pxTCBSize);

#ifdef __cplusplus
}
#endif

#endif // TASK_SNAPSHOTS_WRAPPER_H