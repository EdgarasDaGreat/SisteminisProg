#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>

//Trumpiniai
#define SHARED_MEM_NAME "ProducerConsumer_SharedMem"
#define MUTEX_NAME "ProducerConsumer_Mutex"
#define SEMAPHORE_NAME "ProducerConsumer_Semaphore"
#define EVENT_NAME "ProducerConsumer_Event"
#define TASK_QUEUE_SIZE 1000


//Taskas - aka skaicius kuri tikrins ar cia prime. Galima ir be sito apsieiti drasiai
struct Task {
    long long number;
};

// Shared buffer - resides in shared memory
struct SharedBuffer {
    Task queue[TASK_QUEUE_SIZE];      //Masyvas 1000 elementu
    int queueStart;                   //Indexas nuo kur skaitom taskus
    int queueEnd;                     //Indexas kur rasom naujus taskus
    int queueCount;                   //Kiek tasku eilej
    
    long long minPrime;               //Maziausias primas rastas
    long long maxPrime;               //Didziausas primas rastas
    BOOL resultsInitialized;          //Ar mes radom prime?
};

//Funkcija prime tikrina
BOOL IsPrime(long long num) {
    if (num < 2) return FALSE;
    if (num == 2 || num == 3) return TRUE;
    if (num % 2 == 0 || num % 3 == 0) return FALSE;
    
    for (long long i = 5; i * i <= num; i += 6) {
        if (num % i == 0 || num % (i + 2) == 0) return FALSE;
    }
    return TRUE;
}

//Produceris
void RunProducer() {
    printf("PRODUCER: Starting...\n");
    
	//Atidarom bendra atminties objekta
    //Gali ir skaitti rasyti/nepaveldi vaikiniai procesai, pavadinimas kuriuo ieskom atminties gabalo
    HANDLE hSharedMem = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
    if (!hSharedMem) {
        printf("PRODUCER: Failed to open shared memory\n");
        return;
    }
    //pointeris i atminties gabala
    SharedBuffer* shared = (SharedBuffer*)MapViewOfFile(hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!shared) {
        printf("PRODUCER: Failed to map shared memory\n");
        CloseHandle(hSharedMem);
        return;
    }
    
    //Atidarom mutexa
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, MUTEX_NAME);
	//Atidarom semafora, turi teise sinchronizuotis ir modifikuoti semafora
    HANDLE hSem = OpenSemaphoreA(SEMAPHORE_MODIFY_STATE | SYNCHRONIZE, FALSE, SEMAPHORE_NAME);
    
    if (!hMutex || !hSem) {
        printf("PRODUCER: Failed to open synchronization objects\n");
        UnmapViewOfFile(shared);
        CloseHandle(hSharedMem);
        return;
    }
    
    //Pasiziurim ar turim failu direktorijoj
    WIN32_FIND_DATAA findData;
    HANDLE findHandle;
    char searchPath[MAX_PATH];
    
    GetCurrentDirectoryA(MAX_PATH, searchPath);
    strcat_s(searchPath, MAX_PATH, "\\rand_files");
    strcat_s(searchPath, MAX_PATH, "\\*.txt");
    
    findHandle = FindFirstFileA(searchPath, &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        printf("PRODUCER: No files found in rand_files directory\n");
        
        //Reikia eventa producerio ijungt, kad uzsibaigtu programa
        HANDLE hEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, EVENT_NAME);
        if (hEvent) {
            SetEvent(hEvent);
            CloseHandle(hEvent);
        }
        
        UnmapViewOfFile(shared);
        CloseHandle(hSharedMem);
        CloseHandle(hMutex);
        CloseHandle(hSem);
        return;
    }
    
    int filesProcessed = 0;
    
    //Visus failus apdoroti
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { //patikrina ar nera failas direktorija (papke)
            //cia sukuria pilna failo kelia i fullPath
            char fullPath[MAX_PATH];
            char dirPath[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, dirPath);
            strcat_s(dirPath, MAX_PATH, "\\rand_files\\");
            strcpy_s(fullPath, MAX_PATH, dirPath);
            strcat_s(fullPath, MAX_PATH, findData.cFileName);
            
            HANDLE hFile = CreateFileA(fullPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD fileSize = GetFileSize(hFile, NULL); //gaunam failo dydi
                char* buffer = (char*)malloc(fileSize + 1); //alokuojam jam atminties

                if (buffer) {//Jei atmintis alokuota
                    DWORD bytesRead;
					ReadFile(hFile, buffer, fileSize, &bytesRead, NULL); //nuskaitom faila i bufferi
                    buffer[bytesRead] = '\0'; //paverciam i stringa viska

                    long long num;
                    char* context = NULL;
                    char* token = strtok_s(buffer, " \t\n\r", &context); //Isskaido bufery esanti stringa pagal delimiterius ir nurodo i pirma token pointeri

                    while (token) {
                        if (sscanf_s(token, "%lld", &num) == 1) { //converuoja tokena i longlonga
                            BOOL added = FALSE;
                            while (!added) {
                                WaitForSingleObject(hMutex, INFINITE);//uzrakinam mutexa, kad neoverlapintu duomenys

                                if (shared->queueCount < TASK_QUEUE_SIZE) {//tikrina ar yra vietos
                                    shared->queue[shared->queueEnd].number = num;//ideda skaiciu
                                    shared->queueEnd = (shared->queueEnd + 1) % TASK_QUEUE_SIZE; //apsisaugom kad uz ribu neiseituma ir pastumiam gala
									shared->queueCount++; //padidina tasku skaiciu

                                    ReleaseMutex(hMutex); //paleidziam mutexa
                                    ReleaseSemaphore(hSem, 1, NULL); //signalizuojam consumeriams kad yra ka veikt eilej
                                    added = TRUE; //flagas kad pridejom
                                }
                                else {
                                    ReleaseMutex(hMutex); //Kol neprideta paleidziam atgal mutexa
                                    Sleep(10); //laukiam 10ms ir bandom vel rakint ir pridet
                                }
                            }
                        }
                        token = strtok_s(NULL, " \t\n\r", &context);//imam sekanti tokena (NULL nurodo tesiam kur baigem)
                    }

                    free(buffer); //atlaisvinam buferio atminti (nes malloc)
                }
                CloseHandle(hFile);//uzdarom failo handle kai nebeliko tokenu ten
				filesProcessed++; //padidina apdorotu failu skaiciu (grnj skaitliukui)

                if (filesProcessed % 100 == 0) {
                    printf("PRODUCER: Processed %d files...\n", filesProcessed);
                }
            }
            else
                printf("File was not open for reading. Skipping...");
        }
	} while (FindNextFileA(findHandle, &findData)); //imam sekanti faila ir kartojam procesa kol nebeliks failu
    
    FindClose(findHandle);//uzdarom direktorijos handle
    
    //Producerio eventas kad baige darba
    HANDLE hEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, EVENT_NAME);
    if (hEvent) {
        SetEvent(hEvent);
        CloseHandle(hEvent);
    }
    
    //Pasibaigus produceriui, signalizuoja consumeriams 10 fantominiu skaiciu, kad jie pabustu ir uzbaigtu darba
    for (int i = 0; i < 10; i++) {
        ReleaseSemaphore(hSem, 1, NULL);
    }
    
    printf("PRODUCER: Finished. Processed %d files.\n", filesProcessed);
    
    //Apsitvarko handles
    UnmapViewOfFile(shared);
    CloseHandle(hSharedMem);
    CloseHandle(hMutex);
    CloseHandle(hSem);
}

//Consumeris
void RunConsumer() {
    printf("CONSUMER (PID %lu): Starting...\n", GetCurrentProcessId());
    
	//tas pats kaip su produceriu, atidarom shared memory ir synchronization objects (cia dar evento reik)
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
    
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, MUTEX_NAME);
    HANDLE hSem = OpenSemaphoreA(SEMAPHORE_MODIFY_STATE | SYNCHRONIZE, FALSE, SEMAPHORE_NAME);
    HANDLE hEvent = OpenEventA(SYNCHRONIZE, FALSE, EVENT_NAME);
    
    if (!hMutex || !hSem || !hEvent) {
        printf("CONSUMER: Failed to open synchronization objects\n");
        UnmapViewOfFile(shared);
        CloseHandle(hSharedMem);
        return;
    }
    
    int tasksProcessed = 0;
    
    //Handle masyvas su eventu ir semaforu
    HANDLE waitHandles[2] = { hSem, hEvent };
    
    while (TRUE) {
        //Laukia semaphore (nauja uzduotis) ARBA event (producer baige)
        //cia false pasako laukiam arba to arba to
        DWORD result = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        
        WaitForSingleObject(hMutex, INFINITE);//Laukiam mutexo
        
        if (shared->queueCount > 0) {//jei yra uzduotciu
            Task task = shared->queue[shared->queueStart];//paima uzduoti is eiles priekio
            shared->queueStart = (shared->queueStart + 1) % TASK_QUEUE_SIZE;//pastumia starto buferi (vel su ciklline apsauga)
            shared->queueCount--;//uzduociu skaitliuka sumazina
            
            ReleaseMutex(hMutex);//paleidzia mutexa
            
            //Apdirba skaiciu (ar prime)
            if (IsPrime(task.number)) {//jeigu prime
                WaitForSingleObject(hMutex, INFINITE);//imam mutexa
                
                if (!shared->resultsInitialized) {//jei nebuvo rezultatu tai irasom skaiciu tiek i min tiek i max
                    shared->minPrime = task.number;
                    shared->maxPrime = task.number;
                    shared->resultsInitialized = TRUE;//flagas kad radom rezultata
                } else {
                    if (task.number < shared->minPrime) shared->minPrime = task.number;//Tikrinam ar skaicius mazesnis uz min
                    if (task.number > shared->maxPrime) shared->maxPrime = task.number;//Tikrinam ar skaicius didesnis uz max
                }
                
                ReleaseMutex(hMutex);//paleidziam mutexa
            }
            
            tasksProcessed++; //statsam kiek koks procesas apdorojo uzduociu
        } else {
            //Jei eile tuscia, tikrina ar producer baige
			ReleaseMutex(hMutex);//paleidziam mutexa ir tikrinam eventa
            
            DWORD eventState = WaitForSingleObject(hEvent, 0);
            if (eventState == WAIT_OBJECT_0 && shared->queueCount == 0) {//jei baigesi eile (tuscia) ir eventas produserio baigesi
                printf("CONSUMER (PID %lu): Producer finished. Processed %d tasks.\n", 
                       GetCurrentProcessId(), tasksProcessed);
                break;
            }
            continue;//griztam i loopa jeigu nesibaige
        }
    }
    
    printf("CONSUMER (PID %lu): Finished.\n", GetCurrentProcessId());
    
    //Apsivalom
    UnmapViewOfFile(shared);
    CloseHandle(hSharedMem);
    CloseHandle(hMutex);
    CloseHandle(hSem);
    CloseHandle(hEvent);
}

//Main controleris
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
		INVALID_HANDLE_VALUE, //Naudok atminties faila (RAM), o ne tikra faila
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
    HANDLE hEvent = CreateEventA(
        NULL,   // Default security
        TRUE,   // Manual reset (neišsijungia automatiškai)
        FALSE,  // Initial state = nesignalizuota
        EVENT_NAME
    );

    if (!hMutex || !hSem || !hEvent) {
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

    //Main loop
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
                
                DWORD eventState = WaitForSingleObject(hEvent, 0);
                printf("Producer done: %s\n", (eventState == WAIT_OBJECT_0) ? "YES" : "NO");
                
                printf("---------------\n\n");
                ReleaseMutex(hMutex);
            }
            else if (ch == 'q' || ch == 'Q') {
                printf("Shutting down...\n");
                break;
            }
        }
        
		//kas 2 sekundes atvaizduoja esama situacija (eile, consumeriu skaicius, ir min max primes)
        DWORD now = GetTickCount();
        if (now - lastDisplay > 2000) {
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
        
        //patikrina ar produceris baige
        if (WaitForSingleObject(hEvent, 0) == WAIT_OBJECT_0) {
            printf("Producer finished. Waiting 10s for consumers to finish...\n");
            
            // Laukia max 10 sekundziu
            DWORD result = WaitForMultipleObjects(numConsumers, consumers, TRUE, 10000);
            
            if (result == WAIT_TIMEOUT) {
                printf("Warning: Some consumers didn't finish in time. Forcing shutdown.\n");
            } else {
                printf("All consumers finished successfully.\n");
            }
            
            break;
        }
        
        Sleep(100);
    }
    
    //Visus procesus uzdaro
    for (int i = 0; i < numConsumers; i++) {
        TerminateProcess(consumers[i], 0);
        WaitForSingleObject(consumers[i], INFINITE);
        CloseHandle(consumers[i]);
    }
    
    TerminateProcess(producerProc, 0);
    WaitForSingleObject(producerProc, INFINITE);
    CloseHandle(producerProc);
    
    //Rezultatai
    printf("\n=== FINAL RESULTS ===\n");
    if (shared->resultsInitialized) {
        printf("Minimum Prime: %lld\n", shared->minPrime);
        printf("Maximum Prime: %lld\n", shared->maxPrime);
    } else {
        printf("No primes found\n");
    }
    printf("=====================\n");
    
    //Sutvarko handle
    UnmapViewOfFile(shared);
    CloseHandle(hSharedMem);
    CloseHandle(hMutex);
    CloseHandle(hSem);
    CloseHandle(hEvent);
    
    return 0;
}