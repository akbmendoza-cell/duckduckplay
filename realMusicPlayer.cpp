#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <ctime>

// _WIN32 Libraries
#include <windows.h>
#include <mmsystem.h>   // PlaySound
#include <conio.h>      // _kbhit(), _getch()

#pragma comment(lib, "winmm.lib")

using namespace std;

// ─── Constants ───────────────────────────────────────────────────────────────
const string CSV_FILE        = "playlist.csv";
const string EXPECTED_HEADER = "Title,Artist,Album,Genre,Year,Duration,FilePath";
const int    MAX_SONGS       = 500;
const int    MAX_FIELDS      = 7;
const int    VIEWPORT_SIZE   = 10;  // max songs visible at once in the list

// ─── Utilities ────────────────────────────────────────────────────────────────
void clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// Move cursor to top-left and clear each line as we redraw —
// avoids the full-screen flash that system("cls") causes.
void moveCursorHome() {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD  coord    = {0, 0};
    SetConsoleCursorPosition(hConsole, coord);
#else
    // ANSI: cursor to row 1, col 1
    cout << "\033[H";
#endif
}

// Erase from cursor to end of line so leftover characters from a longer
// previous frame don't bleed through on the current redraw.
void clearEOL() {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        COORD  cur   = csbi.dwCursorPosition;
        DWORD  cols  = csbi.dwSize.X - cur.X;
        DWORD  written;
        FillConsoleOutputCharacterA(hConsole, ' ', cols, cur, &written);
        SetConsoleCursorPosition(hConsole, cur); // leave cursor in place
    }
#else
    cout << "\033[K";
#endif
}

void sleepMs(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

// Truncate a string to maxLen, appending "..." if it was cut.
string truncate(const string& s, int maxLen) {
    if ((int)s.length() <= maxLen) return s;
    return s.substr(0, maxLen - 3) + "...";
}

string toLower(string str) {
    for (int i = 0; i < (int)str.length(); i++)
        str[i] = tolower(str[i]);
    return str;
}

// ─── CLASS: Song ──────────────────────────────────────────────────────────────
class Song {
public:
    string title;
    string artist;
    string album;
    string genre;
    int    year;
    string duration;   // "MM:SS"
    string filePath;   // path to .wav file

    Song() { year = 0; }

    Song(string t, string ar, string al, string g, int y, string d, string fp)
        : title(t), artist(ar), album(al), genre(g), year(y),
          duration(d), filePath(fp) {}

    // Convert "MM:SS" to total seconds
    int durationInSeconds() const {
        int colonPos = (int)duration.find(":");
        if (colonPos == (int)string::npos) return 0;
        return stoi(duration.substr(0, colonPos)) * 60
             + stoi(duration.substr(colonPos + 1));
    }
};

// ─── CLASS: Node  (circular doubly linked list) ───────────────────────────────
class Node {
public:
    Song  song;
    Node* next;
    Node* prev;

    Node(Song s) : song(s), next(nullptr), prev(nullptr) {}
};

// ─── CLASS: CSVManager ────────────────────────────────────────────────────────
class CSVManager {
public:
    // Split a CSV line into exactly fieldCount tokens.
    bool parseLine(string line, string fields[], int fieldCount) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        int    idx   = 0;
        string token = "";

        for (int i = 0; i <= (int)line.length(); i++) {
            if (i == (int)line.length() || line[i] == ',') {
                if (idx >= fieldCount) return false;
                fields[idx++] = token;
                token = "";
            } else {
                token += line[i];
            }
        }
        return idx == fieldCount;
    }

    // ── Master list of default songs ─────────────────────────────────────────
    // Add new entries here — they are appended to the CSV automatically on
    // the next run if they are not already present.
    void getDefaultSongs(Song out[], int& count) {
        count = 0;
        out[count++] = Song(
            "Breakin' My Heart (Pretty Brown Eyes)", "Mint Condition",
            "Meant to Be Mint", "R&B", 1991, "4:16",
            "music/breakin_my_heart.mp3"
        );
    }

    void writeDefaultSongs() {
        ofstream file(CSV_FILE);
        if (!file.is_open()) { cout << "Error: Cannot create CSV.\n"; return; }

        file << EXPECTED_HEADER << "\n";

        Song defaults[MAX_SONGS];
        int  count = 0;
        getDefaultSongs(defaults, count);
        for (int i = 0; i < count; i++) {
            file << defaults[i].title    << ","
                 << defaults[i].artist   << ","
                 << defaults[i].album    << ","
                 << defaults[i].genre    << ","
                 << defaults[i].year     << ","
                 << defaults[i].duration << ","
                 << defaults[i].filePath << "\n";
        }
        file.close();
    }

    bool isDuplicate(const string& title, const string& artist) {
        ifstream file(CSV_FILE);
        if (!file.is_open()) return false;
        string line;
        getline(file, line); // skip header
        while (getline(file, line)) {
            if (line.empty()) continue;
            string fields[MAX_FIELDS];
            if (!parseLine(line, fields, MAX_FIELDS)) continue;
            if (toLower(fields[0]) == toLower(title) &&
                toLower(fields[1]) == toLower(artist)) {
                file.close(); return true;
            }
        }
        file.close(); return false;
    }

    void saveSong(const Song& s) {
        ofstream file(CSV_FILE, ios::app);
        if (!file.is_open()) { cout << "Error: Cannot write CSV.\n"; return; }
        file << s.title    << ","
             << s.artist   << ","
             << s.album    << ","
             << s.genre    << ","
             << s.year     << ","
             << s.duration << ","
             << s.filePath << "\n";
        file.close();
    }

    int loadSongs(Song songs[], int maxSongs) {
        ifstream file(CSV_FILE);
        if (!file.is_open()) { cout << "Error: Cannot open CSV.\n"; return 0; }
        string line;
        getline(file, line); // skip header
        int count = 0;
        while (getline(file, line) && count < maxSongs) {
            if (line.empty()) continue;
            string fields[MAX_FIELDS];
            if (!parseLine(line, fields, MAX_FIELDS)) continue;
            songs[count++] = Song(
                fields[0], fields[1], fields[2], fields[3],
                stoi(fields[4]), fields[5], fields[6]
            );
        }
        file.close();
        return count;
    }

    // Create or repair the CSV, then ensure all default songs are present.
    void createDefaultCSV() {
        ifstream checkFile(CSV_FILE);
        if (checkFile.is_open()) {
            string header;
            getline(checkFile, header);
            if (!header.empty() && header.back() == '\r') header.pop_back();
            checkFile.close();

            if (header != EXPECTED_HEADER) {
                cout << "[!] Old CSV format detected. Recreating playlist.csv...\n";
                writeDefaultSongs();
                cout << "[+] Default playlist CSV created: " << CSV_FILE << "\n";
                return;
            }

            // Header OK — append any missing default songs
            Song defaults[MAX_SONGS];
            int  count = 0;
            getDefaultSongs(defaults, count);
            int added = 0;
            for (int i = 0; i < count; i++) {
                if (!isDuplicate(defaults[i].title, defaults[i].artist)) {
                    saveSong(defaults[i]);
                    cout << "[+] Added missing default song: \""
                         << defaults[i].title << "\"\n";
                    added++;
                }
            }
            if (added > 0)
                cout << "[+] " << added
                     << " default song(s) added to " << CSV_FILE << "\n";
            return;
        }
        // File did not exist — create fresh
        writeDefaultSongs();
        cout << "[+] Default playlist CSV created: " << CSV_FILE << "\n";
    }
};

// ─── CLASS: MusicPlayer ───────────────────────────────────────────────────────
class MusicPlayer {
private:
    Node*      head;
    Node*      current;       // node currently playing
    Node*      browseCursor;  // node highlighted in the browse list
    bool       isPlaying;
    time_t     playStartTime; // wall-clock time when current song started
    bool       firstDraw;     // true until the screen has been drawn once
    CSVManager csv;

    // ── Linked-list helpers ───────────────────────────────────────────────────
    int countSongs() {
        if (!head) return 0;
        int n = 0; Node* t = head;
        do { n++; t = t->next; } while (t != head);
        return n;
    }

    void insertAtEnd(Song s) {
        Node* newNode = new Node(s);
        if (!head) {
            newNode->next = newNode;
            newNode->prev = newNode;
            head = newNode;
        } else {
            Node* tail    = head->prev;
            tail->next    = newNode;
            newNode->prev = tail;
            newNode->next = head;
            head->prev    = newNode;
        }
    }

    // Fisher-Yates shuffle (swaps Song data, preserves node pointers)
    void shuffleList() {
        int n = countSongs();
        if (n <= 1) return;

        Node* nodes[MAX_SONGS];
        Node* t = head;
        for (int i = 0; i < n; i++) { nodes[i] = t; t = t->next; }

        for (int i = n - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            Song tmp       = nodes[i]->song;
            nodes[i]->song = nodes[j]->song;
            nodes[j]->song = tmp;
        }

        for (int i = 0; i < n; i++) {
            nodes[i]->next = nodes[(i + 1) % n];
            nodes[i]->prev = nodes[(i - 1 + n) % n];
        }

        head = nodes[0];
    }

    // ── Selection Sort ────────────────────────────────────────────────────────
    // Sorts the linked list in-place by swapping Song data (not pointers).
    // key: 0 = title, 1 = artist, 2 = year
    void selectionSort(int key) {
        int n = countSongs();
        if (n <= 1) return;

        Node* nodes[MAX_SONGS];
        Node* t = head;
        for (int i = 0; i < n; i++) { nodes[i] = t; t = t->next; }

        for (int i = 0; i < n - 1; i++) {
            int minIdx = i;
            for (int j = i + 1; j < n; j++) {
                bool less = false;
                if      (key == 0) less = toLower(nodes[j]->song.title)  < toLower(nodes[minIdx]->song.title);
                else if (key == 1) less = toLower(nodes[j]->song.artist) < toLower(nodes[minIdx]->song.artist);
                else               less = nodes[j]->song.year             < nodes[minIdx]->song.year;
                if (less) minIdx = j;
            }
            if (minIdx != i) {
                Song tmp          = nodes[i]->song;
                nodes[i]->song    = nodes[minIdx]->song;
                nodes[minIdx]->song = tmp;
            }
        }
    }

    // ── Linear Search ─────────────────────────────────────────────────────────
    // Returns the first node whose title OR artist contains `query` (case-insensitive).
    // Returns nullptr if not found.
    Node* linearSearch(const string& query) {
        if (!head || query.empty()) return nullptr;
        string q = toLower(query);
        Node* t = head;
        do {
            if (toLower(t->song.title).find(q)  != string::npos ||
                toLower(t->song.artist).find(q) != string::npos)
                return t;
            t = t->next;
        } while (t != head);
        return nullptr;
    }

    // ── Sort screen ───────────────────────────────────────────────────────────
    void sortMenu() {
        clearScreen();
        cout << string(W, '=') << "\n";
        string hdr = "Sort Playlist";
        cout << string((W - (int)hdr.length()) / 2, ' ') << hdr << "\n";
        cout << string(W, '=') << "\n\n";
        cout << "  Sort by:\n";
        cout << "    [1] Title  (A-Z)\n";
        cout << "    [2] Artist (A-Z)\n";
        cout << "    [3] Year   (oldest first)\n";
        cout << "    [0] Cancel\n\n";
        cout << "  Choice: ";

        char c = _getch();
        cout << c << "\n";
        int key = -1;
        if      (c == '1') key = 0;
        else if (c == '2') key = 1;
        else if (c == '3') key = 2;

        if (key >= 0) {
            selectionSort(key);
            browseCursor = head;
            cout << "\n  [+] Playlist sorted!\n";
        } else {
            cout << "\n  Cancelled.\n";
        }
        sleepMs(800);
        firstDraw = true;
    }

    // ── Search screen ─────────────────────────────────────────────────────────
    void searchMenu() {
        clearScreen();
        cout << string(W, '=') << "\n";
        string hdr = "Search Playlist";
        cout << string((W - (int)hdr.length()) / 2, ' ') << hdr << "\n";
        cout << string(W, '=') << "\n\n";

        // Show cursor while typing
#ifdef _WIN32
        HANDLE hc = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO ci; GetConsoleCursorInfo(hc, &ci);
        CONSOLE_CURSOR_INFO shown = ci; shown.bVisible = TRUE;
        SetConsoleCursorInfo(hc, &shown);
#else
        cout << "\033[?25h"; cout.flush();
#endif

        cout << "  Enter title or artist to search: ";
        string query;
        getline(cin, query);

        Node* found = linearSearch(query);

        if (found) {
            browseCursor = found;
            cout << "\n  [+] Found: \"" << found->song.title
                 << "\" by " << found->song.artist << "\n";
        } else {
            cout << "\n  [!] No match found for \"" << query << "\".\n";
        }

        cout << "\n  Press Enter to return...";
        cin.get();

        // Re-hide cursor
#ifdef _WIN32
        SetConsoleCursorInfo(hc, &ci);
#else
        cout << "\033[?25l"; cout.flush();
#endif
        firstDraw = true;
    }

    // ── Playback helpers ──────────────────────────────────────────────────────

    // Non-blocking play — audio runs via SND_ASYNC so the UI stays live.
    void startPlaying(Node* node) {
        if (!node) return;
        PlaySound(NULL, NULL, 0);          // stop any currently running audio
        current       = node;
        isPlaying     = true;
        playStartTime = time(nullptr);
        if (!node->song.filePath.empty()) {
            PlaySoundA(node->song.filePath.c_str(), NULL,
                       SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        }
    }

    void stopPlaying() {
        PlaySound(NULL, NULL, 0);
        isPlaying = false;
    }

    // Seconds elapsed since playback started, capped at song duration.
    int elapsedSeconds() const {
        if (!isPlaying || !current) return 0;
        int e = (int)(time(nullptr) - playStartTime);
        int t = current->song.durationInSeconds();
        return (e > t) ? t : e;
    }

    // Format integer seconds as "MM:SS".
    string fmtTime(int secs) const {
        if (secs < 0) secs = 0;
        int m = secs / 60, s = secs % 60;
        string r;
        r += (m < 10 ? "0" : ""); r += to_string(m);
        r += ":";
        r += (s < 10 ? "0" : ""); r += to_string(s);
        return r;
    }

    // ── Main screen renderer ──────────────────────────────────────────────────

    // Fixed display width — every line is padded/truncated to this.
    static const int W = 60;

    // Print a string centred within W chars, then clearEOL + newline.
    void printCentered(const string& s) {
        int pad = (W - (int)s.length()) / 2;
        if (pad < 0) pad = 0;
        cout << string(pad, ' ') << s;
        clearEOL(); cout << "\n";
    }

    // Print a line of `ch` repeated W times.
    void printRule(char ch) {
        cout << string(W, ch);
        clearEOL(); cout << "\n";
    }

    // Build a song row string (without trailing newline).
    // Format:  MRK N. "Title..." by Artist  [D:DD]  <--
    // The whole thing is left-padded so <-- lands at column W.
    string songRow(int idx, Node* node, bool isBrowse, bool isCurrent) {
        // Marker prefix (4 chars)
        string mrk;
        if      (isBrowse && isCurrent) mrk = ">(*";
        else if (isBrowse)              mrk = ">> ";
        else if (isCurrent)             mrk = " * ";
        else                            mrk = "   ";

        string dur    = "[" + node->song.duration + "]";
        string suffix = isBrowse ? ("  " + dur + "  <--")
                                 : ("  " + dur);

        // Budget for title+artist: W - 1(space) - 3(mrk) - numLen - 4(`. "`) - 5(`" by `) - suffix
        string numStr = to_string(idx + 1) + ". \"";
        int budget = W - 1 - 3 - (int)numStr.length() - 5 - (int)suffix.length();
        if (budget < 4) budget = 4;

        // Split budget: ~60% title, ~40% artist
        int tMax = budget * 6 / 10;
        int aMax = budget - tMax;

        string title  = truncate(node->song.title,  tMax);
        string artist = truncate(node->song.artist, aMax);

        string row = " " + mrk + numStr + title + "\" by " + artist + suffix;

        // Pad so every row is exactly W chars (keeps <-- aligned)
        if ((int)row.length() < W)
            row += string(W - row.length(), ' ');
        else if ((int)row.length() > W)
            row = row.substr(0, W);

        return row;
    }

    void drawScreen() {
        // First draw: clear once so we start with a blank canvas.
        // All subsequent redraws just move the cursor back to the top,
        // which eliminates the blinking / flashing from repeated cls calls.
        if (firstDraw) {
            clearScreen();
            firstDraw = false;
        } else {
            moveCursorHome();
        }

        printRule('=');
        printCentered("C++ Music Player");
        printRule('=');

        // ── Now Playing bar ───────────────────────────────────────────────────
        if (isPlaying && current) {
            int elapsed = elapsedSeconds();
            int total   = current->song.durationInSeconds();
            int remain  = total - elapsed;
            int barW    = 34;
            int filled  = (total > 0) ? (elapsed * barW / total) : barW;
            bool ended  = (total > 0 && elapsed >= total);

            string nowLine = ">> \"" + truncate(current->song.title, 28)
                           + "\" by " + truncate(current->song.artist, 18);
            printCentered(nowLine);

            string status = ended ? "[ENDED]" : "[PLAYING]";
            string bar    = fmtTime(elapsed) + " [";
            for (int i = 0; i < barW; i++) bar += (i < filled ? '#' : '-');
            bar += "] -" + fmtTime(remain) + " " + status;
            printCentered(bar);
        } else {
            printCentered("No song is currently playing.");
            cout << "\n"; // keep line count equal to the playing branch
        }

        printRule('-');

        // ── Scrollable playlist / browse view ────────────────────────────────
        if (!head) {
            printCentered("Playlist is empty. Press [A] to add.");
        } else {
            int total = countSongs();

            // Find index of browseCursor
            int browseIdx = 0, idx = 0;
            Node* t = head;
            do {
                if (t == browseCursor) browseIdx = idx;
                idx++; t = t->next;
            } while (t != head);

            // Scrolling viewport centred on the browse cursor
            int vStart = browseIdx - VIEWPORT_SIZE / 2;
            if (vStart < 0) vStart = 0;
            if (vStart + VIEWPORT_SIZE > total)
                vStart = (total > VIEWPORT_SIZE) ? total - VIEWPORT_SIZE : 0;
            int vEnd = vStart + VIEWPORT_SIZE;
            if (vEnd > total) vEnd = total;

            if (vStart > 0)
                printCentered("... (" + to_string(vStart) + " above)");

            Node* node = head;
            for (int i = 0; i < vStart; i++) node = node->next;

            for (int i = vStart; i < vEnd; i++) {
                bool isBrowse  = (node == browseCursor);
                bool isCurrent = (isPlaying && node == current);
                cout << songRow(i, node, isBrowse, isCurrent);
                clearEOL(); cout << "\n";
                node = node->next;
            }

            int below = total - vEnd;
            if (below > 0)
                printCentered("... (" + to_string(below) + " below)");
        }

        printRule('-');
        printCentered("[UP/DOWN] Browse          [ENTER] Play");
        printCentered("[N] Next  [P] Prev  [S] Stop  [R] Random");
        printCentered("[A] Add  [H] Shuffle  [F] Search  [O] Sort");
        printCentered("[Q] Quit");
        printRule('=');
    }

public:
    // ── Constructor ───────────────────────────────────────────────────────────
    MusicPlayer() : head(nullptr), current(nullptr), browseCursor(nullptr),
                    isPlaying(false), playStartTime(0), firstDraw(true) {
        csv.createDefaultCSV();
        loadFromCSV();
    }

    void loadFromCSV() {
        Song songs[MAX_SONGS];
        int count = csv.loadSongs(songs, MAX_SONGS);
        for (int i = 0; i < count; i++) insertAtEnd(songs[i]);
        if (head && !browseCursor) browseCursor = head;
    }

    // ── Add Song ──────────────────────────────────────────────────────────────
    void addSong() {
        clearScreen();
        cout << "=== Add Song ===\n\n";

        string title, artist, album, genre, duration, filePath;
        int year;

        cout << "Enter song title        : "; getline(cin, title);
        cout << "Enter artist name       : "; getline(cin, artist);

        if (csv.isDuplicate(title, artist)) {
            cout << "\n[!] \"" << title << "\" by " << artist
                 << " is already in the playlist!\n"
                 << "    Duplicate was not added.\n";
            cout << "\nPress Enter to continue...";
            cin.get();
            return;
        }

        cout << "Enter album name        : "; getline(cin, album);
        cout << "Enter genre             : "; getline(cin, genre);
        cout << "Enter release year      : "; cin >> year; cin.ignore();
        cout << "Enter duration (MM:SS)  : "; getline(cin, duration);
        cout << "Enter WAV file path     : "; getline(cin, filePath);

        Song newSong(title, artist, album, genre, year, duration, filePath);
        insertAtEnd(newSong);
        csv.saveSong(newSong);
        if (!browseCursor) browseCursor = head;

        cout << "\n[+] Song added: \"" << title << "\" by " << artist << "\n";
        cout << "\nPress Enter to continue...";
        cin.get();
    }

    // ── Main event loop ───────────────────────────────────────────────────────
    // The main screen IS the player — no separate menu.
    // Arrow keys browse the playlist; letter keys control playback.
    // Audio runs via SND_ASYNC: the screen stays responsive while music plays.
    void run() {
        srand((unsigned)time(nullptr));

        // Hide the blinking cursor — it jumps around on every redraw.
        // We restore it when the player exits.
#ifdef _WIN32
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO cursorInfo;
        GetConsoleCursorInfo(hConsole, &cursorInfo);
        CONSOLE_CURSOR_INFO hiddenCursor = cursorInfo;
        hiddenCursor.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &hiddenCursor);
#else
        cout << "\033[?25l"; // hide cursor (ANSI)
        cout.flush();
#endif

        while (true) {
            drawScreen();

            // Poll for a keypress in 100 ms increments.
            // If ~500 ms pass with no input, redraw to update the progress bar.
            bool gotKey = false;
            for (int i = 0; i < 5 && !gotKey; i++) {
                if (_kbhit()) gotKey = true;
                else sleepMs(100);
            }
            if (!gotKey) continue;

            int key = _getch();

            // Arrow keys arrive as two bytes: prefix (0 or 224) + code
            if (key == 0 || key == 224) {
                int k2 = _getch();
                if      (k2 == 72 && browseCursor) browseCursor = browseCursor->prev; // UP
                else if (k2 == 80 && browseCursor) browseCursor = browseCursor->next; // DOWN
                continue;
            }

            switch (key) {

                // ENTER: play the highlighted song
                case 13:
                    if (browseCursor) startPlaying(browseCursor);
                    break;

                // A: add a new song
                case 'a': case 'A':
                    addSong();
                    firstDraw = true; // addSong cleared the screen; redraw cleanly
                    if (!browseCursor && head) browseCursor = head;
                    break;

                // S: stop playback
                case 's': case 'S':
                    stopPlaying();
                    break;

                // N: next song
                case 'n': case 'N': {
                    if (!head) break;
                    Node* nxt = (isPlaying && current) ? current->next : head;
                    startPlaying(nxt);
                    browseCursor = nxt;
                    break;
                }

                // P: previous song
                case 'p': case 'P':
                    if (head && isPlaying && current) {
                        Node* prv = current->prev;
                        startPlaying(prv);
                        browseCursor = prv;
                    }
                    break;

                // R: random song
                case 'r': case 'R': {
                    if (!head) break;
                    int n = countSongs(), r = rand() % n;
                    Node* t = head;
                    for (int i = 0; i < r; i++) t = t->next;
                    startPlaying(t);
                    browseCursor = t;
                    break;
                }

                // F: search
                case 'f': case 'F':
                    searchMenu();
                    break;

                // O: sort
                case 'o': case 'O':
                    sortMenu();
                    break;

                // H: shuffle (stops current playback since order changes)
                case 'h': case 'H':
                    stopPlaying();
                    shuffleList();
                    browseCursor = head;
                    break;

                // Q / ESC: quit
                case 'q': case 'Q':
                case 27:
                    stopPlaying();
                    clearScreen();
#ifdef _WIN32
                    SetConsoleCursorInfo(hConsole, &cursorInfo); // restore cursor
#else
                    cout << "\033[?25h"; // restore cursor (ANSI)
                    cout.flush();
#endif
                    cout << "Exiting Music Player. Goodbye!\n";
                    return;
            }
        }
    }
};

// ─── Entry point ─────────────────────────────────────────────────────────────
int main() {
    MusicPlayer player;
    player.run();
    return 0;
}