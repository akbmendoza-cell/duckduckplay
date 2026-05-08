/*
 * ============================================================
 *   C++ Music Player - SFML 3.1.0 Edition
 *
 *   Features:
 *   1. Browse songs with UP/DOWN arrow keys
 *   2. LEFT/RIGHT arrow keys seek -10 or +10 seconds
 *   3. Pause, Stop, Next, Previous, Random
 *   4. Shuffle (keeps current song at the top)
 *   5. Custom Playlist (add/remove songs with [P])
 *   6. Search and Sort
 *   7. KTV / Lyrics Mode (add more songs easily - see below)
 *   8. Play history / Wrapped summary
 *   9. Add and Delete songs
 * ============================================================
 *
 *  HOW TO COMPILE (Windows + MinGW, SFML 3.1.0):
 *    g++ duckduckplay.cpp -o duckduckplay.exe
 *        -I"C:\Users\Henry james\Documents\libraries\SFML-3.1.0\include"
 *        -L"C:\Users\Henry james\Documents\libraries\SFML-3.1.0\lib"
 *        -lsfml-audio -lsfml-system
 *
 *  FILES NEEDED (same folder as the .exe):
 *    playlist.csv              -- your song library (auto-created)
 *    history.csv               -- auto-created when songs are played
 *    sfml-audio-3.dll, sfml-system-3.dll, openal32.dll
 *      (found in your SFML-3.1.0\bin\ folder)
 *
 *  HOW TO ADD MORE KTV SONGS:
 *    1. Create a CSV file for the lyrics (e.g., alipin.csv)
 *       Format of each row:  Verse text, Start time (MM:SS), End time (MM:SS or END)
 *       Example row:  Ikaw ang aking alipin,0:30,0:45
 *    2. Scroll down to the KTV LIBRARY section and add a line there.
 */

#include <SFML/Audio.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <map>
#include <windows.h>
#include <conio.h>

using namespace std;

//  CONSTANTS

const string CSV_FILE = "playlist.csv";
const string HISTORY_FILE = "history.csv";
const string EXPECTED_HEADER = "Title,Artist,Album,Genre,Year,Duration,FilePath";
const string HISTORY_HEADER = "Title,Artist,Genre,Timestamp";
const int MAX_SONGS = 500;
const int MAX_FIELDS = 7;
const int VIEWPORT_SIZE = 10;
const int SCREEN_WIDTH = 70;

//  KTV LIBRARY

struct KtvEntry
{
    string songTitle;
    string csvFile;
};

KtvEntry ktvLibrary[] = {
    {"Shape of My Heart", "lyrics/shape_of_my_heart.csv"},
    {"Incomplete", "lyrics/incomplete.csv"},
    {"Knocks Me Off My Feet", "lyrics/knocks_me_off_my_feet.csv"},
    {"Alipin", "lyrics/alipin.csv"}};

const int KTV_COUNT = sizeof(ktvLibrary) / sizeof(ktvLibrary[0]);

//  UTILITY FUNCTIONS

void clearScreen() { system("cls"); }
void sleepMs(int ms) { Sleep(ms); }

void moveCursorHome()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos = {0, 0};
    SetConsoleCursorPosition(handle, pos);
}

void clearRestOfLine()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(handle, &info))
    {
        COORD currentPos = info.dwCursorPosition;
        DWORD charsToErase = info.dwSize.X - currentPos.X;
        DWORD charsWritten;
        FillConsoleOutputCharacterA(handle, ' ', charsToErase, currentPos, &charsWritten);
        SetConsoleCursorPosition(handle, currentPos);
    }
}

void showCursor(bool visible)
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(handle, &cursorInfo);
    cursorInfo.bVisible = visible ? TRUE : FALSE;
    SetConsoleCursorInfo(handle, &cursorInfo);
}

string truncate(const string &text, int maxLength)
{
    if ((int)text.size() <= maxLength)
        return text;
    return text.substr(0, maxLength - 3) + "...";
}

string toLower(string text)
{
    for (int i = 0; i < (int)text.size(); i++)
        text[i] = (char)tolower(text[i]);
    return text;
}

string trim(string text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\r' || text.front() == '\t'))
        text.erase(text.begin());
    while (!text.empty() && (text.back() == ' ' || text.back() == '\r' || text.back() == '\t'))
        text.pop_back();
    return text;
}

//  SONG

class Song
{
public:
    string title, artist, album, genre, duration, filePath;
    int year, playCount;

    Song() : year(0), playCount(0) {}
    Song(string t, string ar, string al, string g, int y, string d, string fp)
        : title(t), artist(ar), album(al), genre(g), year(y),
          duration(d), filePath(fp), playCount(0) {}

    int durationSeconds() const
    {
        size_t colonPos = duration.find(':');
        if (colonPos == string::npos)
            return 0;
        return stoi(duration.substr(0, colonPos)) * 60 + stoi(duration.substr(colonPos + 1));
    }
};

//  NODE

class Node
{
public:
    Song song;
    Node *next;
    Node *prev;
    Node(Song s) : song(s), next(nullptr), prev(nullptr) {}
};

//  LYRIC LINE

struct LyricLine
{
    string verse;
    int startSec;
    int endSec; // -1 = until end of song
};

//  HISTORY MANAGER

class HistoryManager
{
public:
    void record(const Song &s)
    {
        time_t now = time(nullptr);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

        ifstream checkFile(HISTORY_FILE);
        bool hasHeader = false;
        if (checkFile.is_open())
        {
            string first;
            getline(checkFile, first);
            if (first.find("Title") != string::npos)
                hasHeader = true;
            checkFile.close();
        }

        ofstream out(HISTORY_FILE, ios::app);
        if (!out.is_open())
            return;
        if (!hasHeader)
            out << HISTORY_HEADER << "\n";
        out << s.title << "," << s.artist << "," << s.genre << "," << buf << "\n";
        out.close();
    }

    void showWrapped()
    {
        ifstream in(HISTORY_FILE);
        if (!in.is_open())
        {
            cout << "\n  No play history found yet.\n";
            return;
        }

        map<string, int> songCount, artistCount, genreCount;
        string line;
        getline(in, line);
        int total = 0;

        while (getline(in, line))
        {
            if (line.empty())
                continue;
            vector<string> fields;
            stringstream ss(line);
            string tok;
            while (getline(ss, tok, ','))
                fields.push_back(trim(tok));
            if (fields.size() < 3)
                continue;
            songCount[fields[0]]++;
            artistCount[fields[1]]++;
            genreCount[fields[2]]++;
            total++;
        }
        in.close();

        if (total == 0)
        {
            cout << "\n  No plays recorded yet.\n";
            return;
        }

        string topSong = "", topArtist = "", topGenre = "";
        int topSC = 0, topAC = 0, topGC = 0;
        for (auto &e : songCount)
            if (e.second > topSC)
            {
                topSong = e.first;
                topSC = e.second;
            }
        for (auto &e : artistCount)
            if (e.second > topAC)
            {
                topArtist = e.first;
                topAC = e.second;
            }
        for (auto &e : genreCount)
            if (e.second > topGC)
            {
                topGenre = e.first;
                topGC = e.second;
            }

        clearScreen();
        string rule(SCREEN_WIDTH, '=');
        cout << rule << "\n";
        cout << "         *** YOUR SPOTIFY-WRAPPED SUMMARY ***\n";
        cout << rule << "\n\n";
        cout << "  Total plays recorded : " << total << "\n\n";
        cout << "  Most Played Song     : \"" << topSong << "\" (" << topSC << " plays)\n";
        cout << "  Favorite Artist      : " << topArtist << "  (" << topAC << " plays)\n";
        cout << "  Top Genre            : " << topGenre << "  (" << topGC << " plays)\n\n";

        vector<pair<string, int>> sorted(songCount.begin(), songCount.end());
        for (int i = 0; i < (int)sorted.size() - 1; i++)
            for (int j = 0; j < (int)sorted.size() - 1 - i; j++)
                if (sorted[j].second < sorted[j + 1].second)
                    swap(sorted[j], sorted[j + 1]);

        cout << "  -- All Song Plays --\n";
        for (auto &p : sorted)
            cout << "    " << p.second << "x  " << p.first << "\n";

        cout << "\n"
             << rule << "\n\n  Press Enter to return...";
        showCursor(true);
        cin.get();
        showCursor(false);
    }
};

//  LYRICS ENGINE

class LyricsEngine
{
    vector<LyricLine> lines;

    int parseTime(const string &t)
    {
        if (t == "END" || t == "end")
            return -1;
        size_t c = t.find(':');
        if (c == string::npos)
            return 0;
        return stoi(t.substr(0, c)) * 60 + stoi(t.substr(c + 1));
    }

public:
    bool load(const string &path)
    {
        lines.clear();
        ifstream in(path);
        if (!in.is_open())
            return false;
        string line;
        getline(in, line); // skip header
        while (getline(in, line))
        {
            if (line.empty())
                continue;
            vector<string> f;
            stringstream ss(line);
            string tok;
            while (getline(ss, tok, ','))
                f.push_back(trim(tok));
            if (f.size() < 3)
                continue;
            LyricLine ll;
            ll.verse = f[0];
            ll.startSec = parseTime(f[1]);
            ll.endSec = parseTime(f[2]);
            lines.push_back(ll);
        }
        return !lines.empty();
    }

    string getLine(int elapsed) const
    {
        for (auto &l : lines)
        {
            int end = (l.endSec == -1) ? 99999 : l.endSec;
            if (elapsed >= l.startSec && elapsed < end)
                return l.verse;
        }
        return "";
    }

    bool loaded() const { return !lines.empty(); }
};

//  AUDIO ENGINE  --  compatible with SFML 3.1.0
//
//  Despite being SFML 3, the Audio API kept the SFML 2 style:
//    - openFromFile() is still an instance method returning bool
//    - setPlayingOffset() is still used for seeking
//    - Status constants moved to sf::Music::Status::Playing etc.

class AudioEngine
{
    sf::Music music;
    string loadedPath;

public:
    // Load an audio file. Returns true on success.
    bool load(const string &path)
    {
        if (loadedPath == path &&
            music.getStatus() != sf::Music::Status::Stopped)
        {
            return true;
        }
        music.stop();
        if (!music.openFromFile(path))
        {
            cout << "  [!] Cannot open audio file: " << path << "\n";
            return false;
        }
        loadedPath = path;
        return true;
    }

    void play() { music.play(); }
    void pause() { music.pause(); }
    void resume() { music.play(); }

    void stop()
    {
        music.stop();
        loadedPath = "";
    }

    void seek(int newSeconds)
    {
        if (newSeconds < 0)
            newSeconds = 0;
        music.setPlayingOffset(sf::seconds((float)newSeconds));
    }

    bool isPlaying() const { return music.getStatus() == sf::Music::Status::Playing; }
    bool isPaused() const { return music.getStatus() == sf::Music::Status::Paused; }
    bool isStopped() const { return music.getStatus() == sf::Music::Status::Stopped; }

    int elapsedSeconds() const
    {
        return (int)music.getPlayingOffset().asSeconds();
    }
};

//  CSV MANAGER

class CSVManager
{
public:
    bool parseLine(string line, string fields[], int fieldCount)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        int idx = 0;
        string tok = "";
        for (int i = 0; i <= (int)line.size(); i++)
        {
            if (i == (int)line.size() || line[i] == ',')
            {
                if (idx >= fieldCount)
                    return false;
                fields[idx++] = tok;
                tok = "";
            }
            else
            {
                tok += line[i];
            }
        }
        return idx == fieldCount;
    }

    bool isDuplicate(const string &title, const string &artist)
    {
        ifstream in(CSV_FILE);
        if (!in.is_open())
            return false;
        string line;
        getline(in, line);
        while (getline(in, line))
        {
            if (line.empty())
                continue;
            string f[MAX_FIELDS];
            if (!parseLine(line, f, MAX_FIELDS))
                continue;
            if (toLower(f[0]) == toLower(title) && toLower(f[1]) == toLower(artist))
            {
                in.close();
                return true;
            }
        }
        in.close();
        return false;
    }

    void saveSong(const Song &s)
    {
        ifstream check(CSV_FILE);
        bool hasHeader = false;
        if (check.is_open())
        {
            string first;
            getline(check, first);
            if (first.find("Title") != string::npos)
                hasHeader = true;
            check.close();
        }
        if (!hasHeader)
        {
            ofstream hf(CSV_FILE);
            hf << EXPECTED_HEADER << "\n";
            hf.close();
        }

        {
            ifstream peek(CSV_FILE, ios::ate | ios::binary);
            if (peek.is_open() && peek.tellg() > 0)
            {
                peek.seekg(-1, ios::end);
                char last = '\0';
                peek.get(last);
                peek.close();
                if (last != '\n')
                {
                    ofstream fix(CSV_FILE, ios::app | ios::binary);
                    fix << "\n";
                    fix.close();
                }
            }
        }

        ofstream out(CSV_FILE, ios::app);
        if (!out.is_open())
            return;
        out << s.title << "," << s.artist << "," << s.album << ","
            << s.genre << "," << s.year << "," << s.duration << ","
            << s.filePath << "\n";
        out.close();
    }

    int loadSongs(Song songs[], int maxSongs)
    {
        ifstream in(CSV_FILE);
        if (!in.is_open())
            return 0;
        string line;
        getline(in, line);
        int count = 0;
        while (getline(in, line) && count < maxSongs)
        {
            if (line.empty())
                continue;
            string f[MAX_FIELDS];
            if (!parseLine(line, f, MAX_FIELDS))
                continue;
            try
            {
                songs[count++] = Song(f[0], f[1], f[2], f[3], stoi(f[4]), f[5], f[6]);
            }
            catch (...)
            {
            }
        }
        in.close();
        return count;
    }

    void rewriteAll(Node *head)
    {
        if (!head)
        {
            ofstream out(CSV_FILE);
            out << EXPECTED_HEADER << "\n";
            out.close();
            return;
        }
        ofstream out(CSV_FILE);
        if (!out.is_open())
            return;
        out << EXPECTED_HEADER << "\n";
        Node *cur = head;
        do
        {
            Song &s = cur->song;
            out << s.title << "," << s.artist << "," << s.album << ","
                << s.genre << "," << s.year << "," << s.duration << ","
                << s.filePath << "\n";
            cur = cur->next;
        } while (cur != head);
        out.close();
    }

    void ensureCSV()
    {
        ifstream check(CSV_FILE);
        if (check.is_open())
        {
            string first;
            getline(check, first);
            check.close();
            if (trim(first) == EXPECTED_HEADER)
                return;
        }
        ofstream out(CSV_FILE);
        out << EXPECTED_HEADER << "\n";
        out.close();
    }
};

//  MUSIC PLAYER

class MusicPlayer
{
    Node *head;
    Node *current;
    Node *browseCursor;
    bool paused;
    bool firstDraw;

    AudioEngine audio;
    CSVManager csv;
    HistoryManager history;
    vector<string> customPlaylist;

    string makeKey(const Song &s) { return s.title + "|" + s.artist; }

    int countSongs() const
    {
        if (!head)
            return 0;
        int n = 0;
        Node *node = head;
        do
        {
            n++;
            node = node->next;
        } while (node != head);
        return n;
    }

    void insertAtEnd(Song s)
    {
        Node *n = new Node(s);
        if (!head)
        {
            n->next = n->prev = n;
            head = n;
            return;
        }
        Node *tail = head->prev;
        tail->next = n;
        n->prev = tail;
        n->next = head;
        head->prev = n;
    }

    Node *removeNode(Node *n)
    {
        if (!n)
            return nullptr;
        if (countSongs() == 1)
        {
            delete n;
            head = nullptr;
            return nullptr;
        }
        Node *next = n->next;
        n->prev->next = n->next;
        n->next->prev = n->prev;
        if (head == n)
            head = next;
        delete n;
        return next;
    }

    void shuffleList()
    {
        int total = countSongs();
        if (total <= 1)
            return;
        string playTitle = "", playArtist = "";
        if (current)
        {
            playTitle = current->song.title;
            playArtist = current->song.artist;
        }

        vector<Node *> nodes;
        Node *node = head;
        do
        {
            nodes.push_back(node);
            node = node->next;
        } while (node != head);

        for (int i = total - 1; i > 0; i--)
            swap(nodes[i]->song, nodes[rand() % (i + 1)]->song);

        if (!playTitle.empty())
        {
            node = head;
            do
            {
                if (node->song.title == playTitle && node->song.artist == playArtist)
                {
                    swap(node->song, head->song);
                    break;
                }
                node = node->next;
            } while (node != head);
            current = browseCursor = head;
        }
    }

    void selectionSort(int key)
    {
        int total = countSongs();
        if (total <= 1)
            return;
        vector<Node *> nodes;
        Node *node = head;
        do
        {
            nodes.push_back(node);
            node = node->next;
        } while (node != head);

        for (int i = 0; i < total - 1; i++)
        {
            int best = i;
            for (int j = i + 1; j < total; j++)
            {
                bool smaller = false;
                if (key == 0)
                    smaller = toLower(nodes[j]->song.title) < toLower(nodes[best]->song.title);
                else if (key == 1)
                    smaller = toLower(nodes[j]->song.artist) < toLower(nodes[best]->song.artist);
                else if (key == 2)
                    smaller = nodes[j]->song.year < nodes[best]->song.year;
                else
                    smaller = nodes[j]->song.durationSeconds() < nodes[best]->song.durationSeconds();
                if (smaller)
                    best = j;
            }
            if (best != i)
                swap(nodes[i]->song, nodes[best]->song);
        }
    }

    Node *searchSong(const string &query)
    {
        if (!head || query.empty())
            return nullptr;
        string lq = toLower(query);
        Node *node = head;
        do
        {
            if (toLower(node->song.title).find(lq) != string::npos ||
                toLower(node->song.artist).find(lq) != string::npos ||
                toLower(node->song.genre).find(lq) != string::npos)
                return node;
            node = node->next;
        } while (node != head);
        return nullptr;
    }

    string totalRuntime()
    {
        if (!head)
            return "0m 0s";
        int secs = 0, n = countSongs();
        Node *node = head;
        for (int i = 0; i < n; i++)
        {
            secs += node->song.durationSeconds();
            node = node->next;
        }
        int h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
        string r = "";
        if (h > 0)
            r += to_string(h) + "h ";
        return r + to_string(m) + "m " + to_string(s) + "s";
    }

    void printRule(char ch)
    {
        cout << string(SCREEN_WIDTH, ch);
        clearRestOfLine();
        cout << "\n";
    }

    void printCentered(const string &text)
    {
        int pad = (SCREEN_WIDTH - (int)text.size()) / 2;
        if (pad < 0)
            pad = 0;
        cout << string(pad, ' ') << text;
        clearRestOfLine();
        cout << "\n";
    }

    string formatTime(int secs) const
    {
        if (secs < 0)
            secs = 0;
        string m = to_string(secs / 60), s = to_string(secs % 60);
        if (m.size() < 2)
            m = "0" + m;
        if (s.size() < 2)
            s = "0" + s;
        return m + ":" + s;
    }

    string buildSongRow(int index, Node *node, bool isBrowsed, bool isCurrent)
    {
        string marker;
        if (isBrowsed && isCurrent)
            marker = ">(*";
        else if (isBrowsed)
            marker = ">> ";
        else if (isCurrent)
            marker = " * ";
        else
            marker = "   ";

        string dur = "[" + node->song.duration + "]";
        string suffix = isBrowsed ? ("  " + dur + " <--") : ("  " + dur);
        string prefix = to_string(index + 1) + ". \"";
        int budget = SCREEN_WIDTH - 1 - 3 - (int)prefix.size() - 5 - (int)suffix.size();
        if (budget < 4)
            budget = 4;
        int tMax = budget * 6 / 10, aMax = budget - tMax;
        string row = " " + marker + prefix + truncate(node->song.title, tMax) + "\" by " + truncate(node->song.artist, aMax) + suffix;
        if ((int)row.size() < SCREEN_WIDTH)
            row += string(SCREEN_WIDTH - row.size(), ' ');
        else if ((int)row.size() > SCREEN_WIDTH)
            row = row.substr(0, SCREEN_WIDTH);
        return row;
    }

    void drawScreen()
    {
        if (firstDraw)
        {
            clearScreen();
            firstDraw = false;
        }
        else
            moveCursorHome();

        printRule('=');
        printCentered("C++ Music Player");
        printRule('=');

        if (current && (audio.isPlaying() || audio.isPaused()))
        {
            int elapsed = audio.elapsedSeconds(), total = current->song.durationSeconds();
            int remaining = total - elapsed, barWidth = 34;
            int filled = (total > 0) ? min(elapsed * barWidth / total, barWidth) : 0;
            bool ended = (total > 0 && elapsed >= total);
            string status = paused ? "[PAUSED]" : (ended ? "[ENDED]" : "[PLAYING]");
            printCentered(">> \"" + truncate(current->song.title, 28) + "\" by " + truncate(current->song.artist, 18));
            string bar = formatTime(elapsed) + " [";
            for (int i = 0; i < barWidth; i++)
                bar += (i < filled) ? '#' : '-';
            bar += "] -" + formatTime(remaining) + " " + status;
            printCentered(bar);
        }
        else
        {
            printCentered("No song is currently playing.");
            cout << "\n";
        }

        printRule('-');
        printCentered("Songs: " + to_string(countSongs()) + "  |  Total Runtime: " + totalRuntime());
        printRule('-');

        if (!head)
        {
            printCentered("Song list is empty.  Press [A] to add a song.");
        }
        else
        {
            int total = countSongs();
            int browseIdx = 0, idx = 0;
            Node *node = head;
            do
            {
                if (node == browseCursor)
                    browseIdx = idx;
                idx++;
                node = node->next;
            } while (node != head);

            int vs = browseIdx - VIEWPORT_SIZE / 2;
            if (vs < 0)
                vs = 0;
            if (vs + VIEWPORT_SIZE > total)
                vs = total - VIEWPORT_SIZE;
            if (vs < 0)
                vs = 0;
            int ve = min(vs + VIEWPORT_SIZE, total);

            if (vs > 0)
                printCentered("... (" + to_string(vs) + " more above)");

            node = head;
            for (int i = 0; i < vs; i++)
                node = node->next;
            for (int i = vs; i < ve; i++)
            {
                cout << buildSongRow(i, node, node == browseCursor, current && node == current);
                clearRestOfLine();
                cout << "\n";
                node = node->next;
            }
            int below = total - ve;
            if (below > 0)
                printCentered("... (" + to_string(below) + " more below)");
        }

        printRule('-');
        printCentered("[UP/DOWN] Browse   [LEFT/RIGHT] Seek -/+10s   [ENTER] Play");
        printCentered("[N] Next   [B] Prev   [SPACE] Pause   [S] Stop   [R] Random");
        printCentered("[H] Shuffle   [A] Add   [7] Delete   [F] Search   [O] Sort");
        printCentered("[P] Custom Playlist   [K] KTV Mode   [W] Wrapped   [Q] Quit");
        printRule('=');
    }

    void startPlaying(Node *node)
    {
        if (!node)
            return;
        audio.stop();
        current = node;
        paused = false;
        if (!node->song.filePath.empty() && audio.load(node->song.filePath))
        {
            audio.play();
            node->song.playCount++;
            history.record(node->song);
        }
        browseCursor = node;
    }

    void stopPlaying()
    {
        audio.stop();
        current = nullptr;
        paused = false;
    }

    void togglePause()
    {
        if (!current)
            return;
        if (audio.isPaused())
        {
            audio.resume();
            paused = false;
        }
        else if (audio.isPlaying())
        {
            audio.pause();
            paused = true;
        }
    }

    void addSong()
    {
        clearScreen();
        showCursor(true);
        cout << string(SCREEN_WIDTH, '=') << "\n  Add New Song\n"
             << string(SCREEN_WIDTH, '=') << "\n\n";
        cout << "  Enter the full path to the audio file (e.g., C:\\Music\\song.mp3):\n\n";
        string filePath;
        cout << "  File path : ";
        getline(cin, filePath);
        filePath = trim(filePath);

        string guess = filePath;
        size_t sl = guess.find_last_of("/\\");
        if (sl != string::npos)
            guess = guess.substr(sl + 1);
        size_t dot = guess.rfind('.');
        if (dot != string::npos)
            guess = guess.substr(0, dot);
        for (int i = 0; i < (int)guess.size(); i++)
            if (guess[i] == '_')
                guess[i] = ' ';

        cout << "\n  Auto-detected title: \"" << guess << "\"\n";
        cout << "  Title (press Enter to keep as-is): ";
        string t;
        getline(cin, t);
        if (!t.empty())
            guess = trim(t);

        cout << "  Artist   : ";
        string artist;
        getline(cin, artist);
        artist = trim(artist);

        if (csv.isDuplicate(guess, artist))
        {
            cout << "\n  [!] This song is already in the playlist!\n  Press Enter to go back...";
            cin.get();
            showCursor(false);
            return;
        }

        cout << "  Album    : ";
        string album;
        getline(cin, album);
        album = trim(album);
        cout << "  Genre    : ";
        string genre;
        getline(cin, genre);
        genre = trim(genre);
        cout << "  Year     : ";
        int year = 0;
        cin >> year;
        cin.ignore();
        cout << "  Duration (MM:SS, e.g., 3:45): ";
        string dur;
        getline(cin, dur);
        dur = trim(dur);

        Song ns(guess, artist, album, genre, year, dur, filePath);
        insertAtEnd(ns);
        csv.saveSong(ns);
        if (!browseCursor)
            browseCursor = head;

        cout << "\n  [OK] Song added: \"" << guess << "\"\n  Press Enter to continue...";
        cin.get();
        showCursor(false);
        firstDraw = true;
    }

    void deleteSong()
    {
        if (!browseCursor)
        {
            cout << "  No song selected.\n";
            sleepMs(800);
            return;
        }
        clearScreen();
        showCursor(false);
        cout << string(SCREEN_WIDTH, '=') << "\n  DELETE: \""
             << browseCursor->song.title << "\" by " << browseCursor->song.artist << "\n"
             << string(SCREEN_WIDTH, '=') << "\n\n";
        cout << "  Are you sure? Press Y to confirm, any other key to cancel: ";
        showCursor(true);
        char c = (char)_getch();
        cout << c << "\n";
        showCursor(false);

        if (c != 'y' && c != 'Y')
        {
            cout << "  Cancelled.\n";
            sleepMs(600);
            firstDraw = true;
            return;
        }

        if (current == browseCursor)
            stopPlaying();
        string key = makeKey(browseCursor->song);
        for (int i = 0; i < (int)customPlaylist.size(); i++)
            if (customPlaylist[i] == key)
            {
                customPlaylist.erase(customPlaylist.begin() + i);
                break;
            }

        browseCursor = removeNode(browseCursor);
        if (!browseCursor && head)
            browseCursor = head;
        csv.rewriteAll(head);
        cout << "  Song deleted.\n";
        sleepMs(700);
        firstDraw = true;
    }

    void searchMenu()
    {
        clearScreen();
        showCursor(true);
        cout << string(SCREEN_WIDTH, '=') << "\n  Search\n"
             << string(SCREEN_WIDTH, '=') << "\n\n";
        cout << "  Type a title, artist name, or genre: ";
        string q;
        getline(cin, q);
        Node *found = searchSong(q);
        if (found)
        {
            browseCursor = found;
            cout << "\n  Found: \"" << found->song.title << "\" by " << found->song.artist << "\n";
        }
        else
            cout << "\n  No match found for \"" << q << "\".\n";
        cout << "\n  Press Enter to return...";
        cin.get();
        showCursor(false);
        firstDraw = true;
    }

    void sortMenu()
    {
        clearScreen();
        showCursor(true);
        cout << string(SCREEN_WIDTH, '=') << "\n  Sort Songs\n"
             << string(SCREEN_WIDTH, '=') << "\n\n";
        cout << "  [1] Title A-Z\n  [2] Artist A-Z\n  [3] Year (oldest first)\n  [4] Duration (shortest first)\n  [0] Cancel\n\n  Your choice: ";
        char c = (char)_getch();
        cout << c << "\n";
        int key = -1;
        if (c == '1')
            key = 0;
        else if (c == '2')
            key = 1;
        else if (c == '3')
            key = 2;
        else if (c == '4')
            key = 3;
        if (key >= 0)
        {
            selectionSort(key);
            browseCursor = head;
            cout << "\n  Done!\n";
        }
        sleepMs(700);
        showCursor(false);
        firstDraw = true;
    }

    void playlistMenu()
    {
        while (true)
        {
            clearScreen();
            showCursor(false);
            cout << string(SCREEN_WIDTH, '=') << "\n  Custom Playlist  (" << customPlaylist.size() << " songs)\n"
                 << string(SCREEN_WIDTH, '=') << "\n\n";
            if (customPlaylist.empty())
                cout << "  (No songs added yet.)\n";
            else
                for (int i = 0; i < (int)customPlaylist.size(); i++)
                {
                    string key = customPlaylist[i];
                    size_t sep = key.find('|');
                    cout << "  " << (i + 1) << ". \"" << (sep != string::npos ? key.substr(0, sep) : key) << "\" by " << (sep != string::npos ? key.substr(sep + 1) : "") << "\n";
                }
            cout << "\n  Highlighted: ";
            if (browseCursor)
                cout << "\"" << browseCursor->song.title << "\" by " << browseCursor->song.artist << "\n";
            else
                cout << "(none)\n";
            cout << "\n  [A] Add highlighted   [R] Remove   [C] Clear   [0] Back\n\n  Your choice: ";
            showCursor(true);
            char c = (char)_getch();
            cout << c << "\n";
            showCursor(false);

            if (c == '0')
                break;
            else if (c == 'a' || c == 'A')
            {
                if (!browseCursor)
                {
                    cout << "  No song highlighted.\n";
                    sleepMs(1100);
                    continue;
                }
                string key = makeKey(browseCursor->song);
                bool exists = false;
                for (auto &k : customPlaylist)
                    if (k == key)
                    {
                        exists = true;
                        break;
                    }
                if (exists)
                    cout << "  Already in playlist.\n";
                else
                {
                    customPlaylist.push_back(key);
                    cout << "  Added: \"" << browseCursor->song.title << "\"\n";
                }
                sleepMs(800);
            }
            else if (c == 'r' || c == 'R')
            {
                if (customPlaylist.empty())
                {
                    cout << "  Playlist is empty.\n";
                    sleepMs(700);
                    continue;
                }
                cout << "  Number to remove (1-" << customPlaylist.size() << "): ";
                showCursor(true);
                int num = 0;
                cin >> num;
                cin.ignore();
                showCursor(false);
                if (num >= 1 && num <= (int)customPlaylist.size())
                {
                    string key = customPlaylist[num - 1];
                    size_t sep = key.find('|');
                    cout << "  Removed: \"" << (sep != string::npos ? key.substr(0, sep) : key) << "\"\n";
                    customPlaylist.erase(customPlaylist.begin() + (num - 1));
                }
                else
                    cout << "  Invalid number.\n";
                sleepMs(800);
            }
            else if (c == 'c' || c == 'C')
            {
                customPlaylist.clear();
                cout << "  Cleared.\n";
                sleepMs(700);
            }
        }
        firstDraw = true;
    }

    //  KTV MODE

    vector<string> wrapLyric(const string &text, int maxWidth)
    {
        vector<string> result;
        if (text.empty())
        {
            result.push_back("");
            return result;
        }
        vector<string> words;
        string word;
        for (int i = 0; i <= (int)text.size(); i++)
        {
            if (i == (int)text.size() || text[i] == ' ')
            {
                if (!word.empty())
                {
                    words.push_back(word);
                    word = "";
                }
            }
            else
                word += text[i];
        }
        string cur;
        for (auto &w : words)
        {
            string cand = cur.empty() ? w : cur + " " + w;
            if ((int)cand.size() <= maxWidth)
            {
                cur = cand;
            }
            else
            {
                if (!cur.empty())
                    result.push_back(cur);
                if ((int)w.size() > maxWidth)
                {
                    string tmp = w;
                    while ((int)tmp.size() > maxWidth)
                    {
                        result.push_back(tmp.substr(0, maxWidth));
                        tmp = tmp.substr(maxWidth);
                    }
                    cur = tmp;
                }
                else
                    cur = w;
            }
        }
        if (!cur.empty())
            result.push_back(cur);
        return result;
    }

    static const int LYRIC_SLOT_ROWS = 4;

    void drawKtvScreen(const string &songTitle, const vector<string> &lyricLines,
                       int elapsed, int total, bool isPausedState, int stageWidth)
    {
        moveCursorHome();
        printRule('=');
        printCentered("KTV  -  " + songTitle);
        printRule('=');
        cout << "\n";
        printCentered("[LEFT/RIGHT] Seek -/+10s   [SPACE] Pause   [Any other key] Exit");
        cout << "\n";

        string stageLine(stageWidth, '-');
        cout << "  " << stageLine << "\n\n";

        int topPad = (LYRIC_SLOT_ROWS - (int)lyricLines.size()) / 2;
        if (topPad < 0)
            topPad = 0;
        int botPad = LYRIC_SLOT_ROWS - (int)lyricLines.size() - topPad;
        if (botPad < 0)
            botPad = 0;

        for (int i = 0; i < topPad; i++)
        {
            clearRestOfLine();
            cout << "\n";
        }
        for (int i = 0; i < (int)lyricLines.size() && i < LYRIC_SLOT_ROWS; i++)
        {
            int pad = (stageWidth - (int)lyricLines[i].size()) / 2;
            if (pad < 0)
                pad = 0;
            cout << "  " << string(pad, ' ') << lyricLines[i];
            clearRestOfLine();
            cout << "\n";
        }
        for (int i = 0; i < botPad; i++)
        {
            clearRestOfLine();
            cout << "\n";
        }

        cout << "\n  " << stageLine << "\n\n";

        int remaining = total - elapsed, barWidth = 30;
        int filled = (total > 0) ? min(elapsed * barWidth / total, barWidth) : 0;
        string status = isPausedState ? "[PAUSED]" : "[PLAYING]";
        string bar = formatTime(elapsed) + " [";
        for (int i = 0; i < barWidth; i++)
            bar += (i < filled) ? '#' : '-';
        bar += "] -" + formatTime(remaining < 0 ? 0 : remaining) + " " + status;
        int pad = (SCREEN_WIDTH - (int)bar.size()) / 2;
        if (pad < 0)
            pad = 0;
        cout << string(pad, ' ') << bar;
        clearRestOfLine();
        cout << "\n";
        printRule('=');
    }

    void ktvMode()
    {
        clearScreen();
        showCursor(false);
        printRule('=');
        printCentered("KTV / LYRICS MODE");
        printRule('=');
        cout << "\n  Available songs:\n\n";
        for (int i = 0; i < KTV_COUNT; i++)
            cout << "  [" << (i + 1) << "] " << ktvLibrary[i].songTitle << "\n";
        cout << "\n  [0] Cancel\n\n  Choose a song: ";
        showCursor(true);
        int choice = 0;
        cin >> choice;
        cin.ignore();
        showCursor(false);

        if (choice < 1 || choice > KTV_COUNT)
        {
            firstDraw = true;
            return;
        }
        int ki = choice - 1;

        LyricsEngine lyrics;
        if (!lyrics.load(ktvLibrary[ki].csvFile))
        {
            cout << "\n  [!] Could not open: " << ktvLibrary[ki].csvFile << "\n\n  Press Enter to return...";
            showCursor(true);
            cin.get();
            showCursor(false);
            firstDraw = true;
            return;
        }

        Node *ktvNode = nullptr;
        if (head)
        {
            Node *node = head;
            string sf = toLower(ktvLibrary[ki].songTitle);
            do
            {
                if (toLower(node->song.title).find(sf) != string::npos)
                {
                    ktvNode = node;
                    break;
                }
                node = node->next;
            } while (node != head);
        }

        if (!ktvNode)
        {
            cout << "\n  [!] Song not found in playlist: \"" << ktvLibrary[ki].songTitle << "\"\n\n  Press Enter to return...";
            showCursor(true);
            cin.get();
            showCursor(false);
            firstDraw = true;
            return;
        }

        startPlaying(ktvNode);
        int stageWidth = SCREEN_WIDTH - 4;
        string lastLine;
        vector<string> lastWrapped;
        bool ktvPaused = false;
        clearScreen();

        while (audio.isPlaying() || audio.isPaused())
        {
            int elapsed = audio.elapsedSeconds(), total = ktvNode->song.durationSeconds();
            string curLine = lyrics.getLine(elapsed);
            if (curLine != lastLine)
            {
                lastLine = curLine;
                lastWrapped = wrapLyric(curLine, stageWidth);
            }
            drawKtvScreen(ktvLibrary[ki].songTitle, lastWrapped, elapsed, total, ktvPaused, stageWidth);

            if (_kbhit())
            {
                int k = _getch();
                if (k == 0 || k == 224)
                {
                    int k2 = _getch();
                    if (k2 == 75)
                    {
                        audio.seek(audio.elapsedSeconds() - 10);
                        lastLine = "";
                        continue;
                    }
                    if (k2 == 77)
                    {
                        int p = audio.elapsedSeconds() + 10;
                        if (total > 0 && p >= total)
                            p = total - 2;
                        audio.seek(p);
                        lastLine = "";
                        continue;
                    }
                    continue;
                }
                if (k == ' ')
                {
                    if (audio.isPaused())
                    {
                        audio.resume();
                        ktvPaused = false;
                    }
                    else
                    {
                        audio.pause();
                        ktvPaused = true;
                    }
                    continue;
                }
                break;
            }
            sleepMs(200);
        }
        stopPlaying();
        firstDraw = true;
    }

public:
    MusicPlayer() : head(nullptr), current(nullptr), browseCursor(nullptr), paused(false), firstDraw(true)
    {
        srand((unsigned)time(nullptr));
        csv.ensureCSV();
        loadFromCSV();
    }

    void loadFromCSV()
    {
        Song songs[MAX_SONGS];
        int count = csv.loadSongs(songs, MAX_SONGS);
        for (int i = 0; i < count; i++)
            insertAtEnd(songs[i]);
        if (head && !browseCursor)
            browseCursor = head;
    }

    void run()
    {
        showCursor(false);
        while (true)
        {
            drawScreen();
            bool keyPressed = false;
            for (int i = 0; i < 5 && !keyPressed; i++)
            {
                if (_kbhit())
                    keyPressed = true;
                else
                    sleepMs(100);
            }
            if (!keyPressed)
                continue;

            int key = _getch();
            if (key == 0 || key == 224)
            {
                int k2 = _getch();
                if (k2 == 72)
                {
                    if (browseCursor)
                        browseCursor = browseCursor->prev;
                }
                else if (k2 == 80)
                {
                    if (browseCursor)
                        browseCursor = browseCursor->next;
                }
                else if (k2 == 75)
                {
                    if (current && (audio.isPlaying() || audio.isPaused()))
                        audio.seek(audio.elapsedSeconds() - 10);
                }
                else if (k2 == 77)
                {
                    if (current && (audio.isPlaying() || audio.isPaused()))
                    {
                        int np = audio.elapsedSeconds() + 10, sl = current->song.durationSeconds();
                        if (sl > 0 && np >= sl)
                            np = sl - 2;
                        audio.seek(np);
                    }
                }
                continue;
            }

            switch (key)
            {
            case 13:
                if (browseCursor)
                    startPlaying(browseCursor);
                break;
            case ' ':
                togglePause();
                break;
            case 'a':
            case 'A':
                showCursor(true);
                addSong();
                showCursor(false);
                if (!browseCursor && head)
                    browseCursor = head;
                break;
            case '7':
                deleteSong();
                break;
            case 's':
            case 'S':
                stopPlaying();
                break;
            case 'n':
            case 'N':
            {
                Node *nx = current ? current->next : (head ? head : nullptr);
                if (nx)
                    startPlaying(nx);
                break;
            }
            case 'b':
            case 'B':
            {
                Node *pv = current ? current->prev : (head ? head->prev : nullptr);
                if (pv)
                    startPlaying(pv);
                break;
            }
            case 'r':
            case 'R':
            {
                if (!head)
                    break;
                int n = countSongs(), ri = rand() % n;
                Node *nd = head;
                for (int i = 0; i < ri; i++)
                    nd = nd->next;
                startPlaying(nd);
                break;
            }
            case 'h':
            case 'H':
                shuffleList();
                break;
            case 'f':
            case 'F':
                searchMenu();
                break;
            case 'o':
            case 'O':
                sortMenu();
                break;
            case 'p':
            case 'P':
                playlistMenu();
                break;
            case 'k':
            case 'K':
                ktvMode();
                break;
            case 'w':
            case 'W':
                history.showWrapped();
                firstDraw = true;
                break;
            case 'q':
            case 'Q':
            case 27:
                stopPlaying();
                clearScreen();
                showCursor(true);
                cout << "Goodbye!\n";
                return;
            }
        }
    }
};

//  MAIN

int main()
{
    MusicPlayer player;
    player.run();
    return 0;
}