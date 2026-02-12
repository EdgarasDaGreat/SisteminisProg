#include <windows.h>
#include <iostream>
#include <string>

using namespace std;

int main()
{
    // Change this to your project directory
    string projectDir = "C:\\Users\\20233476\\Downloads\\SisteminisProg\\II_sisteminis_file";

    // Search pattern for mp3 files
    string searchPath = projectDir + "*.mp3";

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        cout << "No MP3 files found." << endl;
        return 0;
    }

    do
    {
        // Skip directories (just in case)
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            cout << "Found MP3 file: "
                << projectDir + findData.cFileName
                << endl;

            break; // remove this if you want to list all mp3 files
        }

    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    return 0;
}