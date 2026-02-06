#include <iostream>
#include <string>
#include <cstdlib>
#include <windows.h>
#include <iomanip>
#include <wincrypt.h>

#pragma comment(lib, "Crypt32.lib") //Nemeluosiu tiesiog gpt pasake, kad reikia prie base64 converterio

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
		// rodo sistemine informacija (WinAPI)
		//https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-system_info

		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo); // - butinai i pointeri, nes programa negali redaguoti duomenu, kurie yra outside of project, tai tam kad gauti duomenis i sysInfo vietini, reikia GetSystemInfo funkcijai paduoti pointeri musu variable

		cout << "Number of Logical Processors: " << sysInfo.dwNumberOfProcessors << endl;
		cout << "Page size: " << sysInfo.dwPageSize << " Bytes" << endl;
		cout << "Processor Mask: 0x" << hex << sysInfo.dwActiveProcessorMask << endl;
		cout << "Minimum process address: 0x" << hex << sysInfo.lpMinimumApplicationAddress << endl;
		cout << "Maximum process address: 0x" << hex << sysInfo.lpMaximumApplicationAddress << endl;
	}
	else if (param == "--error")
	{
		//rodo Windows klaidos teksta
		//https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage
		//<https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499->


		if (argc < 3) {
			cout << "No error code provided!\n";
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

		cout << errorCode << " -> " << string(buffer);
	}
	else if (param == "--prime")
	{
		//tikrina ar skaicius yra pirminis

		if (argc < 3) {
			cout << "No number specified!\n";
			return 1;
		}
		int number = atoi(argv[2]);

		bool isPrime = true;

		if (number < 2)
		{
			isPrime = false;
		}
		else
		{
			for (int i = 2; i <= number / 2; i++)
			{
				if (number % i == 0)
				{
					isPrime = false;
					break;
				}
			}
		}
		if (isPrime)
		{
			cout << "Number " << number << " is a prime number\n";
		}
		else
		{
			cout << "Number " << number << " is not a prime number\n";
		}
	}
	else if (param == "--encode")
	{
		//BASE64
		//https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptbinarytostringa

		if (argc < 3) {
			cout << "No text provided!\n";
			return 1;
		}

		string text = "";

		for (int i = 2; i < argc; i++)
		{
			text += argv[i];
			if (i != argc - 1) text += " ";
		}

		DWORD encodedSize = 0; //Dword 32 bitai unsigned integer

		//pirmas callas, kad suzinot kokio dydzio buferio reikia encoded tekstui
		CryptBinaryToStringA(
			(BYTE*)text.c_str(), //basically stringa type castinam i byte pointeri, nes sita funkcija gali convertuoti tik BYTE tipo duomenis, o text.c_str() grazina charo masyva is stringo
			text.length(),
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, //1 - pasako, kad reikia base64, o 2 - kad nereikia \n
			NULL, //Cia turetu buti buferis, i kuri norim irasyt encoded teksta, bet kadangi cia tik pirmas callas, mums reikia buferio dydzio todel null
			&encodedSize //pointeris i dydzio kintamaji, funkcija parasys cia reikiama dydi buferio
		);

		//sukuriame buffer pagal gauta dydi
		char* encodedText = new char[encodedSize];

		//antras callas, same kaip ir pirmas, tik irasome jau actual buferi i kuri norim, kad irasytu uzkoduota teksta
		CryptBinaryToStringA(
			(BYTE*)text.c_str(),
			text.length(),
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			encodedText,
			&encodedSize
		);

		cout << "|" << text << "|" << " -> " << encodedText << endl;
	}
	else
	{
		cout << param << " <- Parameter unknown\n";
	}

}