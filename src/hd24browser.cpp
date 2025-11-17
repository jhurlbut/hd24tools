#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <sys/stat.h>

// Include ncurses first, then undefine conflicting macros
#include <ncurses.h>
#undef border
#undef scroll

// Now include HD24 headers
#include <hd24fs.h>
#include <hd24utils.h>
#include <sndfile.h>

using namespace std;

struct SongInfo {
    hd24project* project;
    hd24song* song;
    string project_name;
    string song_name;
    __uint32 sample_rate;
    __uint32 channels;
    string duration;
    __uint32 project_id;
    __uint32 song_id;
};

class HD24Browser {
private:
    hd24fs* fs;
    vector<SongInfo> songs;
    int current_selection;
    int view_mode; // 0 = song list, 1 = track selection
    bool track_selected[24];
    SongInfo* selected_song_info;
    string export_dir;
    int scroll_offset; // For scrolling in song list

    void ensure_directory_exists(const char* path) {
        struct stat st = {0};
        if (stat(path, &st) == -1) {
            mkdir(path, 0755);
        }
    }

    void list_all_songs() {
        songs.clear();

        __uint32 projcount = fs->projectcount();
        for (__uint32 i = 1; i <= projcount; i++) {
            hd24project* proj = fs->getproject(i);
            if (proj != NULL) {
                string* proj_name = proj->projectname();
                __uint32 songcount = proj->songcount();

                for (__uint32 j = 1; j <= songcount; j++) {
                    hd24song* song = proj->getsong(j);
                    if (song != NULL) {
                        SongInfo info;
                        info.project = proj;
                        info.song = song;
                        info.project_name = *proj_name;

                        string* sname = song->songname();
                        info.song_name = *sname;
                        delete sname;

                        info.sample_rate = song->samplerate();
                        info.channels = song->physical_channels();

                        string* dur = song->display_duration();
                        info.duration = *dur;
                        delete dur;

                        info.project_id = i;
                        info.song_id = j;

                        songs.push_back(info);
                    }
                }
                delete proj_name;
            }
        }
    }

    void draw_song_list() {
        clear();
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        // Title
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(0, 0, "HD24 Song Browser");
        attroff(A_BOLD | COLOR_PAIR(1));

        mvprintw(0, max_x - 30, "Arrow Keys: Navigate");
        mvprintw(1, 0, "================================================================================");

        // Column headers
        attron(A_BOLD);
        mvprintw(2, 0, "%-30s %-12s %-6s %s", "Song Name", "Sample Rate", "Tracks", "Duration");
        attroff(A_BOLD);
        mvprintw(3, 0, "--------------------------------------------------------------------------------");

        // Calculate visible range
        int visible_rows = max_y - 8; // Leave room for header and footer
        if (current_selection < scroll_offset) {
            scroll_offset = current_selection;
        }
        if (current_selection >= scroll_offset + visible_rows) {
            scroll_offset = current_selection - visible_rows + 1;
        }

        // List songs
        for (int i = scroll_offset; i < (int)songs.size() && i < scroll_offset + visible_rows; i++) {
            if (i == current_selection) {
                attron(A_REVERSE | COLOR_PAIR(2));
            }

            SongInfo& info = songs[i];

            // Truncate long names
            string display_name = info.song_name;
            if (display_name.length() > 28) {
                display_name = display_name.substr(0, 27) + "...";
            }

            mvprintw(4 + (i - scroll_offset), 0, "%-30s %-12d %-6d %s",
                    display_name.c_str(),
                    info.sample_rate,
                    info.channels,
                    info.duration.c_str());

            if (i == current_selection) {
                attroff(A_REVERSE | COLOR_PAIR(2));
            }
        }

        // Instructions at bottom
        int instr_row = max_y - 3;
        mvprintw(instr_row, 0, "--------------------------------------------------------------------------------");
        attron(COLOR_PAIR(3));
        mvprintw(instr_row + 1, 0, "ENTER");
        attroff(COLOR_PAIR(3));
        printw(": Select tracks for export  ");
        attron(COLOR_PAIR(3));
        printw("Q");
        attroff(COLOR_PAIR(3));
        printw(": Quit");

        // Show song count
        mvprintw(instr_row + 2, 0, "Total songs: %d  |  Selected: %d of %d",
                (int)songs.size(), current_selection + 1, (int)songs.size());

        refresh();
    }

    void draw_track_selection() {
        clear();
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        // Title
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(0, 0, "Track Selection - %s", selected_song_info->song_name.c_str());
        attroff(A_BOLD | COLOR_PAIR(1));

        mvprintw(1, 0, "================================================================================");

        // Song info
        mvprintw(2, 0, "Project: %s  |  Rate: %d Hz  |  Tracks: %d  |  Duration: %s",
                selected_song_info->project_name.c_str(),
                selected_song_info->sample_rate,
                selected_song_info->channels,
                selected_song_info->duration.c_str());

        mvprintw(3, 0, "--------------------------------------------------------------------------------");

        // Track list
        __uint32 channels = selected_song_info->channels;
        for (__uint32 i = 0; i < channels; i++) {
            if ((int)i == current_selection) {
                attron(A_REVERSE | COLOR_PAIR(2));
            }

            char checkbox = track_selected[i] ? 'X' : ' ';
            mvprintw(4 + i, 0, "[%c] Track %2d", checkbox, i + 1);

            if ((int)i == current_selection) {
                attroff(A_REVERSE | COLOR_PAIR(2));
            }
        }

        // Count selected tracks
        int selected_count = 0;
        for (__uint32 i = 0; i < channels; i++) {
            if (track_selected[i]) selected_count++;
        }

        // Instructions at bottom
        int instr_row = max_y - 4;
        mvprintw(instr_row, 0, "--------------------------------------------------------------------------------");

        attron(COLOR_PAIR(3));
        mvprintw(instr_row + 1, 0, "SPACE");
        attroff(COLOR_PAIR(3));
        printw(": Toggle  ");

        attron(COLOR_PAIR(3));
        printw("A");
        attroff(COLOR_PAIR(3));
        printw(": Select All  ");

        attron(COLOR_PAIR(3));
        printw("N");
        attroff(COLOR_PAIR(3));
        printw(": Select None  ");

        attron(COLOR_PAIR(3));
        printw("E");
        attroff(COLOR_PAIR(3));
        printw(": Export  ");

        attron(COLOR_PAIR(3));
        printw("ESC");
        attroff(COLOR_PAIR(3));
        printw(": Back");

        mvprintw(instr_row + 2, 0, "Selected: %d of %d tracks  |  Export to: %s",
                selected_count, channels, export_dir.c_str());

        refresh();
    }

    void export_tracks() {
        if (selected_song_info == NULL) return;

        // Count selected tracks
        int selected_count = 0;
        __uint32 channels = selected_song_info->channels;
        for (__uint32 i = 0; i < channels; i++) {
            if (track_selected[i]) selected_count++;
        }

        if (selected_count == 0) {
            clear();
            attron(COLOR_PAIR(4));
            mvprintw(0, 0, "No tracks selected. Press any key to continue...");
            attroff(COLOR_PAIR(4));
            refresh();
            getch();
            return;
        }

        clear();
        mvprintw(0, 0, "Exporting %d track(s) to AIFF format...", selected_count);
        mvprintw(1, 0, "Export directory: %s", export_dir.c_str());
        mvprintw(2, 0, "");
        refresh();

        // Ensure export directory exists
        ensure_directory_exists(export_dir.c_str());

        hd24song* song = selected_song_info->song;
        __uint32 sample_rate = song->samplerate();
        __uint32 song_length = song->songlength_in_wamples();

        // Buffer for reading samples (1024 samples at a time)
        const int BUFFER_SIZE = 1024;
        long sample_buffer[BUFFER_SIZE];
        unsigned char* byte_buffer = new unsigned char[BUFFER_SIZE * 3]; // 3 bytes per 24-bit sample

        __sint64 total_bytes = 0;
        int track_num = 0;

        // Export each selected track
        for (__uint32 track = 0; track < channels; track++) {
            if (!track_selected[track]) continue;

            track_num++;
            mvprintw(4, 0, "Exporting track %d of %d...                    ",
                    track_num, selected_count);
            refresh();

            // Create output filename
            char filename[512];
            snprintf(filename, sizeof(filename), "%s/%s_Track%02u.aif",
                    export_dir.c_str(),
                    selected_song_info->song_name.c_str(),
                    (unsigned int)(track + 1));

            // Set up SNDFILE for writing
            SF_INFO sfinfo;
            memset(&sfinfo, 0, sizeof(sfinfo));
            sfinfo.samplerate = sample_rate;
            sfinfo.channels = 1; // Mono
            sfinfo.format = SF_FORMAT_AIFF | SF_FORMAT_PCM_24;

            SNDFILE* outfile = sf_open(filename, SFM_WRITE, &sfinfo);
            if (!outfile) {
                mvprintw(6, 0, "Error: Could not create file: %s", filename);
                continue;
            }

            // Reset song position
            song->currentlocation(0);

            // Read and write samples
            __uint32 samples_written = 0;
            while (samples_written < song_length) {
                __uint32 samples_to_read = BUFFER_SIZE;
                if (samples_written + samples_to_read > song_length) {
                    samples_to_read = song_length - samples_written;
                }

                // Read multi-track samples and convert to 24-bit big-endian bytes
                for (__uint32 s = 0; s < samples_to_read; s++) {
                    song->getmultitracksample(sample_buffer, hd24song::READMODE_COPY);
                    // HD24 returns 24-bit samples as unsigned long
                    // getmultitracksample returns samples with bytes in this order:
                    //   bits 0-7   = low byte (was disk byte 2)
                    //   bits 8-15  = mid byte (was disk byte 1)
                    //   bits 16-23 = high byte (was disk byte 0)
                    // We need to write them in reverse order to match AIFF format
                    unsigned long sample = sample_buffer[track];

                    // Write in reverse byte order
                    byte_buffer[s * 3 + 0] = sample & 0xFF;          // Low byte first
                    byte_buffer[s * 3 + 1] = (sample >> 8) & 0xFF;   // Mid byte
                    byte_buffer[s * 3 + 2] = (sample >> 16) & 0xFF;  // High byte last
                }

                // Write raw 24-bit data to file
                sf_count_t written = sf_write_raw(outfile, byte_buffer, samples_to_read * 3);
                if (written != samples_to_read * 3) {
                    break;
                }

                samples_written += samples_to_read;

                // Update progress
                int percent = (samples_written * 100) / song_length;
                mvprintw(5, 0, "Progress: %d%%    ", percent);
                refresh();
            }

            sf_close(outfile);
            total_bytes += samples_written * 3; // 3 bytes per 24-bit sample
        }

        delete[] byte_buffer;

        // Show result
        mvprintw(6, 0, "");
        if (total_bytes > 0) {
            attron(COLOR_PAIR(2) | A_BOLD);
            mvprintw(7, 0, "Export completed successfully!");
            attroff(COLOR_PAIR(2) | A_BOLD);
            mvprintw(8, 0, "Exported %lld bytes to %d AIFF file(s)", total_bytes, selected_count);
        } else {
            attron(COLOR_PAIR(4) | A_BOLD);
            mvprintw(7, 0, "Export failed!");
            attroff(COLOR_PAIR(4) | A_BOLD);
        }
        mvprintw(10, 0, "Press any key to continue...");
        refresh();
        getch();
    }

public:
    HD24Browser(hd24fs* filesystem, const string& output_dir)
        : fs(filesystem), current_selection(0), view_mode(0),
          selected_song_info(NULL), export_dir(output_dir), scroll_offset(0) {
        memset(track_selected, 0, sizeof(track_selected));
    }

    void run() {
        // Initialize ncurses
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0); // Hide cursor

        // Initialize colors
        if (has_colors()) {
            start_color();
            init_pair(1, COLOR_CYAN, COLOR_BLACK);    // Headers
            init_pair(2, COLOR_GREEN, COLOR_BLACK);   // Success/Selection
            init_pair(3, COLOR_YELLOW, COLOR_BLACK);  // Keys
            init_pair(4, COLOR_RED, COLOR_BLACK);     // Errors
        }

        // Load songs
        list_all_songs();

        if (songs.empty()) {
            clear();
            attron(COLOR_PAIR(4) | A_BOLD);
            mvprintw(0, 0, "No songs found on HD24 device.");
            attroff(COLOR_PAIR(4) | A_BOLD);
            mvprintw(2, 0, "Press any key to exit...");
            refresh();
            getch();
            endwin();
            return;
        }

        bool running = true;
        while (running) {
            if (view_mode == 0) {
                draw_song_list();

                int ch = getch();
                switch (ch) {
                    case KEY_UP:
                        if (current_selection > 0) current_selection--;
                        break;
                    case KEY_DOWN:
                        if (current_selection < (int)songs.size() - 1) current_selection++;
                        break;
                    case 10: // ENTER
                    case KEY_ENTER:
                        selected_song_info = &songs[current_selection];
                        view_mode = 1;
                        current_selection = 0;
                        memset(track_selected, 0, sizeof(track_selected));
                        break;
                    case 'q':
                    case 'Q':
                        running = false;
                        break;
                }
            } else if (view_mode == 1) {
                draw_track_selection();

                int ch = getch();
                __uint32 channels = selected_song_info->channels;

                switch (ch) {
                    case KEY_UP:
                        if (current_selection > 0) current_selection--;
                        break;
                    case KEY_DOWN:
                        if (current_selection < (int)channels - 1) current_selection++;
                        break;
                    case ' ': // SPACE
                        track_selected[current_selection] = !track_selected[current_selection];
                        break;
                    case 'a':
                    case 'A':
                        for (__uint32 i = 0; i < channels; i++) {
                            track_selected[i] = true;
                        }
                        break;
                    case 'n':
                    case 'N':
                        for (__uint32 i = 0; i < channels; i++) {
                            track_selected[i] = false;
                        }
                        break;
                    case 'e':
                    case 'E':
                        export_tracks();
                        break;
                    case 27: // ESC
                        view_mode = 0;
                        current_selection = 0;
                        break;
                    case 'q':
                    case 'Q':
                        running = false;
                        break;
                }
            }
        }

        endwin();
    }
};

int main(int argc, char** argv) {
    cout << "HD24 Browser - Interactive song and track selector" << endl;
    cout << "===================================================" << endl << endl;

    string drive_image = "";
    string export_dir = "./hd24_export";

    // Parse command line arguments
    if (argc > 1) {
        drive_image = argv[1];
    }
    if (argc > 2) {
        export_dir = argv[2];
    }

    // If no drive image specified, prompt for one
    if (drive_image.empty()) {
        cout << "HD24 drive image file (or press Enter to auto-detect device): ";
        getline(cin, drive_image);
    }

    // Prompt for export directory if not provided
    if (argc <= 2) {
        cout << "Export directory [default: ./hd24_export]: ";
        string input;
        getline(cin, input);
        if (!input.empty()) {
            export_dir = input;
        }
    }

    cout << endl;
    cout << "Export directory: " << export_dir << endl;

    hd24fs* fs;

    if (drive_image.empty()) {
        cout << "Detecting HD24 drives..." << endl;

        // On macOS, scan /dev/rdisk* devices to find HD24
        #ifdef DARWIN
        for (int disk_num = 0; disk_num < 20; disk_num++) {
            char test_device[32];
            snprintf(test_device, sizeof(test_device), "/dev/rdisk%d", disk_num);

            // Try to open and validate as HD24
            string* test_dev = new string(test_device);
            hd24fs* test_fs = new hd24fs("", hd24fs::MODE_RDONLY, test_dev, false);

            if (test_fs->isOpen()) {
                cout << "Found HD24 device: " << test_device << endl;
                drive_image = string(test_device);
                delete test_fs;
                delete test_dev;
                break;
            }

            delete test_fs;
            delete test_dev;
        }
        #endif

        if (drive_image.empty()) {
            // Fall back to library auto-detection
            fs = new hd24fs((const char*)NULL);
        } else {
            // Use the detected device
            string* img = new string(drive_image);
            fs = new hd24fs("", hd24fs::MODE_RDONLY, img, false);
            delete img;
        }
    } else {
        cout << "Opening drive image: " << drive_image << endl;
        // Open specific drive image
        string* img = new string(drive_image);
        fs = new hd24fs("", hd24fs::MODE_RDONLY, img, false);
        delete img;
    }

    if (!fs->isOpen()) {
        cout << "Error: Could not find HD24 device or drive image." << endl;
        cout << "Please ensure your HD24 is connected." << endl;
        cout << endl;
        cout << "Tip: Auto-detection may not work on macOS." << endl;
        cout << "     Try running: ./hd24browser /dev/rdiskX" << endl;
        cout << "     where X is your HD24 drive number." << endl;
        cout << endl;
        cout << "To find your HD24 drive, run: diskutil list" << endl;
        delete fs;
        return 1;
    }

    string* device_name = fs->getdevicename();
    string* volume_name = fs->volumename();
    cout << "Found HD24 device: " << device_name->c_str() << endl;
    cout << "Volume name: " << volume_name->c_str() << endl;
    delete device_name;
    delete volume_name;

    cout << endl << "Starting browser..." << endl;
    cout << "Press any key to continue...";
    cin.get();

    // Run browser
    HD24Browser browser(fs, export_dir);
    browser.run();

    // Cleanup
    delete fs;

    cout << endl << "Thank you for using HD24 Browser!" << endl;

    return 0;
}
