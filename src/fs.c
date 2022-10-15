#include "fs.h"

#include "event.h"
#include "heap.h"
#include "queue.h"
#include "thread.h"
#include "lz4/lz4.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct fs_t
{
	heap_t* heap;
	queue_t* file_queue;
	queue_t* comp_decomp_queue; // queue for compression and decompression
	thread_t* file_thread;
	thread_t* comp_decomp_thread; // thread for compression and decompression
} fs_t;

typedef enum fs_work_op_t
{
	k_fs_work_op_read,
	k_fs_work_op_write,
} fs_work_op_t;

typedef struct fs_work_t
{
	heap_t* heap;
	fs_work_op_t op;
	char path[1024];
	bool null_terminate;
	bool use_compression;
	void* buffer;
	size_t size;
	event_t* done;
	int result;
} fs_work_t;

static int file_thread_func(void* user);
static int comp_decomp_thread_func(void* user);

fs_t* fs_create(heap_t* heap, int queue_capacity)
{
	fs_t* fs = heap_alloc(heap, sizeof(fs_t), 8);
	fs->heap = heap;
	fs->file_queue = queue_create(heap, queue_capacity);
	fs->comp_decomp_queue = queue_create(heap, queue_capacity); // creates compression and decompression queue
	fs->file_thread = thread_create(file_thread_func, fs);
	fs->comp_decomp_thread = thread_create(comp_decomp_thread_func, fs); // creates compression and decompression thread
	return fs;
}

void fs_destroy(fs_t* fs)
{
	queue_push(fs->file_queue, NULL);
	queue_push(fs->comp_decomp_queue, NULL); // To break out of compression and decompression func loop
	thread_destroy(fs->file_thread);
	thread_destroy(fs->comp_decomp_thread); // destroys compression and decompression thread
	queue_destroy(fs->file_queue);
	queue_destroy(fs->comp_decomp_queue); // destroys compression and decompression queue
	heap_free(fs->heap, fs);
}

fs_work_t* fs_read(fs_t* fs, const char* path, heap_t* heap, bool null_terminate, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = heap;
	work->op = k_fs_work_op_read;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = NULL;
	work->size = 0;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = null_terminate;
	work->use_compression = use_compression;
	queue_push(fs->file_queue, work);
	return work;
}

fs_work_t* fs_write(fs_t* fs, const char* path, const void* buffer, size_t size, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = fs->heap;
	work->op = k_fs_work_op_write;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = (void*)buffer;
	work->size = size;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = false;
	work->use_compression = use_compression;

	if (use_compression)
	{
		// HOMEWORK 2: Queue file write work on compression queue!
		queue_push(fs->comp_decomp_queue, work);
	}
	else
	{
		queue_push(fs->file_queue, work);
	}

	return work;
}

bool fs_work_is_done(fs_work_t* work)
{
	return work ? event_is_raised(work->done) : true;
}

void fs_work_wait(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
	}
}

int fs_work_get_result(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->result : -1;
}

void* fs_work_get_buffer(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->buffer : NULL;
}

size_t fs_work_get_size(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->size : 0;
}

void fs_work_destroy(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
		event_destroy(work->done);
		heap_free(work->heap, work);
	}
}

static void file_read(fs_work_t* work, fs_t* fs) // added arg for pushing onto the queue
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		event_signal(work->done);
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		event_signal(work->done);
		return;
	}

	if (!GetFileSizeEx(handle, (PLARGE_INTEGER)&work->size))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->buffer = heap_alloc(work->heap, work->null_terminate ? work->size + 1 : work->size, 8);

	DWORD bytes_read = 0;
	if (!ReadFile(handle, work->buffer, (DWORD)work->size, &bytes_read, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->size = bytes_read;
	if (work->null_terminate)
	{
		((char*)work->buffer)[bytes_read] = 0;
	}

	CloseHandle(handle);

	if (work->use_compression)
	{
		// HOMEWORK 2: Queue file read work on decompression queue!
		queue_push(fs->comp_decomp_queue, work);
	}
	else
	{
		event_signal(work->done);
	}
}

int get_hash(void* address, int bucket_count)
{
	return (intptr_t)address % bucket_count;
}


static void file_write(fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		event_signal(work->done);
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		event_signal(work->done);
		return;
	}

	DWORD bytes_written = 0;
	if (!WriteFile(handle, work->buffer, (DWORD)work->size, &bytes_written, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->size = bytes_written;

	CloseHandle(handle);

	event_signal(work->done);
}

static int file_thread_func(void* user)
{
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->file_queue);
		if (work == NULL)
		{
			break;
		}
		
		switch (work->op)
		{
		case k_fs_work_op_read:
			file_read(work, fs); // added arg for decompression
			break;
		case k_fs_work_op_write:
			file_write(work);
			break;
		}
	}
	return 0;
}

static int comp_decomp_thread_func(void* user)
{
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->comp_decomp_queue);
		if (work == NULL)
		{
			break;
		}

		switch (work->op)
		{
		case k_fs_work_op_read:
			{
				int buffer_size = 1000000; // size of decompressed memory allocation
				void* data = heap_alloc(fs->heap, buffer_size, 8); // allocate memory
				int decompressed_size = LZ4_decompress_safe((char*)work->buffer, data, (int)work->size, buffer_size); // decompress the file
				heap_free(fs->heap, work->buffer); // free previous memory
				work->buffer = data; // set buffer to decompressed file
				work->size = decompressed_size; // setting the size to the decompressed size
				if (work->null_terminate) {
					((char*)work->buffer)[work->size] = '\0'; // if null terminate is true
				}
				event_signal(work->done); // Work is done
				break;
			}
		case k_fs_work_op_write:
			{
				int buffer_size = LZ4_compressBound((int)work->size); // size of compressed file
				void* data = heap_alloc(fs->heap, buffer_size, 8); // allocate memory for compressed file 
				int compressed_size = LZ4_compress_default(work->buffer, data, (int)work->size, buffer_size); // compressing the file
				work->buffer = data; // setting new data
				work->size = compressed_size; // setting new size to compressed size
				queue_push(fs->file_queue, work); // pushing compressed file to file queue
				break;
			}
		}
	}
	return 0;
}

/*
function to compress and decompress data (it happens in the function)
push on the compression queue

- For compress
lz4_compress_bound(work->size)
heapAlloc(compressBound)
lz4_compress_default(work->buffer, heapAlloc, work->size, compressBound);
work->buffer = compressed version
work->size = compressed version
push to file queue

- For decompress
lz4_compress_bound(work->size)
heapAlloc(compressBound)
Lz4_decompress_safe(work->buffer, heapAlloc, work->size, compressBound);
work->buffer = compressed version
work->size = compressed version
eventSignal(done



*/ 






