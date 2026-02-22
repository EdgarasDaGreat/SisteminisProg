#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>

// ============================================================================
// SHARED MEMORY STRUCTURES AND CONSTANTS
// ============================================================================

//Trumpiniai
#define SHARED_MEM_NAME "ProducerConsumer_SharedMem"
#define MUTEX_NAME "ProducerConsumer_Mutex"
#define SEMAPHORE_NAME "ProducerConsumer_Semaphore"
#define TASK_QUEUE_SIZE 1000


// Task structure
struct Task {
    long long number;
};

// Shared buffer - resides in shared memory
struct SharedBuffer {
    Task queue[TASK_QUEUE_SIZE];      // Array to hold 1000 tasks
    int queueStart;                   // Index where we READ from
    int queueEnd;                     // Index where we WRITE to
    int queueCount;                   // How many tasks are in queue
    
    long long minPrime;               // Smallest prime found
    long long maxPrime;               // Largest prime found
    BOOL resultsInitialized;          // Have we found any primes yet?
    
    BOOL producerDone;                // Did producer finish?
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Simple prime checking function
BOOL IsPrime(long long num) {
    if (num < 2) return FALSE;
    if (num == 2 || num == 3) return TRUE;
    if (num % 2 == 0 || num % 3 == 0) return FALSE;
    
    for (long long i = 5; i * i <= num; i += 6) {
        if (num % i == 0 || num % (i + 2) == 0) return FALSE;
    }
    return TRUE;
}

// ============================================================================
// PRODUCER PROCESS
// ============================================================================

void RunProducer() {
    printf("PRODUCER: Starting...\n");
    
    // Open shared memory
    HANDLE hSharedMem = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
    if (!hSharedMem) {
        printf("PRODUCER: Failed to open shared memory\n");
        return;
    }
    
    SharedBuffer* shared = (SharedBuffer*)MapViewOfFile(
        hSharedMem,              // Which shared memory?
        FILE_MAP_ALL_ACCESS,     // Full read/write permission
        0, 0, 0                  // Map the entire thing from start
    );
    if (!shared) {
        printf("PRODUCER: Failed to map shared memory\n");
        CloseHandle(hSharedMem);
        return;
    }
    
    // Open synchronization objects
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, MUTEX_NAME);
    HANDLE hSem = OpenSemaphoreA(SYNCHRONIZE, FALSE, SEMAPHORE_NAME);
    
    if (!hMutex || !hSem) {
        printf("PRODUCER: Failed to open synchronization objects\n");
        UnmapViewOfFile(shared);
        CloseHandle(hSharedMem);
        return;
    }
    
    // Scan rand_files directory
    WIN32_FIND_DATAA findData;
    HANDLE findHandle;
    char searchPath[MAX_PATH];
    char filePath[MAX_PATH];
    
    GetCurrentDirectoryA(MAX_PATH, searchPath);
    strcat_s(searchPath, MAX_PATH, "\\rand_files");
    strcat_s(searchPath, MAX_PATH, "\\*.txt");
    
    findHandle = FindFirstFileA(searchPath, &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        printf("PRODUCER: No files found in rand_files directory\n");
        WaitForSingleObject(hMutex, INFINITE);
        shared->producerDone = TRUE;
        ReleaseMutex(hMutex);
        ReleaseSemaphore(hSem, 1, NULL);
        UnmapViewOfFile(shared);
        CloseHandle(hSharedMem);
        CloseHandle(hMutex);
        CloseHandle(hSem);
        return;
    }
    
    int filesProcessed = 0;
    
    // Process each file
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // Build full file path
            char fullPath[MAX_PATH];
            char dirPath[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, dirPath);
            strcat_s(dirPath, MAX_PATH, "\\rand_files\\");
            strcpy_s(fullPath, MAX_PATH, dirPath);
            strcat_s(fullPath, MAX_PATH, findData.cFileName);
            
            // Open and read file
            HANDLE hFile = CreateFileA(fullPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD fileSize = GetFileSize(hFile, NULL);
                char* buffer = (char*)malloc(fileSize + 1);
                
                if (buffer) {
                    DWORD bytesRead;
                    ReadFile(hFile, buffer, fileSize, &bytesRead, NULL);
                    buffer[bytesRead] = '\0';
                    
                    // Parse numbers from file
                    long long num;
                    char* context = NULL;
                    char* token = strtok_s(buffer, " \t\n\r", &context);
                    
                    while (token) {
                        if (sscanf_s(token, "%lld", &num) == 1) {
                            // Add to queue with mutex protection - RETRY until added
                            BOOL added = FALSE;
                            while (!added) {
                                WaitForSingleObject(hMutex, INFINITE);
                                
                                if (shared->queueCount < TASK_QUEUE_SIZE) {
                                    shared->queue[shared->queueEnd].number = num;
                                    shared->queueEnd = (shared->queueEnd + 1) % TASK_QUEUE_SIZE;
                                    shared->queueCount++;
                                    
                                    ReleaseMutex(hMutex);
                                    ReleaseSemaphore(hSem, 1, NULL); // Signal task available
                                    added = TRUE;
                                } else {
                                    ReleaseMutex(hMutex);
                                    Sleep(10); // Queue full, wait and RETRY
                                }
                            }
                        }
                        token = strtok_s(NULL, " \t\n\r", &context);
                    }
                    
                    free(buffer);
                }
                CloseHandle(hFile);
                filesProcessed++;
                
                if (filesProcessed % 100 == 0) {
                    printf("PRODUCER: Processed %d files...\n", filesProcessed);
                }
            }
        }
    } while (FindNextFileA(findHandle, &findData));
    
    FindClose(findHandle);
    
    // Signal that producer is done
    WaitForSingleObject(hMutex, INFINITE);
    shared->producerDone = TRUE;
    ReleaseMutex(hMutex);
    
    // Wake up consumers
    for (int i = 0; i < 10; i++) {
        ReleaseSemaphore(hSem, 1, NULL);
    }
    
    printf("PRODUCER: Finished. Processed %d files.\n", filesProcessed);
    
    // Cleanup
    UnmapViewOfFile(shared);
    CloseHandle(hSharedMem);
    CloseHandle(hMutex);
    CloseHandle(hSem);
}

// ============================================================================
// CONSUMER PROCESS
// ============================================================================

void RunConsumer() {
    printf("CONSUMER (PID %lu): Starting...\n", GetCurrentProcessId());
    
    // Open shared memory
    HANDLE hSharedMem = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
    if (!hSharedMem) {
        printf("CONSUMER: Failed to open shared memory\n");
        return;
    }
    
    SharedBuffer* shared = (SharedBuffer*)MapViewOfFile(hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!shared) {
        printf("CONSUMER: Failed to map shared memory\n");
        CloseHandle(hSharedMem);
        return;
    }
    
    // Open synchronization objects
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, MUTEX_NAME);
    HANDLE hSem = OpenSemaphoreA(SYNCHRONIZE, FALSE, SEMAPHORE_NAME);
    
    if (!hMutex || !hSem) {
        printf("CONSUMER: Failed to open synchronization objects\n");
        UnmapViewOfFile(shared);
        CloseHandle(hSharedMem);
        return;
    }
    
    int tasksProcessed = 0;
    
    // Main consumer loop
    while (TRUE) {
        // Always check queue first, don't rely only on semaphore
        WaitForSingleObject(hMutex, INFINITE);
        
        if (shared->queueCount > 0) {
            // Get task from queue
            Task task = shared->queue[shared->queueStart];
            shared->queueStart = (shared->queueStart + 1) % TASK_QUEUE_SIZE;
            shared->queueCount--;
            
            ReleaseMutex(hMutex);
            
            // Check if prime
            if (IsPrime(task.number)) {
                WaitForSingleObject(hMutex, INFINITE);
                
                if (!shared->resultsInitialized) {
                    shared->minPrime = task.number;
                    shared->maxPrime = task.number;
                    shared->resultsInitialized = TRUE;
                } else {
                    if (task.number < shared->minPrime) {
                        shared->minPrime = task.number;
                    }
                    if (task.number > shared->maxPrime) {
                        shared->maxPrime = task.number;
                    }
                }
                
                ReleaseMutex(hMutex);
            }
            
            tasksProcessed++;
        } else {
            ReleaseMutex(hMutex);
            
            // Queue empty - check if done
            WaitForSingleObject(hMutex, INFINITE);
            BOOL done = shared->producerDone && shared->queueCount == 0;
            ReleaseMutex(hMutex);
            
            if (done) {
                printf("CONSUMER (PID %lu): Producer finished. Exiting. Processed %d tasks.\n", GetCurrentProcessId(), tasksProcessed);
                break;
            }
            
            Sleep(10);  // Brief sleep to avoid busy-waiting
        }
    }
    
    printf("CONSUMER (PID %lu): Finished.\n", GetCurrentProcessId());
    
    // Cleanup
    UnmapViewOfFile(shared);
    CloseHandle(hSharedMem);
    CloseHandle(hMutex);
    CloseHandle(hSem);
}

// ============================================================================
// MAIN CONTROLLER
// ============================================================================

int main(int argc, char* argv[]) {
    // Check if this is a worker process
    if (argc > 1) {
        if (strcmp(argv[1], "producer") == 0) {
            RunProducer();
            return 0;
        }
        if (strcmp(argv[1], "consumer") == 0) {
            RunConsumer();
            return 0;
        }
    }
    
    // Main controller
    printf("Producer-Consumer Pattern with Windows API\n");
    printf("============================================\n\n");
    
    // Create shared memory
    HANDLE hSharedMem = CreateFileMappingA(
		INVALID_HANDLE_VALUE, //Naudok atminties faila, o ne tikra faila (RAM)
		NULL, //defaultine apsauga
		PAGE_READWRITE, //Visi gali skaityti ir rasyti
        0, //kazkoks nereikalingas flagas
		sizeof(SharedBuffer), //bendro buferio dydis
		SHARED_MEM_NAME //Pavadinimas bendros atminties objektui
    );
    
    if (!hSharedMem) {
        printf("Failed to create shared memory\n");
        return 1;
    }
    
	//Padarom pointeri i ta bendro buferio atminties gabala
    SharedBuffer* shared = (SharedBuffer*)MapViewOfFile(hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!shared) {
        printf("Failed to map shared memory\n");
        CloseHandle(hSharedMem);
        return 1;
    }
    
    ZeroMemory(shared, sizeof(SharedBuffer)); //nunulina atminties gabala isvalo is praeitu
    
    
	//NULL - default apsauga, FALSE - pradzioj niekam nepriklauso, MUTEX_NAME - mutexo pavadinimas
    //kalbejimo lazda
    HANDLE hMutex = CreateMutexA(NULL, FALSE, MUTEX_NAME);
	//NULL - default apsauga, 0 - pradzioj tuscia eile, TASK_QUEUE_SIZE - maksimalus tasku skaicius, SEMAPHORE_NAME - semaforo pavadinimas
    //skaitliukas
    HANDLE hSem = CreateSemaphoreA(NULL, 0, TASK_QUEUE_SIZE, SEMAPHORE_NAME);
    
    if (!hMutex || !hSem) {
        printf("Failed to create synchronization objects\n");
        UnmapViewOfFile(shared);
        CloseHandle(hSharedMem);
        return 1;
    }
    
    //Masyvas consumeriams
    HANDLE consumers[20];
    int numConsumers = 0;
    
    //Pradeda pati pirma produceri
    STARTUPINFOA si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
	si.cb = sizeof(si); //Nurodomas STARTUPINFO structo dydis
	si.dwFlags = STARTF_USESHOWWINDOW; //Nurodomas flagas, kad norim nustatyti lango rodymo buda
	si.wShowWindow = SW_SHOW; //Nustatome, kad langas turetu buti rodomas (ne pasleptas)
    
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    char cmdLine[MAX_PATH * 2];
    sprintf_s(cmdLine, _countof(cmdLine), "\"%s\" producer", exePath);
    //Basically tsg pasako yo, paleisk procesa su cmdLine ir naudok &si ir &pi. Visi kiti flagai default ir normal.
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("Failed to create producer process\n");
        UnmapViewOfFile(shared);
        CloseHandle(hSharedMem);
        CloseHandle(hMutex);
        CloseHandle(hSem);
        return 1;
    }
    CloseHandle(pi.hThread);
    HANDLE producerProc = pi.hProcess; //is pi paimam handle ir priskiriam producerProc, kad issaugoti katik sukurto
    //producerio handle
    
    printf("Producer process created (PID: %lu)\n", pi.dwProcessId);
    printf("Commands: + (add consumer), - (remove), r (results), q (quit)\n\n");
    
    //Sukurt pradinius consumerius (2 vnt)
    for (int i = 0; i < 2; i++) {
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOW;
        sprintf_s(cmdLine, _countof(cmdLine), "\"%s\" consumer", exePath);
        
        if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            consumers[numConsumers++] = pi.hProcess;
            CloseHandle(pi.hThread);
            printf("Consumer %d created (PID: %lu)\n", numConsumers, pi.dwProcessId);
        }
    }

    //CIA BAIGIOAU
    // Main loop
    DWORD lastDisplay = GetTickCount();
    
    while (TRUE) {
        if (_kbhit()) {
            int ch = _getch();
            
            if (ch == '+') {
                if (numConsumers < 20) {
                    si.dwFlags = STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_SHOW;
                    sprintf_s(cmdLine, _countof(cmdLine), "\"%s\" consumer", exePath);
                    
                    if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                        consumers[numConsumers++] = pi.hProcess;
                        CloseHandle(pi.hThread);
                        printf("Consumer %d created (PID: %lu). Total: %d\n", numConsumers, pi.dwProcessId, numConsumers);
                    }
                } else {
                    printf("Maximum consumers reached\n");
                }
            }
            else if (ch == '-') {
                if (numConsumers > 0) {
                    TerminateProcess(consumers[numConsumers - 1], 0);
                    WaitForSingleObject(consumers[numConsumers - 1], INFINITE);
                    CloseHandle(consumers[numConsumers - 1]);
                    numConsumers--;
                    printf("Consumer terminated. Total: %d\n", numConsumers);
                }
            }
            else if (ch == 'r' || ch == 'R') {
                WaitForSingleObject(hMutex, INFINITE);
                printf("\n--- RESULTS ---\n");
                printf("Queue: %d tasks\n", shared->queueCount);
                printf("Consumers: %d\n", numConsumers);
                if (shared->resultsInitialized) {
                    printf("Min Prime: %lld\n", shared->minPrime);
                    printf("Max Prime: %lld\n", shared->maxPrime);
                } else {
                    printf("(No primes found yet)\n");
                }
                printf("Producer done: %s\n", shared->producerDone ? "YES" : "NO");
                printf("---------------\n\n");
                ReleaseMutex(hMutex);
            }
            else if (ch == 'q' || ch == 'Q') {
                printf("Shutting down...\n");
                break;
            }
        }
        
        // Display results periodically
        DWORD now = GetTickCount();
        if (now - lastDisplay > 5000) {
            WaitForSingleObject(hMutex, INFINITE);
            printf("[Queue: %d | Consumers: %d | ", shared->queueCount, numConsumers);
            if (shared->resultsInitialized) {
                printf("Min: %lld, Max: %lld", shared->minPrime, shared->maxPrime);
            } else {
                printf("No primes yet");
            }
            printf("]\n");
            ReleaseMutex(hMutex);
            lastDisplay = now;
        }
        
        // Check if producer finished
        if (WaitForSingleObject(producerProc, 0) == WAIT_OBJECT_0) {
            printf("Producer finished. Waiting for consumers...\n");
            WaitForSingleObject(hMutex, 5000);
            break;
        }
        
        Sleep(100);
    }
    
    // Terminate all processes
    for (int i = 0; i < numConsumers; i++) {
        TerminateProcess(consumers[i], 0);
        WaitForSingleObject(consumers[i], INFINITE);
        CloseHandle(consumers[i]);
    }
    
    TerminateProcess(producerProc, 0);
    WaitForSingleObject(producerProc, INFINITE);
    CloseHandle(producerProc);
    
    // Final results
    printf("\n=== FINAL RESULTS ===\n");
    if (shared->resultsInitialized) {
        printf("Minimum Prime: %lld\n", shared->minPrime);
        printf("Maximum Prime: %lld\n", shared->maxPrime);
    } else {
        printf("No primes found\n");
    }
    printf("=====================\n");
    
    // Cleanup
    UnmapViewOfFile(shared);
    CloseHandle(hSharedMem);
    CloseHandle(hMutex);
    CloseHandle(hSem);
    
    return 0;
}