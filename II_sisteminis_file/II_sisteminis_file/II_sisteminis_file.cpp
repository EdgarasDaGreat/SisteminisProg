#include <windows.h>
#include <iostream>
#include <string>
#include <memory>

using namespace std;

#pragma pack(push, 1) //kad nepridetu paddingo tarp baitu, nes butina butent 128 nuo galo
struct ID3v1Tag {
    char header[3];      //tagas
    char title[30];      //dainos pavadinimas
    char artist[30];     //autorius
    char album[30];      //albumas
    char year[4];
    char comment[28];
    unsigned char zero;
    unsigned char track;
    unsigned char genre;
};
#pragma pack(pop) //strukturos definining pabaiga, toliau vel gali paddint

//customine funkcija atminties atlaisvinimui po unique_ptr
struct VirtualFreeDeleter {
    void operator()(void* p) const {
        if (p) VirtualFree(p, 0, MEM_RELEASE); //jei pointeris valid, atlaisvink vietos
        //p - pointeryje, 0 - trink viska, MEM_RELEASE - atlaisvink atminti sistemai
    }
};

int main()
{
    //1. ieskom pirmo mp3 failo darbiniam kataloge
    WIN32_FIND_DATAA findData; //struktura kuri laiko info apie failus
    HANDLE hFind = FindFirstFileA("*.mp3", &findData); //grazina HANDLE - vos ne pointeris i tam tikra resursa

    if (hFind == INVALID_HANDLE_VALUE) { //jei nerasta failu, grazina INVALID_HANDLE_VALUE
        cout << "No MP3 file found!" << endl;
        return 0;
    }

    string filename = findData.cFileName; //pasiimam pavadinima failo
    FindClose(hFind); //uzdarom find handle, nes mums daugiau nereikia ieskoti failu
    cout << "Found: " << filename << "\n" << endl;

    //2. skaitom ID3v1 tag'a is failo pabaigos
    HANDLE hFile = CreateFileA( //skaitom faila, grazina handle
        filename.c_str(), //pavadinima i char*
        GENERIC_READ,           //sakom, kad norim skaityt
        0,
        NULL,
        OPEN_EXISTING,          //failas turi egzistuot (open only)
        0,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) { //jei handle invalid - failo neatidarem
        cout << "Cannot open file for reading!" << endl;
        return 1;
    }

    LARGE_INTEGER offset;//64 bitu intas
    offset.QuadPart = -128;  //neigiamas skaicius, reiskias judes pointeris atgal
    //hFile - sitam faile, offset - judam tiek, null - senos pos saugot nereikia, FILE_END - nuo failo pabaigos
    //padeda pointeri i reikiama vieta faila, kad galima butu skaityti taga
    SetFilePointerEx(hFile, offset, NULL, FILE_END);

    ID3v1Tag tag;//tago structas
    //skaitom faila, rasom i structa, 128 baitu, jau auksciau nustatem  pointeri, nuo kur skaityt
    ReadFile(hFile, &tag, 128, NULL, NULL);
    CloseHandle(hFile);//uzdarom failo handle, nes perskaitem i structa info

    //parodom informacija, kuri yra faile
    cout << "Current info:" << endl;
    cout << "Name:   " << string(tag.title, 30) << endl; //sukisam i stringa ir spausdinam 30 baitu
    cout << "Author: " << string(tag.artist, 30) << endl;
    cout << "Album:  " << string(tag.album, 30) << endl;

    //3. klausiam vartotojo ar keiciam informacija
    cout << "\nEnter new info (press ENTER to skip):\n" << endl;

    string input; //inputas userio

    cout << "New name: ";
    getline(cin, input);//skaitom visa eilute
    if (!input.empty()) {//jei tiesiog spaudzia enter be teksto, bus empty
        memset(tag.title, 0, 30);  //memset - uzpildom title masyva nuliais
        //memcpy - kopijuojam inputo string i title masyva, pasiimam tik 30 baitu, nes tik tiek
        //turim vietos. memcpy(destination, source, count)
        memcpy(tag.title, input.c_str(), min(30, input.length()));
    }

    //cia same shit kaip ir auksciau
    cout << "New author: ";
    getline(cin, input);
    if (!input.empty()) {
        memset(tag.artist, 0, 30);
        memcpy(tag.artist, input.c_str(), min(30, input.length()));
    }

    //cia same shit kaip ir auksciau
    cout << "New album: ";
    getline(cin, input);
    if (!input.empty()) {
        memset(tag.album, 0, 30);
        memcpy(tag.album, input.c_str(), min(30, input.length()));
    }

    //4. alokuojam dinamiskai vietos
    unique_ptr<ID3v1Tag, VirtualFreeDeleter> pTag( //smart pointeris su trinimo metodu, kuri pasikviecia, kai programa arba funkcija returnina (siuo atveju main)
        static_cast<ID3v1Tag*>(VirtualAlloc( //castinam tago structura ant alokavimo, nes alokavimas grazins generic pointeri, bet unique_ptr laukia butent tago pointerio
            NULL,                    //sistema pati renkasi vieta
            128,                     //kiek alokuoti vietos
            MEM_COMMIT | MEM_RESERVE, //reservuoja ir priskiria atminti
            PAGE_READWRITE           //leidziam atminti read/write
        ))
    );

    if (!pTag) { //jei virtualAlloc grazino nullptr, tai gaundom sita
        cout << "Memory allocation failed!" << endl;
        return 1;
    }

    //kopijuojam modifikuota taga i alokuota vieta, kad galetume ji rasyti i faila
    memcpy(pTag.get(), &tag, 128);

    //5. rasom i faila
    hFile = CreateFileA( //vel atidarom
        filename.c_str(),
        GENERIC_WRITE,         //cia skirtumas, nes dabar mes norim WRITE, o ne READ
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        cout << "Cannot open file for writing! (File might be read-only)" << endl;
        return 1;
    }

    //tas pats, nustatom pointeri 128 baitus nuo galo
    SetFilePointerEx(hFile, offset, NULL, FILE_END);
    //rasom i faila
    //hFile - kur rasom, pTag.get() - ka rasom, 128 - kiek rasom
    WriteFile(hFile, pTag.get(), 128, NULL, NULL);
    //uzdarom hande
    CloseHandle(hFile);

    cout << "\nDone! Updated:" << endl;
    cout << "Name:   " << string(tag.title, 30) << endl;
    cout << "Author: " << string(tag.artist, 30) << endl;
    cout << "Album:  " << string(tag.album, 30) << endl;

    return 0;
}