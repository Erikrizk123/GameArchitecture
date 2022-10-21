#include "trace.h"

#include "heap.h"
#include "mutex.h"
#include "fs.h"
#include "timer_object.h"
#include <windows.h>
#include <stdio.h>

#include <stddef.h>


// Struct for managing objects and important class variables
typedef struct trace_t
{
	heap_t* heap;
	fs_t* fs;
	timer_object_t* timer;
	mutex_t* mutex;
	size_t event_capacity;
	size_t occured_events;
	bool recording;
	char* path;
	struct event_t* next;
} trace_t;

// Struct for each push and pop event
typedef struct event_t
{
	heap_t* heap;
	char* name; // event name
	char* ph;
	int pid; // process Id
	int tid; // thread Id
	size_t time;
	bool popped;
	struct event_t* next;
} event_t;

trace_t* trace_create(heap_t* heap, int event_capacity)
{
	// Create Trace Struct
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->fs = fs_create(heap, event_capacity);
	trace->timer = NULL;
	trace->mutex = mutex_create();
	trace->heap = heap;
	trace->event_capacity = (size_t)event_capacity;
	trace->occured_events = 0;
	trace->recording = false;
	trace->path = NULL;
	trace->next = NULL;
	return trace;
}

void trace_destroy(trace_t* trace)
{
	// Destroy used objects
	fs_destroy(trace->fs);
	timer_object_destroy(trace->timer);
	mutex_destroy(trace->mutex);

	// Free events and trace
	if (trace->next != NULL) {
		event_t* cur_event = trace->next;
		while (cur_event != NULL) {
			heap_free(cur_event->heap, cur_event);
			cur_event = cur_event->next;
		}
	}
	heap_free(trace->heap, trace);
}

void trace_duration_push(trace_t* trace, const char* name)
{
	mutex_lock(trace->mutex);
	
	// Checks number of events
	if (trace->recording == false || trace->occured_events == trace->event_capacity) {
		return;
	}
	
	// Sets event variables
	event_t* eve = heap_alloc(trace->heap, sizeof(event_t), 8);
	// strcpy_s(eve->name, strlen(name), name);
	eve->name = _strdup(name);
	eve->ph = "B";
	eve->pid = GetCurrentProcessId();
	eve->tid = GetCurrentThreadId();
	eve->time = timer_object_get_us(trace->timer);
	eve->popped = false;
	
	// Stores event
	if (trace->next != NULL) {
		event_t* cur_event = trace->next;
		while (cur_event->next != NULL) {
			cur_event = cur_event->next;
		}
		cur_event->next = eve;
	}
	else {
		trace->next = eve;
		eve->next = NULL;
	}
	
	trace->occured_events++;
	mutex_unlock(trace->mutex);
}

void trace_duration_pop(trace_t* trace)
{
	mutex_lock(trace->mutex);

	if (trace->recording == false || trace->occured_events == trace->event_capacity || trace->next == NULL) {
		return;
	}

	// find most recent not popped event
	event_t* pop = trace->next;
	if (pop->popped == false) {
		while (pop->next != NULL && pop->next->popped == false) {
			pop = pop->next;
		}
	}
	
	// Create new event for popped
	event_t* eve = heap_alloc(trace->heap, sizeof(event_t), 8);
	// strcpy_s(eve->name, strlen(pop->name), pop->name);
	eve->name = _strdup(pop->name);
	eve->ph = "E";
	eve->pid = GetCurrentProcessId();
	eve->tid = GetCurrentThreadId();
	eve->time = timer_object_get_us(trace->timer);

	eve->popped = true;
	pop->popped = true;

	event_t* cur_event = trace->next;
	while (cur_event->next != NULL) {
		cur_event = cur_event->next;
	}
	cur_event->next = eve;
	

	trace->occured_events++;
	mutex_unlock(trace->mutex);
}

void trace_capture_start(trace_t* trace, const char* path)
{
	if (trace->recording) {
		return;
	}
	trace->recording = true;
	trace->path = _strdup(path);
	trace->timer = timer_object_create(trace->heap, NULL);
}

void trace_capture_stop(trace_t* trace)
{
	if (trace->recording == false) {
		return;
	}
	trace->recording = false;
	// printf("1\n");
	char* output = "{\n\t\"displayTimeUnit\": \"ns\", \"traceEvents\": [";
	
	char buffer[4096];
	event_t* eve = trace->next;
	while (eve != NULL) {
		snprintf(buffer, sizeof(buffer),
			"\n\t\t{\"name\":\"%s\",\"ph\":\"%s\",\"pid\":\"%d\",\"tid\":\"%d\",\"ts\":\"%d\"},",
			eve->name, eve->ph, eve->pid, eve->tid, (int)eve->time);
		printf("1.1\n");
		strcat_s(output, 4096, buffer);
		eve = eve->next;
		// printf("+");
	}
	// printf("2\n");
	char* end = "\n\t]\n}";
	strcat_s(output, 4096, end);
	// printf("3\n");
	
	fs_work_t* work = fs_write(trace->fs, trace->path, output, strlen(output), false);
	fs_work_is_done(work);

}
