/*
 * ============================================================
 *   DuckDuckPlay - SFML 3.x Edition
 *   YouTube Music-inspired console UI
 *
 *   Features:
 *   1. Browse songs with UP/DOWN arrow keys
 *   2. LEFT/RIGHT arrow keys seek -10 or +10 seconds
 *   3. Pause, Stop, Next, Previous, Random
 *   4. Shuffle (keeps current song at the top)
 *   5. Custom Playlist (add/remove songs with [P])
 *   6. Search and Sort
 *   7. KTV / Lyrics Mode
 *   8. Play history / Wrapped summary
 *   9. Add and Delete songs
 * ============================================================
 *
 *  HOW TO COMPILE (Windows + MinGW, SFML 3.x):
 *    g++ duckduckplay.cpp -o duckduckplay.exe
 *        -I"C:\SFML\include"
 *        -L"C:\SFML\lib"
 *        -lsfml-audio -lsfml-system
 *
 *  FILES NEEDED (same folder as the .exe):
 *    playlist.csv              -- your song library (auto-created)
 *    history.csv               -- auto-created when songs are played
 *    sfml-audio-3.dll, sfml-system-3.dll, openal32.dll
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

// ─────────────────────────────────────────────
//  CONSTANTS
// ─────────────────────────────────────────────

const string CSV_FILE        = "playlist.csv";
const string HISTORY_FILE    = "history.csv";
const string EXPECTED_HEADER = "Title,Artist,Album,Genre,Year,Duration,FilePath";
const string HISTORY_HEADER  = "Title,Artist,Genre,Timestamp";
const int MAX_SONGS     = 500;
const int MAX_FIELDS    = 7;
const int VIEWPORT_SIZE = 10;
const int SCREEN_WIDTH  = 78;

// ─────────────────────────────────────────────
//  YOUTUBE MUSIC COLOR PALETTE  (ANSI codes)
// ─────────────────────────────────────────────

namespace C {
    const string RESET  = "\033[0m";
    const string BOLD   = "\033[1m";
    const string DIM    = "\033[2m";
    // YouTube Music palette
    const string RED    = "\033[91m";   // YT red accent  — now-playing, logo
    const string WHITE  = "\033[97m";   // primary text
    const string GRAY   = "\033[90m";   // subdued / hints
    const string SILVER = "\033[37m";   // secondary text
    const string CYAN   = "\033[96m";   // browse-cursor highlight
    const string YELLOW = "\033[93m";   // playing + browsed (both)
    const string GREEN  = "\033[92m";   // status: PLAYING
    const string ORANGE = "\033[33m";   // status: PAUSED
}

// Enable ANSI escape sequences & UTF-8 on Windows
void initConsole()
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode))
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
    // Wider console window for 78-char layout
    system("mode con: cols=90 lines=35");
}

// ─────────────────────────────────────────────
//  KTV LIBRARY
// ─────────────────────────────────────────────

struct KtvEntry {
    string songTitle;
    string csvFile;
};

KtvEntry ktvLibrary[] = {
    {"Shape of My Heart",      "lyrics/shape_of_my_heart.csv"},
    {"Incomplete",             "lyrics/incomplete.csv"},
    {"Knocks Me Off My Feet",  "lyrics/knocks_me_off_my_feet.csv"},
    {"Alipin",                 "lyrics/alipin.csv"}
};
const int KTV_COUNT = sizeof(ktvLibrary) / sizeof(ktvLibrary[0]);

// ─────────────────────────────────────────────
//  UTILITY FUNCTIONS
// ─────────────────────────────────────────────

void clearScreen()   { system("cls"); }
void sleepMs(int ms) { Sleep(ms); }

void moveCursorHome()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos = {0, 0};
    SetConsoleCursorPosition(h, pos);
}

void clearRestOfLine()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(h, &info))
    {
        COORD cur = info.dwCursorPosition;
        DWORD n, w = info.dwSize.X - cur.X;
        FillConsoleOutputCharacterA(h, ' ', w, cur, &n);
        SetConsoleCursorPosition(h, cur);
    }
}

void showCursor(bool visible)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(h, &ci);
    ci.bVisible = visible ? TRUE : FALSE;
    SetConsoleCursorInfo(h, &ci);
}

string truncate(const string &text, int maxLen)
{
    if ((int)text.size() <= maxLen) return text;
    return text.substr(0, maxLen - 3) + "...";
}

string padRight(const string &text, int width)
{
    if ((int)text.size() >= width) return text.substr(0, width);
    return text + string(width - text.size(), ' ');
}

string toLower(string text)
{
    for (char &c : text) c = (char)tolower(c);
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

// ─────────────────────────────────────────────
//  SONG
// ─────────────────────────────────────────────

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
        size_t c = duration.find(':');
        if (c == string::npos) return 0;
        return stoi(duration.substr(0, c)) * 60 + stoi(duration.substr(c + 1));
    }
};

// ─────────────────────────────────────────────
//  NODE
// ─────────────────────────────────────────────

class Node
{
public:
    Song song;
    Node *next, *prev;
    Node(Song s) : song(s), next(nullptr), prev(nullptr) {}
};

// ─────────────────────────────────────────────
//  LYRIC LINE
// ─────────────────────────────────────────────

struct LyricLine {
    string verse;
    int startSec;
    int endSec; // -1 = until end of song
};

// ─────────────────────────────────────────────
//  HISTORY MANAGER
// ─────────────────────────────────────────────

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
            if (first.find("Title") != string::npos) hasHeader = true;
            checkFile.close();
        }
        ofstream out(HISTORY_FILE, ios::app);
        if (!out.is_open()) return;
        if (!hasHeader) out << HISTORY_HEADER << "\n";
        out << s.title << "," << s.artist << "," << s.genre << "," << buf << "\n";
        out.close();
    }

    void showWrapped()
    {
        ifstream in(HISTORY_FILE);
        if (!in.is_open())
        {
            cout << C::GRAY << "\n  No play history found yet.\n" << C::RESET;
            return;
        }

        map<string,int> songCount, artistCount, genreCount;
        string line;
        getline(in, line);
        int total = 0;

        while (getline(in, line))
        {
            if (line.empty()) continue;
            vector<string> fields;
            stringstream ss(line);
            string tok;
            while (getline(ss, tok, ',')) fields.push_back(trim(tok));
            if (fields.size() < 3) continue;
            songCount[fields[0]]++;
            artistCount[fields[1]]++;
            genreCount[fields[2]]++;
            total++;
        }
        in.close();

        if (total == 0)
        {
            cout << C::GRAY << "\n  No plays recorded yet.\n" << C::RESET;
            return;
        }

        string topSong, topArtist, topGenre;
        int topSC = 0, topAC = 0, topGC = 0;
        for (auto &e : songCount)   if (e.second > topSC) { topSong   = e.first; topSC = e.second; }
        for (auto &e : artistCount) if (e.second > topAC) { topArtist = e.first; topAC = e.second; }
        for (auto &e : genreCount)  if (e.second > topGC) { topGenre  = e.first; topGC = e.second; }

        clearScreen();
        // ── Wrapped Header ──
        string dbl(SCREEN_WIDTH, '\xcd'); // ASCII fallback double line
        cout << C::RED << C::BOLD;
        cout << string(SCREEN_WIDTH, '=') << "\n";
        cout << "         YOUR MDZ WRAPPED SUMMARY\n";
        cout << string(SCREEN_WIDTH, '=') << C::RESET << "\n\n";

        cout << C::GRAY << "  Total plays recorded" << C::RESET
             << C::WHITE << " : " << C::YELLOW << C::BOLD << total << C::RESET << "\n\n";

        cout << C::GRAY  << "  Most Played Song   " << C::RESET
             << C::WHITE << " : " << C::CYAN  << "\"" << topSong   << "\""
             << C::GRAY  << " (" << topSC << " plays)\n" << C::RESET;
        cout << C::GRAY  << "  Favorite Artist    " << C::RESET
             << C::WHITE << " : " << C::CYAN  << topArtist
             << C::GRAY  << " (" << topAC << " plays)\n" << C::RESET;
        cout << C::GRAY  << "  Top Genre          " << C::RESET
             << C::WHITE << " : " << C::CYAN  << topGenre
             << C::GRAY  << " (" << topGC << " plays)\n\n" << C::RESET;

        // Sort songs by play count
        vector<pair<string,int>> sorted(songCount.begin(), songCount.end());
        for (int i = 0; i < (int)sorted.size()-1; i++)
            for (int j = 0; j < (int)sorted.size()-1-i; j++)
                if (sorted[j].second < sorted[j+1].second) swap(sorted[j], sorted[j+1]);

        cout << C::GRAY << "  " << string(SCREEN_WIDTH-4, '-') << "\n";
        cout << "  All Song Plays\n";
        cout << "  " << string(SCREEN_WIDTH-4, '-') << C::RESET << "\n";
        for (auto &p : sorted)
            cout << C::SILVER << "    " << C::YELLOW << p.second << "x"
                 << C::WHITE  << "  " << p.first << "\n" << C::RESET;

        cout << "\n" << C::RED << string(SCREEN_WIDTH, '=') << C::RESET
             << "\n\n" << C::GRAY << "  Press Enter to return..." << C::RESET;
        showCursor(true);
        cin.get();
        showCursor(false);
    }
};

// ─────────────────────────────────────────────
//  LYRICS ENGINE
// ─────────────────────────────────────────────

class LyricsEngine
{
    vector<LyricLine> lines;

    int parseTime(const string &t)
    {
        if (t == "END" || t == "end") return -1;
        size_t c = t.find(':');
        if (c == string::npos) return 0;
        return stoi(t.substr(0, c)) * 60 + stoi(t.substr(c + 1));
    }

public:
    bool load(const string &path)
    {
        lines.clear();
        ifstream in(path);
        if (!in.is_open()) return false;
        string line;
        getline(in, line); // skip header
        while (getline(in, line))
        {
            if (line.empty()) continue;
            vector<string> f;
            stringstream ss(line);
            string tok;
            while (getline(ss, tok, ',')) f.push_back(trim(tok));
            if (f.size() < 3) continue;
            LyricLine ll;
            ll.verse    = f[0];
            ll.startSec = parseTime(f[1]);
            ll.endSec   = parseTime(f[2]);
            lines.push_back(ll);
        }
        return !lines.empty();
    }

    string getLine(int elapsed) const
    {
        for (auto &l : lines)
        {
            int end = (l.endSec == -1) ? 99999 : l.endSec;
            if (elapsed >= l.startSec && elapsed < end) return l.verse;
        }
        return "";
    }

    bool loaded() const { return !lines.empty(); }
};

// ─────────────────────────────────────────────
//  AUDIO ENGINE  (SFML 3.x compatible)
// ─────────────────────────────────────────────

class AudioEngine
{
    sf::Music music;
    string loadedPath;

public:
    bool load(const string &path)
    {
        if (loadedPath == path && music.getStatus() != sf::Music::Status::Stopped)
            return true;
        music.stop();
        if (!music.openFromFile(path))
        {
            cout << C::RED << "  [!] Cannot open audio file: " << path << C::RESET << "\n";
            return false;
        }
        loadedPath = path;
        return true;
    }

    void play()   { music.play(); }
    void pause()  { music.pause(); }
    void resume() { music.play(); }

    void stop()
    {
        music.stop();
        loadedPath = "";
    }

    void seek(int newSeconds)
    {
        if (newSeconds < 0) newSeconds = 0;
        music.setPlayingOffset(sf::seconds((float)newSeconds));
    }

    bool isPlaying() const { return music.getStatus() == sf::Music::Status::Playing; }
    bool isPaused()  const { return music.getStatus() == sf::Music::Status::Paused;  }
    bool isStopped() const { return music.getStatus() == sf::Music::Status::Stopped; }

    int elapsedSeconds() const { return (int)music.getPlayingOffset().asSeconds(); }
};

// ─────────────────────────────────────────────
//  CSV MANAGER
// ─────────────────────────────────────────────

class CSVManager
{
public:
    bool parseLine(string line, string fields[], int fieldCount)
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        int idx = 0;
        string tok;
        for (int i = 0; i <= (int)line.size(); i++)
        {
            if (i == (int)line.size() || line[i] == ',')
            {
                if (idx >= fieldCount) return false;
                fields[idx++] = tok;
                tok = "";
            }
            else tok += line[i];
        }
        return idx == fieldCount;
    }

    bool isDuplicate(const string &title, const string &artist)
    {
        ifstream in(CSV_FILE);
        if (!in.is_open()) return false;
        string line;
        getline(in, line);
        while (getline(in, line))
        {
            if (line.empty()) continue;
            string f[MAX_FIELDS];
            if (!parseLine(line, f, MAX_FIELDS)) continue;
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
            if (first.find("Title") != string::npos) hasHeader = true;
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
                if (last != '\n') { ofstream fix(CSV_FILE, ios::app | ios::binary); fix << "\n"; }
            }
        }
        ofstream out(CSV_FILE, ios::app);
        if (!out.is_open()) return;
        out << s.title   << "," << s.artist << "," << s.album  << ","
            << s.genre   << "," << s.year   << "," << s.duration << ","
            << s.filePath << "\n";
        out.close();
    }

    int loadSongs(Song songs[], int maxSongs)
    {
        ifstream in(CSV_FILE);
        if (!in.is_open()) return 0;
        string line;
        getline(in, line);
        int count = 0;
        while (getline(in, line) && count < maxSongs)
        {
            if (line.empty()) continue;
            string f[MAX_FIELDS];
            if (!parseLine(line, f, MAX_FIELDS)) continue;
            try { songs[count++] = Song(f[0], f[1], f[2], f[3], stoi(f[4]), f[5], f[6]); }
            catch (...) {}
        }
        in.close();
        return count;
    }

    void rewriteAll(Node *head)
    {
        if (!head) { ofstream o(CSV_FILE); o << EXPECTED_HEADER << "\n"; return; }
        ofstream out(CSV_FILE);
        if (!out.is_open()) return;
        out << EXPECTED_HEADER << "\n";
        Node *cur = head;
        do {
            Song &s = cur->song;
            out << s.title << "," << s.artist << "," << s.album << ","
                << s.genre << "," << s.year   << "," << s.duration << ","
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
            if (trim(first) == EXPECTED_HEADER) return;
        }
        ofstream out(CSV_FILE);
        out << EXPECTED_HEADER << "\n";
        out.close();
    }
};

// ─────────────────────────────────────────────
//  MUSIC PLAYER
// ─────────────────────────────────────────────

class MusicPlayer
{
    Node *head, *current, *browseCursor;
    bool paused, firstDraw;

    AudioEngine    audio;
    CSVManager     csv;
    HistoryManager history;
    vector<string> customPlaylist;

    string makeKey(const Song &s) { return s.title + "|" + s.artist; }

    int countSongs() const
    {
        if (!head) return 0;
        int n = 0;
        Node *node = head;
        do { n++; node = node->next; } while (node != head);
        return n;
    }

    void insertAtEnd(Song s)
    {
        Node *n = new Node(s);
        if (!head) { n->next = n->prev = n; head = n; return; }
        Node *tail = head->prev;
        tail->next = n; n->prev = tail; n->next = head; head->prev = n;
    }

    Node *removeNode(Node *n)
    {
        if (!n) return nullptr;
        if (countSongs() == 1) { delete n; head = nullptr; return nullptr; }
        Node *next = n->next;
        n->prev->next = n->next; n->next->prev = n->prev;
        if (head == n) head = next;
        delete n;
        return next;
    }

    void shuffleList()
    {
        int total = countSongs();
        if (total <= 1) return;
        string playTitle, playArtist;
        if (current) { playTitle = current->song.title; playArtist = current->song.artist; }

        vector<Node*> nodes;
        Node *node = head;
        do { nodes.push_back(node); node = node->next; } while (node != head);

        for (int i = total-1; i > 0; i--)
            swap(nodes[i]->song, nodes[rand()%(i+1)]->song);

        if (!playTitle.empty())
        {
            node = head;
            do {
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
        if (total <= 1) return;
        vector<Node*> nodes;
        Node *node = head;
        do { nodes.push_back(node); node = node->next; } while (node != head);

        for (int i = 0; i < total-1; i++)
        {
            int best = i;
            for (int j = i+1; j < total; j++)
            {
                bool smaller = false;
                if      (key == 0) smaller = toLower(nodes[j]->song.title)  < toLower(nodes[best]->song.title);
                else if (key == 1) smaller = toLower(nodes[j]->song.artist) < toLower(nodes[best]->song.artist);
                else if (key == 2) smaller = nodes[j]->song.year            < nodes[best]->song.year;
                else               smaller = nodes[j]->song.durationSeconds()< nodes[best]->song.durationSeconds();
                if (smaller) best = j;
            }
            if (best != i) swap(nodes[i]->song, nodes[best]->song);
        }
    }

    Node *searchSong(const string &query)
    {
        if (!head || query.empty()) return nullptr;
        string lq = toLower(query);
        Node *node = head;
        do {
            if (toLower(node->song.title ).find(lq) != string::npos ||
                toLower(node->song.artist).find(lq) != string::npos ||
                toLower(node->song.genre ).find(lq) != string::npos)
                return node;
            node = node->next;
        } while (node != head);
        return nullptr;
    }

    string totalRuntime()
    {
        if (!head) return "0m 0s";
        int secs = 0;
        Node *node = head;
        do { secs += node->song.durationSeconds(); node = node->next; } while (node != head);
        int h = secs/3600, m = (secs%3600)/60, s = secs%60;
        string r = "";
        if (h > 0) r += to_string(h) + "h ";
        return r + to_string(m) + "m " + to_string(s) + "s";
    }

    string formatTime(int secs) const
    {
        if (secs < 0) secs = 0;
        string m = to_string(secs/60), s = to_string(secs%60);
        if (m.size() < 2) m = "0" + m;
        if (s.size() < 2) s = "0" + s;
        return m + ":" + s;
    }

    // ── YT-Music styled dividers ──────────────────

    void printDoubleRule()  // red double-line  ══════
    {
        cout << C::RED << string(SCREEN_WIDTH, '=') << C::RESET;
        clearRestOfLine();
        cout << "\n";
    }

    void printSingleRule()  // gray single-line  ──────
    {
        cout << C::GRAY << string(SCREEN_WIDTH, '-') << C::RESET;
        clearRestOfLine();
        cout << "\n";
    }

    void printCentered(const string &text, const string &color = "")
    {
        int pad = (SCREEN_WIDTH - (int)text.size()) / 2;
        if (pad < 0) pad = 0;
        cout << color << string(pad, ' ') << text << C::RESET;
        clearRestOfLine();
        cout << "\n";
    }

    // ── Song row  (YouTube Music list style) ─────

    string buildSongRow(int index, Node *node, bool isBrowsed, bool isCurrent)
    {
        // Choose row color and marker
        string rowColor, marker;
        if      (isBrowsed && isCurrent) { rowColor = C::YELLOW; marker = " >* "; }
        else if (isCurrent)              { rowColor = C::RED;    marker = " >  "; }
        else if (isBrowsed)              { rowColor = C::CYAN;   marker = " >> "; }
        else                             { rowColor = C::SILVER; marker = "    "; }

        // Number column (right-aligned in 3 chars)
        string num = to_string(index + 1);
        while ((int)num.size() < 3) num = " " + num;
        num += ". ";

        string dur = "[" + node->song.duration + "]";
        // Pointer at end for browse cursor
        string endMark = isBrowsed ? " <" : "  ";

        // Budget for title / artist
        int used   = 4 + (int)num.size() + 2 + (int)dur.size() + (int)endMark.size();
        int budget = SCREEN_WIDTH - used;
        if (budget < 8) budget = 8;
        int tMax = budget * 55 / 100;
        int aMax = budget - tMax - 3; // " - " separator

        string title  = padRight(truncate(node->song.title,  tMax), tMax);
        string artist = padRight(truncate(node->song.artist, aMax), aMax);

        // Assemble plain row (no escape sequences for length calc)
        string row = marker + num + title + " - " + artist + "  " + dur + endMark;
        // Pad/trim to SCREEN_WIDTH
        if ((int)row.size() < SCREEN_WIDTH)
            row += string(SCREEN_WIDTH - row.size(), ' ');
        else if ((int)row.size() > SCREEN_WIDTH)
            row = row.substr(0, SCREEN_WIDTH);

        return rowColor + row + C::RESET;
    }

    // ── Progress bar using block chars ───────────

    string buildProgressBar(int elapsed, int total, bool isPaused, int barWidth = 38)
    {
        int filled = (total > 0) ? min(elapsed * barWidth / total, barWidth) : 0;
        int remaining = total - elapsed;

        string bar = "";
        // Filled portion in red
        bar += C::RED;
        for (int i = 0; i < filled; i++)  bar += "\xe2\x96\x88"; // █
        // Empty portion in gray
        bar += C::GRAY;
        for (int i = filled; i < barWidth; i++) bar += "\xe2\x96\x91"; // ░
        bar += C::RESET;

        string statusColor = isPaused ? C::ORANGE : C::GREEN;
        string statusText  = isPaused ? " PAUSED " : " PLAYING";

        return C::SILVER + formatTime(elapsed) + "  " + C::RESET
             + bar + "  "
             + C::SILVER + "-" + formatTime(remaining < 0 ? 0 : remaining) + C::RESET
             + "  " + statusColor + C::BOLD + "[" + statusText + "]" + C::RESET;
    }

    // ── Main screen (YouTube Music layout) ───────

    void drawScreen()
    {
        if (firstDraw) { clearScreen(); firstDraw = false; }
        else           moveCursorHome();

        // ┌─ Header ───────────────────────────────────┐
        printDoubleRule();
        cout << C::RED << C::BOLD;
        printCentered("MDZ MUSIC", C::RED + C::BOLD);
        printDoubleRule();

        // ┌─ Now Playing section ───────────────────────┐
        if (current && (audio.isPlaying() || audio.isPaused()))
        {
            int elapsed = audio.elapsedSeconds();
            int total   = current->song.durationSeconds();

            // Song info line
            string songInfo = truncate(current->song.title,  28) + "  "
                            + C::GRAY + "\xe2\x80\xa2" + C::WHITE + "  "
                            + truncate(current->song.artist, 18) + "  "
                            + C::GRAY + "\xe2\x80\xa2" + "  "
                            + current->song.genre;
            cout << "  " << C::RED << C::BOLD << "> " << C::RESET
                 << C::WHITE << C::BOLD << truncate(current->song.title, 28) << C::RESET
                 << C::GRAY  << "  *  " << C::RESET
                 << C::SILVER << truncate(current->song.artist, 20) << C::RESET;
            if (!current->song.genre.empty())
                cout << C::GRAY << "  *  " << truncate(current->song.genre, 12) << C::RESET;
            if (current->song.year > 0)
                cout << C::GRAY << "  *  " << current->song.year << C::RESET;
            clearRestOfLine();
            cout << "\n";

            // Progress bar
            cout << "  " << buildProgressBar(elapsed, total, paused);
            clearRestOfLine();
            cout << "\n";
        }
        else
        {
            cout << "  " << C::GRAY << "No song playing.  Press " << C::RESET
                 << C::CYAN << "[ENTER]" << C::RESET
                 << C::GRAY << " to start or " << C::RESET
                 << C::CYAN << "[A]" << C::RESET
                 << C::GRAY << " to add songs." << C::RESET;
            clearRestOfLine();
            cout << "\n\n";
        }

        // ┌─ Library header ───────────────────────────┐
        printSingleRule();
        cout << "  " << C::BOLD << C::WHITE << "LIBRARY" << C::RESET
             << C::GRAY << "   " << countSongs() << " songs"
             << "   *   " << totalRuntime() << C::RESET;
        clearRestOfLine();
        cout << "\n";
        printSingleRule();

        // ┌─ Song list ────────────────────────────────┐
        if (!head)
        {
            cout << "  " << C::GRAY << "Your library is empty." << C::RESET
                 << "  Press " << C::CYAN << "[A]" << C::RESET << " to add songs.\n";
        }
        else
        {
            int total    = countSongs();
            int browseIdx = 0, idx = 0;
            Node *node = head;
            do {
                if (node == browseCursor) browseIdx = idx;
                idx++;
                node = node->next;
            } while (node != head);

            int vs = browseIdx - VIEWPORT_SIZE / 2;
            if (vs < 0) vs = 0;
            if (vs + VIEWPORT_SIZE > total) vs = total - VIEWPORT_SIZE;
            if (vs < 0) vs = 0;
            int ve = min(vs + VIEWPORT_SIZE, total);

            if (vs > 0)
                cout << C::GRAY << "    ... " << vs << " more above ...\n" << C::RESET;

            node = head;
            for (int i = 0; i < vs; i++) node = node->next;
            for (int i = vs; i < ve; i++)
            {
                cout << buildSongRow(i, node, node == browseCursor, current && node == current);
                clearRestOfLine();
                cout << "\n";
                node = node->next;
            }

            int below = total - ve;
            if (below > 0)
                cout << C::GRAY << "    ... " << below << " more below ...\n" << C::RESET;
        }

        // ┌─ Controls ────────────────────────────────┐
        printSingleRule();
        cout << C::GRAY;
        printCentered("[UP/DOWN] Browse   [LEFT/RIGHT] Seek -/+10s   [ENTER] Play   [SPACE] Pause");
        printCentered("[N] Next  [B] Prev  [S] Stop  [R] Random  [H] Shuffle  [A] Add  [7] Delete");
        printCentered("[F] Search  [O] Sort  [P] Playlist  [K] KTV Mode  [W] Wrapped  [Q] Quit");
        cout << C::RESET;
        printDoubleRule();
    }

    // ── Sub-menu colored header ───────────────────

    void drawMenuHeader(const string &title)
    {
        clearScreen();
        printDoubleRule();
        printCentered(title, C::RED + C::BOLD);
        printDoubleRule();
        cout << "\n";
    }

    // ── Playback ──────────────────────────────────

    void startPlaying(Node *node)
    {
        if (!node) return;
        audio.stop();
        current = node;
        paused  = false;
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
        paused  = false;
    }

    void togglePause()
    {
        if (!current) return;
        if (audio.isPaused())   { audio.resume(); paused = false; }
        else if (audio.isPlaying()) { audio.pause();  paused = true;  }
    }

    // ── Add Song ──────────────────────────────────

    void addSong()
    {
        drawMenuHeader("ADD SONG");
        showCursor(true);
        cout << C::GRAY << "  Enter the full path to the audio file:\n" << C::RESET
             << "  e.g.  C:\\Music\\song.mp3\n\n";
        string filePath;
        cout << "  " << C::CYAN << "File path : " << C::RESET;
        getline(cin, filePath);
        filePath = trim(filePath);

        // Auto-detect title from filename
        string guess = filePath;
        size_t sl = guess.find_last_of("/\\");
        if (sl != string::npos) guess = guess.substr(sl + 1);
        size_t dot = guess.rfind('.');
        if (dot != string::npos) guess = guess.substr(0, dot);
        for (char &c : guess) if (c == '_') c = ' ';

        cout << "\n  " << C::GRAY << "Auto-detected title: " << C::RESET
             << C::SILVER << "\"" << guess << "\"\n" << C::RESET;
        cout << "  " << C::CYAN << "Title (Enter to keep): " << C::RESET;
        string t;
        getline(cin, t);
        if (!t.empty()) guess = trim(t);

        cout << "  " << C::CYAN << "Artist   : " << C::RESET; string artist; getline(cin, artist); artist = trim(artist);

        if (csv.isDuplicate(guess, artist))
        {
            cout << "\n  " << C::RED << "[!] This song is already in the library!" << C::RESET
                 << "\n\n  " << C::GRAY << "Press Enter to go back..." << C::RESET;
            cin.get();
            showCursor(false);
            return;
        }

        cout << "  " << C::CYAN << "Album    : " << C::RESET; string album; getline(cin, album); album = trim(album);
        cout << "  " << C::CYAN << "Genre    : " << C::RESET; string genre; getline(cin, genre); genre = trim(genre);
        cout << "  " << C::CYAN << "Year     : " << C::RESET; int year = 0; cin >> year; cin.ignore();
        cout << "  " << C::CYAN << "Duration (MM:SS): " << C::RESET; string dur; getline(cin, dur); dur = trim(dur);

        Song ns(guess, artist, album, genre, year, dur, filePath);
        insertAtEnd(ns);
        csv.saveSong(ns);
        if (!browseCursor) browseCursor = head;

        cout << "\n  " << C::GREEN << C::BOLD << "[OK]" << C::RESET
             << C::WHITE << " Added: \"" << guess << "\"\n" << C::RESET
             << "\n  " << C::GRAY << "Press Enter to continue..." << C::RESET;
        cin.get();
        showCursor(false);
        firstDraw = true;
    }

    // ── Delete Song ───────────────────────────────

    void deleteSong()
    {
        if (!browseCursor)
        {
            cout << "  " << C::GRAY << "No song selected.\n" << C::RESET;
            sleepMs(800);
            return;
        }
        drawMenuHeader("DELETE SONG");
        showCursor(false);
        cout << "  " << C::WHITE << "\"" << browseCursor->song.title << "\""
             << C::GRAY << "  by  " << C::RESET
             << C::SILVER << browseCursor->song.artist << "\n\n" << C::RESET;
        cout << "  " << C::RED << "Are you sure? Press Y to confirm, any other key to cancel: " << C::RESET;
        showCursor(true);
        char c = (char)_getch();
        cout << c << "\n";
        showCursor(false);

        if (c != 'y' && c != 'Y')
        {
            cout << "\n  " << C::GRAY << "Cancelled.\n" << C::RESET;
            sleepMs(600);
            firstDraw = true;
            return;
        }

        if (current == browseCursor) stopPlaying();
        string key = makeKey(browseCursor->song);
        for (int i = 0; i < (int)customPlaylist.size(); i++)
            if (customPlaylist[i] == key) { customPlaylist.erase(customPlaylist.begin()+i); break; }

        browseCursor = removeNode(browseCursor);
        if (!browseCursor && head) browseCursor = head;
        csv.rewriteAll(head);
        cout << "\n  " << C::GREEN << "[OK] Song removed.\n" << C::RESET;
        sleepMs(700);
        firstDraw = true;
    }

    // ── Search ────────────────────────────────────

    void searchMenu()
    {
        drawMenuHeader("SEARCH");
        showCursor(true);
        cout << "  " << C::GRAY << "Search by title, artist, or genre:\n\n" << C::RESET;
        cout << "  " << C::CYAN << "> " << C::RESET;
        string q;
        getline(cin, q);
        Node *found = searchSong(q);
        if (found)
        {
            browseCursor = found;
            cout << "\n  " << C::GREEN << C::BOLD << "Found:" << C::RESET
                 << C::WHITE << " \"" << found->song.title << "\""
                 << C::GRAY  << " by " << C::RESET
                 << C::SILVER << found->song.artist << "\n" << C::RESET;
        }
        else
            cout << "\n  " << C::RED << "No match found for \"" << q << "\".\n" << C::RESET;
        cout << "\n  " << C::GRAY << "Press Enter to return..." << C::RESET;
        cin.get();
        showCursor(false);
        firstDraw = true;
    }

    // ── Sort ──────────────────────────────────────

    void sortMenu()
    {
        drawMenuHeader("SORT LIBRARY");
        showCursor(true);
        cout << C::SILVER;
        cout << "  " << C::CYAN << "[1]" << C::SILVER << "  Title A-Z\n";
        cout << "  " << C::CYAN << "[2]" << C::SILVER << "  Artist A-Z\n";
        cout << "  " << C::CYAN << "[3]" << C::SILVER << "  Year (oldest first)\n";
        cout << "  " << C::CYAN << "[4]" << C::SILVER << "  Duration (shortest first)\n";
        cout << "  " << C::GRAY << "[0]" << C::GRAY   << "  Cancel\n" << C::RESET;
        cout << "\n  " << C::CYAN << "Your choice: " << C::RESET;
        char c = (char)_getch();
        cout << c << "\n";
        int key = -1;
        if (c == '1') key = 0;
        else if (c == '2') key = 1;
        else if (c == '3') key = 2;
        else if (c == '4') key = 3;
        if (key >= 0)
        {
            selectionSort(key);
            browseCursor = head;
            cout << "\n  " << C::GREEN << C::BOLD << "[OK]" << C::RESET
                 << C::WHITE << " Library sorted!\n" << C::RESET;
        }
        sleepMs(700);
        showCursor(false);
        firstDraw = true;
    }

    // ── Custom Playlist ───────────────────────────

    void playlistMenu()
    {
        while (true)
        {
            drawMenuHeader("CUSTOM PLAYLIST");
            showCursor(false);
            cout << C::GRAY << "  " << customPlaylist.size() << " song(s) in playlist\n\n" << C::RESET;

            if (customPlaylist.empty())
                cout << "  " << C::GRAY << "(No songs added yet.)\n" << C::RESET;
            else
                for (int i = 0; i < (int)customPlaylist.size(); i++)
                {
                    string key = customPlaylist[i];
                    size_t sep = key.find('|');
                    cout << "  " << C::CYAN << (i+1) << C::RESET
                         << C::WHITE << ".  \"" << (sep!=string::npos ? key.substr(0,sep) : key) << "\""
                         << C::GRAY  << "  by  " << C::RESET
                         << C::SILVER << (sep!=string::npos ? key.substr(sep+1) : "") << "\n" << C::RESET;
                }

            cout << "\n  " << C::GRAY << "Selected: " << C::RESET;
            if (browseCursor)
                cout << C::CYAN << "\"" << browseCursor->song.title << "\""
                     << C::GRAY << " by " << C::RESET << C::SILVER << browseCursor->song.artist << C::RESET;
            else
                cout << C::GRAY << "(none)" << C::RESET;

            cout << "\n\n";
            printSingleRule();
            cout << "  " << C::CYAN  << "[A]" << C::SILVER << " Add highlighted   "
                 << C::CYAN  << "[R]" << C::SILVER << " Remove   "
                 << C::CYAN  << "[C]" << C::SILVER << " Clear   "
                 << C::GRAY  << "[0]" << C::GRAY   << " Back\n\n" << C::RESET;
            cout << "  " << C::CYAN << "Choice: " << C::RESET;
            showCursor(true);
            char c = (char)_getch();
            cout << c << "\n";
            showCursor(false);

            if (c == '0') break;
            else if (c == 'a' || c == 'A')
            {
                if (!browseCursor) { cout << "  " << C::GRAY << "No song highlighted.\n" << C::RESET; sleepMs(900); continue; }
                string key = makeKey(browseCursor->song);
                bool exists = false;
                for (auto &k : customPlaylist) if (k == key) { exists = true; break; }
                if (exists)
                    cout << "  " << C::ORANGE << "Already in playlist.\n" << C::RESET;
                else
                {
                    customPlaylist.push_back(key);
                    cout << "  " << C::GREEN << "[OK] Added: \"" << browseCursor->song.title << "\"\n" << C::RESET;
                }
                sleepMs(800);
            }
            else if (c == 'r' || c == 'R')
            {
                if (customPlaylist.empty()) { cout << "  " << C::GRAY << "Playlist is empty.\n" << C::RESET; sleepMs(700); continue; }
                cout << "  " << C::CYAN << "Remove number (1-" << customPlaylist.size() << "): " << C::RESET;
                showCursor(true);
                int num = 0; cin >> num; cin.ignore();
                showCursor(false);
                if (num >= 1 && num <= (int)customPlaylist.size())
                {
                    string key = customPlaylist[num-1];
                    size_t sep = key.find('|');
                    cout << "  " << C::GREEN << "[OK] Removed: \"" << (sep!=string::npos?key.substr(0,sep):key) << "\"\n" << C::RESET;
                    customPlaylist.erase(customPlaylist.begin()+(num-1));
                }
                else cout << "  " << C::RED << "Invalid number.\n" << C::RESET;
                sleepMs(800);
            }
            else if (c == 'c' || c == 'C')
            {
                customPlaylist.clear();
                cout << "  " << C::GREEN << "[OK] Playlist cleared.\n" << C::RESET;
                sleepMs(700);
            }
        }
        firstDraw = true;
    }

    // ── KTV Mode ──────────────────────────────────

    vector<string> wrapLyric(const string &text, int maxWidth)
    {
        vector<string> result;
        if (text.empty()) { result.push_back(""); return result; }
        vector<string> words;
        string word;
        for (int i = 0; i <= (int)text.size(); i++)
        {
            if (i == (int)text.size() || text[i] == ' ')
            {
                if (!word.empty()) { words.push_back(word); word = ""; }
            }
            else word += text[i];
        }
        string cur;
        for (auto &w : words)
        {
            string cand = cur.empty() ? w : cur + " " + w;
            if ((int)cand.size() <= maxWidth) cur = cand;
            else
            {
                if (!cur.empty()) result.push_back(cur);
                if ((int)w.size() > maxWidth)
                {
                    string tmp = w;
                    while ((int)tmp.size() > maxWidth) { result.push_back(tmp.substr(0,maxWidth)); tmp = tmp.substr(maxWidth); }
                    cur = tmp;
                }
                else cur = w;
            }
        }
        if (!cur.empty()) result.push_back(cur);
        return result;
    }

    static const int LYRIC_SLOT_ROWS = 4;

    void drawKtvScreen(const string &songTitle, const vector<string> &lyricLines,
                       int elapsed, int total, bool isPausedState, int stageWidth)
    {
        moveCursorHome();
        // Header
        printDoubleRule();
        cout << C::RED << C::BOLD;
        printCentered("KTV MODE  -  " + songTitle, C::RED + C::BOLD);
        printDoubleRule();
        cout << "\n";
        cout << C::GRAY;
        printCentered("[LEFT/RIGHT] Seek  [SPACE] Pause  [Any other key] Exit", C::GRAY);
        cout << C::RESET << "\n";

        // Lyric stage
        cout << "  " << C::GRAY << string(stageWidth, '-') << C::RESET << "\n\n";

        int topPad = (LYRIC_SLOT_ROWS - (int)lyricLines.size()) / 2;
        if (topPad < 0) topPad = 0;
        int botPad = LYRIC_SLOT_ROWS - (int)lyricLines.size() - topPad;
        if (botPad < 0) botPad = 0;

        for (int i = 0; i < topPad; i++) { clearRestOfLine(); cout << "\n"; }
        for (int i = 0; i < (int)lyricLines.size() && i < LYRIC_SLOT_ROWS; i++)
        {
            int pad = (stageWidth - (int)lyricLines[i].size()) / 2;
            if (pad < 0) pad = 0;
            cout << "  " << string(pad, ' ')
                 << C::WHITE << C::BOLD << lyricLines[i] << C::RESET;
            clearRestOfLine();
            cout << "\n";
        }
        for (int i = 0; i < botPad; i++) { clearRestOfLine(); cout << "\n"; }

        cout << "\n  " << C::GRAY << string(stageWidth, '-') << C::RESET << "\n\n";

        // Progress bar
        string bar = buildProgressBar(elapsed, total, isPausedState, 30);
        int remaining = total - elapsed;
        int pad = (SCREEN_WIDTH - 52) / 2;
        if (pad < 0) pad = 0;
        cout << string(pad, ' ') << bar;
        clearRestOfLine();
        cout << "\n";
        printDoubleRule();
    }

    void ktvMode()
    {
        drawMenuHeader("KTV / LYRICS MODE");
        showCursor(false);
        cout << C::SILVER << "  Available songs:\n\n" << C::RESET;
        for (int i = 0; i < KTV_COUNT; i++)
            cout << "  " << C::CYAN << "[" << (i+1) << "]" << C::RESET
                 << C::WHITE << "  " << ktvLibrary[i].songTitle << "\n";
        cout << "\n  " << C::GRAY << "[0] Cancel\n\n" << C::RESET;
        cout << "  " << C::CYAN << "Choose a song: " << C::RESET;
        showCursor(true);
        int choice = 0;
        cin >> choice;
        cin.ignore();
        showCursor(false);
        firstDraw = true;

        if (choice < 1 || choice > KTV_COUNT) { firstDraw = true; return; }
        int ki = choice - 1;

        LyricsEngine lyrics;
        if (!lyrics.load(ktvLibrary[ki].csvFile))
        {
            cout << "\n  " << C::RED << "[!] Could not open: " << ktvLibrary[ki].csvFile << C::RESET
                 << "\n\n  " << C::GRAY << "Press Enter to return..." << C::RESET;
            showCursor(true); cin.get(); showCursor(false);
            firstDraw = true;
            return;
        }

        Node *ktvNode = nullptr;
        if (head)
        {
            Node *node = head;
            string sf = toLower(ktvLibrary[ki].songTitle);
            do {
                if (toLower(node->song.title).find(sf) != string::npos) { ktvNode = node; break; }
                node = node->next;
            } while (node != head);
        }

        if (!ktvNode)
        {
            cout << "\n  " << C::RED << "[!] Song not found in library: \""
                 << ktvLibrary[ki].songTitle << "\"" << C::RESET
                 << "\n\n  " << C::GRAY << "Press Enter to return..." << C::RESET;
            showCursor(true); cin.get(); showCursor(false);
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
            if (curLine != lastLine) { lastLine = curLine; lastWrapped = wrapLyric(curLine, stageWidth); }
            drawKtvScreen(ktvLibrary[ki].songTitle, lastWrapped, elapsed, total, ktvPaused, stageWidth);

            if (_kbhit())
            {
                int k = _getch();
                if (k == 0 || k == 224)
                {
                    int k2 = _getch();
                    if (k2 == 75) { audio.seek(audio.elapsedSeconds()-10); lastLine = ""; continue; }
                    if (k2 == 77)
                    {
                        int p = audio.elapsedSeconds()+10;
                        if (total > 0 && p >= total) p = total-2;
                        audio.seek(p); lastLine = ""; continue;
                    }
                    continue;
                }
                if (k == ' ')
                {
                    if (audio.isPaused()) { audio.resume(); ktvPaused = false; }
                    else                  { audio.pause();  ktvPaused = true;  }
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
        for (int i = 0; i < count; i++) insertAtEnd(songs[i]);
        if (head && !browseCursor) browseCursor = head;
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
                if (_kbhit()) keyPressed = true;
                else sleepMs(100);
            }
            if (!keyPressed) continue;

            int key = _getch();
            if (key == 0 || key == 224)
            {
                int k2 = _getch();
                if      (k2 == 72) { if (browseCursor) browseCursor = browseCursor->prev; }
                else if (k2 == 80) { if (browseCursor) browseCursor = browseCursor->next; }
                else if (k2 == 75) { if (current && (audio.isPlaying()||audio.isPaused())) audio.seek(audio.elapsedSeconds()-10); }
                else if (k2 == 77)
                {
                    if (current && (audio.isPlaying()||audio.isPaused()))
                    {
                        int np = audio.elapsedSeconds()+10, sl = current->song.durationSeconds();
                        if (sl > 0 && np >= sl) np = sl-2;
                        audio.seek(np);
                    }
                }
                continue;
            }

            switch (key)
            {
            case 13:  if (browseCursor) startPlaying(browseCursor); break;
            case ' ': togglePause(); break;
            case 'a': case 'A':
                showCursor(true); addSong(); showCursor(false);
                if (!browseCursor && head) browseCursor = head;
                break;
            case '7': deleteSong(); break;
            case 's': case 'S': stopPlaying(); break;
            case 'n': case 'N': { Node *nx = current ? current->next : (head?head:nullptr); if (nx) startPlaying(nx); break; }
            case 'b': case 'B': { Node *pv = current ? current->prev : (head?head->prev:nullptr); if (pv) startPlaying(pv); break; }
            case 'r': case 'R':
            {
                if (!head) break;
                int n = countSongs(), ri = rand()%n;
                Node *nd = head;
                for (int i = 0; i < ri; i++) nd = nd->next;
                startPlaying(nd);
                break;
            }
            case 'h': case 'H': shuffleList(); break;
            case 'f': case 'F': searchMenu(); break;
            case 'o': case 'O': sortMenu();   break;
            case 'p': case 'P': playlistMenu(); break;
            case 'k': case 'K': ktvMode();    break;
            case 'w': case 'W': history.showWrapped(); firstDraw = true; break;
            case 'q': case 'Q': case 27:
                stopPlaying();
                clearScreen();
                showCursor(true);
                cout << C::RED << C::BOLD << "\n  Thank you for using MDZ Music!\n\n" << C::RESET;
                return;
            }
        }
    }
};

// ─────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────

int main()
{
    initConsole();
    MusicPlayer player;
    player.run();
    return 0;
}