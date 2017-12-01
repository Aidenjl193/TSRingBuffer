#pragma once
#include<windows.h>
#include<atomic>
#include <mutex>

//A fast implementation of a threadsafe FIFO ringbuffer, takes advantage of virtual memory mirroring


template< class type >
class TSRingBuffer {
	typedef type				new_t(void);

public:

	char						*buffer;
	__int32						length;
	std::atomic<__int32>		read;
	std::atomic<__int32>		write;
	__int32						granularity;
	std::mutex					depositMtx;
	std::mutex					readMtx;

	TSRingBuffer();

	bool						InitializeBuffer(__int32 size);
	type						Get();
	type						MultiGet();																//A thread-safe function to allow multiple readers
	bool						Deposit(type *element);
	bool						MultiDeposit(type *element);											//A thread-safe function to allow multiple writers

private:

};

template< class type >
TSRingBuffer<type>::TSRingBuffer() {
	granularity = sizeof(type);
	read = 0;
	write = 0;
}

template< class type >
bool TSRingBuffer<type>::InitializeBuffer(__int32 size) {
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	DWORD pageSize = si.dwAllocationGranularity;

	length = size + pageSize - (size % pageSize);														//Round up according to the page granularity

	size_t allocSize = length * 2;

	void *ptr = VirtualAlloc(0, allocSize, MEM_RESERVE, PAGE_NOACCESS);									//Reserve possible memory & hope we can claim it
	VirtualFree(ptr, 0, MEM_RELEASE);

	HANDLE hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, length, 0);			//Create & mirror the map
	void *pBuf = (void*)MapViewOfFileEx(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, length, ptr);
	MapViewOfFileEx(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, length, (char*)ptr + length);

	buffer = (char*)pBuf;

	return true;
}

template< class type >
bool TSRingBuffer<type>::Deposit(type *element) {
	if ((write < read && write + granularity > read) || write + granularity - length > read)			//Make sure we don't 'lap' the read pointer, this would cause undefined behavior
		return false;

	memcpy(buffer + write, element, granularity);
	write += granularity;

	write -= length * (write > length);																	//If we enter the mirrored buffer, loop back into the first
	return true;
}

template< class type >
bool TSRingBuffer<type>::MultiDeposit(type *element) {
	depositMtx.lock();
	if ((write < read && write + granularity > read) || write + granularity - length > read) {			//Make sure we don't 'lap' the read pointer, this would cause undefined behavior
		depositMtx.unlock();
		return false;
	}

	memcpy(buffer + write, element, granularity);
	write += granularity;

	write -= length * (write > length);																	//If we enter the mirrored buffer, loop back into the first
	depositMtx.unlock();
	return true;
}

template< class type >
type TSRingBuffer<type>::Get() {
	type t = type();

	if (read == write)																					//Determine if there's anything to read
		return t;

	void *ptr = &t;
	memcpy(ptr, buffer + read, granularity);
	read += granularity;

	read -= length * (read > length);																	//If we enter the mirrored buffer, loop back into the first

	return t;
}

template< class type >
type TSRingBuffer<type>::MultiGet() {
	readMtx.lock();
	type t = type();

	if (read == write) {																				//Determine if there's anything to read
		readMtx.unlock();
		return t;
	}

	void *ptr = &t;
	memcpy(ptr, buffer + read, granularity);
	read += granularity;

	read -= length * (read > length);																	//If we enter the mirrored buffer, loop back into the first
	readMtx.unlock();
	return t;
}