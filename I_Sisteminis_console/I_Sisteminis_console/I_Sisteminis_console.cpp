#include <iostream>
#include <string>
#include <cstdlib>
#include <windows.h>
#include <iomanip>

using namespace std;

int main(int argc, char* argv[])
{
	if (argc < 2) {
		cout << "Nenurodytas parametras!\n";
		cout << "Galimi parametrai:\n";
		cout << "--sysinfo\n";
		cout << "--error <code>\n";
		cout << "--prime <number>\n";
		cout << "--encode <text>\n";
		return 1;
	}

	string param = argv[1];

	cout << param << endl;

	if (param == "--sysinfo")
	{
		// rodo sisteminæ informacijà (WinAPI)
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo); // - butinai i pointeri, nes programa negali redaguoti duomenu, kurie yra outside of project, tai tam kad gauti duomenis i sysInfo vietini, reikia GetSystemInfo funkcijai paduoti pointeri musu variable

		//https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-system_info is cia istraukiau po tasko funckijas
		cout << "Number of Logical Processors: " << sysInfo.dwNumberOfProcessors << endl;
		cout << "Page size: " << sysInfo.dwPageSize << " Bytes" << endl;
		cout << "Processor Mask: 0x"<< hex <<sysInfo.dwActiveProcessorMask << endl;
		cout << "Minimum process address: 0x" << hex << sysInfo.lpMinimumApplicationAddress << endl;
		cout << "Maximum process address: 0x" << hex << sysInfo.lpMaximumApplicationAddress << endl;
	}
	else if (param == "--error")
	{
		//rodo Windows klaidos tekstà

		if(argc < 3) {
			cout << "Nenurodytas klaidos kodas!\n";
			return 1;
		}
		int errorCode = stoi(argv[2]);

		if (!((errorCode >= 0 && errorCode <= 15818) || errorCode == 15841)) //yra (0,15818) ir 15841 kodai, todel reik patikros
		{
			cout << errorCode << " -> " << "No such error exists";
			return 1;
		}

		char buffer[512];

		FormatMessageA(
			FORMAT_MESSAGE_FROM_SYSTEM | //Is windowsu paimk klaidos zinute pagal kodaa
			FORMAT_MESSAGE_IGNORE_INSERTS, //nepildyk jokiu papildomu reiksmiu i zinute 
			NULL, //cia jeigu norim is kazkokio specialaus failo imti zinute, tai cia nurodom is kur, o mes norim is windowsu, tai NULL
			errorCode,
			0, //kalbos pasirinkimas (0 - defaultas)
			buffer, //zinutes buferis aka stringas, i kuri irasys zinute (charo masyvas, nes stringo neleidzia)
			sizeof(buffer), //zinutes dydis
			NULL //papildomi argumentai, kuriu mes nenaudojam, tai NULL (tam rasem FORMAT_MESSAGE_IGNORE_INSERTS flaga)
		);

		cout <<errorCode<<" -> "<< string(buffer);
	}
	else if (param == "--prime")
	{
		//tikrina ar skaièius yra pirminis

		if(argc < 3) {
			cout << "Nenurodytas skaièius!\n";
			return 1;
		}
		int number = atoi(argv[2]);

		cout << "Tikrinu ar skaièius " << number << " yra pirminis\n";
		
	}
	else if(param == "--encode")
	{
		//BASE64

		if(argc < 3) {
			cout << "Nenurodytas tekstas!\n";
			return 1;
		}
		string text = argv[2];

		cout << "Koduojamas tekstas: " << text << endl;
	}

}