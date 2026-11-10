#ifndef SHM_H
#define SHM_H

#include <stddef.h>

// разделяемая память POSIX (shm_open + mmap)

// создать объект (старый с тем же именем удаляется) и отобразить; NULL при ошибке
void* shm_create(const char* name, size_t size);
// отобразить существующий; NULL, если его нет
void* shm_attach(const char* name, size_t size);
void shm_detach(void* addr, size_t size);
void shm_remove(const char* name);

#endif
