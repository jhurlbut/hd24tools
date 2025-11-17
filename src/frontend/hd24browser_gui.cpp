#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Check_Browser.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>

#include <hd24fs.h>
#include <hd24utils.h>
#include <sndfile.h>

#include <vector>
#include <string>

using namespace std;

// Song information structure
struct SongInfo {
    hd24project* project;
    hd24song* song;
    string project_name;
    string song_name;
    __uint32 sample_rate;
    __uint32 channels;
    __uint32 song_length;
    string duration;
    __uint32 project_id;
    __uint32 song_id;
};

class HD24BrowserGUI {
private:
    // UI Widgets
    Fl_Double_Window* window;
    Fl_Choice* device_choice;
    Fl_Button* detect_button;
    Fl_Hold_Browser* song_browser;
    Fl_Check_Browser* track_browser;
    Fl_Button* select_all_button;
    Fl_Button* select_none_button;
    Fl_Output* output_dir_field;
    Fl_Button* browse_button;
    Fl_Button* export_button;
    Fl_Box* status_box;

    // Data
    hd24fs* fs;
    vector<SongInfo> songs;
    vector<string> detected_devices;
    string export_dir;

public:
    HD24BrowserGUI() : fs(NULL), export_dir("./hd24_export") {
        create_window();
    }

    ~HD24BrowserGUI() {
        if (fs) delete fs;
    }

    void create_window() {
        window = new Fl_Double_Window(600, 600, "HD24 Browser");
        window->begin();

        // Device selection section
        Fl_Box* device_label = new Fl_Box(10, 10, 580, 25, "HD24 Device:");
        device_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        device_choice = new Fl_Choice(10, 35, 450, 30);
        device_choice->callback(device_selected_cb, this);

        detect_button = new Fl_Button(470, 35, 120, 30, "Auto-Detect");
        detect_button->callback(detect_devices_cb, this);

        // Song list section
        Fl_Box* song_label = new Fl_Box(10, 75, 580, 25, "Songs:");
        song_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        song_browser = new Fl_Hold_Browser(10, 100, 580, 150);
        song_browser->callback(song_selected_cb, this);

        // Track selection section
        Fl_Box* track_label = new Fl_Box(10, 260, 580, 25, "Tracks:");
        track_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        track_browser = new Fl_Check_Browser(10, 285, 400, 180);

        select_all_button = new Fl_Button(420, 285, 80, 30, "Select All");
        select_all_button->callback(select_all_cb, this);

        select_none_button = new Fl_Button(510, 285, 80, 30, "Select None");
        select_none_button->callback(select_none_cb, this);

        // Output directory section
        Fl_Box* output_label = new Fl_Box(10, 475, 580, 25, "Export Directory:");
        output_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        output_dir_field = new Fl_Output(10, 500, 480, 30);
        output_dir_field->value(export_dir.c_str());

        browse_button = new Fl_Button(500, 500, 90, 30, "Browse...");
        browse_button->callback(browse_dir_cb, this);

        // Export button
        export_button = new Fl_Button(10, 540, 580, 35, "Export Selected Tracks");
        export_button->callback(export_cb, this);
        export_button->deactivate();

        // Status
        status_box = new Fl_Box(10, 580, 580, 20, "Ready. Click Auto-Detect to find HD24 drives.");
        status_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        status_box->labelfont(FL_COURIER);
        status_box->labelsize(11);

        window->end();
        window->resizable(window);
    }

    void show() {
        window->show();
    }

    // Auto-detect HD24 devices on macOS
    static void detect_devices_cb(Fl_Widget*, void* data) {
        HD24BrowserGUI* gui = (HD24BrowserGUI*)data;
        gui->detect_devices();
    }

    void detect_devices() {
        status_box->label("Scanning for HD24 drives...");
        status_box->redraw();
        Fl::check();

        detected_devices.clear();
        device_choice->clear();

        #ifdef DARWIN
        // Scan /dev/rdisk0-19 for HD24 devices
        for (int disk_num = 0; disk_num < 20; disk_num++) {
            char test_device[32];
            snprintf(test_device, sizeof(test_device), "/dev/rdisk%d", disk_num);

            string* test_dev = new string(test_device);
            hd24fs* test_fs = new hd24fs("", hd24fs::MODE_RDONLY, test_dev, false);

            if (test_fs->isOpen()) {
                string* volname = test_fs->volumename();
                char label[256];
                snprintf(label, sizeof(label), "%s (%s)", test_device, volname->c_str());
                device_choice->add(label);
                detected_devices.push_back(string(test_device));
                delete volname;
            }

            delete test_fs;
            delete test_dev;
        }
        #endif

        if (detected_devices.empty()) {
            status_box->label("No HD24 drives found. Try specifying device manually.");
            fl_alert("No HD24 drives found.\n\nRun 'diskutil list' in Terminal to find your drive,\nthen use that device path directly.");
        } else {
            device_choice->value(0);
            char msg[256];
            snprintf(msg, sizeof(msg), "Found %d HD24 drive(s). Select one to load songs.",
                    (int)detected_devices.size());
            status_box->label(msg);
            device_selected();
        }

        device_choice->redraw();
    }

    static void device_selected_cb(Fl_Widget*, void* data) {
        HD24BrowserGUI* gui = (HD24BrowserGUI*)data;
        gui->device_selected();
    }

    void device_selected() {
        int idx = device_choice->value();
        if (idx < 0 || idx >= (int)detected_devices.size()) return;

        load_songs(detected_devices[idx]);
    }

    void load_songs(const string& device) {
        status_box->label("Loading songs from device...");
        status_box->redraw();
        Fl::check();

        // Clean up old filesystem
        if (fs) {
            delete fs;
            fs = NULL;
        }

        songs.clear();
        song_browser->clear();
        track_browser->clear();
        export_button->deactivate();

        // Open device
        string* dev = new string(device);
        fs = new hd24fs("", hd24fs::MODE_RDONLY, dev, false);
        delete dev;

        if (!fs->isOpen()) {
            status_box->label("Error: Could not open HD24 device.");
            return;
        }

        // Load all songs
        __uint32 proj_count = fs->projectcount();
        for (__uint32 p = 1; p <= proj_count; p++) {
            hd24project* project = fs->getproject(p);
            if (!project) continue;

            string* proj_name = project->projectname();
            __uint32 song_count = project->songcount();

            for (__uint32 s = 1; s <= song_count; s++) {
                hd24song* song = project->getsong(s);
                if (!song) continue;

                SongInfo info;
                info.project = project;
                info.song = song;
                info.project_name = *proj_name;
                info.song_name = *(song->songname());
                info.sample_rate = song->samplerate();
                info.channels = song->physical_channels();
                info.song_length = song->songlength_in_wamples();
                info.duration = *(song->display_duration());
                info.project_id = p;
                info.song_id = s;

                songs.push_back(info);

                // Add to browser
                char label[512];
                snprintf(label, sizeof(label), "%-30s  %6u Hz  %2u tracks  %s",
                        info.song_name.c_str(),
                        (unsigned int)info.sample_rate,
                        (unsigned int)info.channels,
                        info.duration.c_str());
                song_browser->add(label);
            }

            delete proj_name;
        }

        char msg[256];
        snprintf(msg, sizeof(msg), "Loaded %d song(s). Select a song to view tracks.",
                (int)songs.size());
        status_box->label(msg);
    }

    static void song_selected_cb(Fl_Widget*, void* data) {
        HD24BrowserGUI* gui = (HD24BrowserGUI*)data;
        gui->song_selected();
    }

    void song_selected() {
        int idx = song_browser->value();
        if (idx < 1 || idx > (int)songs.size()) return;

        SongInfo& song_info = songs[idx - 1];

        // Populate track browser
        track_browser->clear();
        for (__uint32 t = 1; t <= song_info.channels; t++) {
            char label[64];
            snprintf(label, sizeof(label), "Track %02u", (unsigned int)t);
            track_browser->add(label);
        }

        export_button->activate();

        char msg[256];
        snprintf(msg, sizeof(msg), "Song '%s' selected. Choose tracks to export.",
                song_info.song_name.c_str());
        status_box->label(msg);
    }

    static void select_all_cb(Fl_Widget*, void* data) {
        HD24BrowserGUI* gui = (HD24BrowserGUI*)data;
        gui->track_browser->check_all();
    }

    static void select_none_cb(Fl_Widget*, void* data) {
        HD24BrowserGUI* gui = (HD24BrowserGUI*)data;
        gui->track_browser->check_none();
    }

    static void browse_dir_cb(Fl_Widget*, void* data) {
        HD24BrowserGUI* gui = (HD24BrowserGUI*)data;
        gui->browse_directory();
    }

    void browse_directory() {
        Fl_Native_File_Chooser chooser;
        chooser.title("Choose Export Directory");
        chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
        chooser.options(Fl_Native_File_Chooser::NEW_FOLDER);
        chooser.directory(export_dir.c_str());

        if (chooser.show() == 0) {
            export_dir = chooser.filename();
            output_dir_field->value(export_dir.c_str());
        }
    }

    static void export_cb(Fl_Widget*, void* data) {
        HD24BrowserGUI* gui = (HD24BrowserGUI*)data;
        gui->export_tracks();
    }

    void export_tracks() {
        int song_idx = song_browser->value();
        if (song_idx < 1 || song_idx > (int)songs.size()) return;

        SongInfo& song_info = songs[song_idx - 1];

        // Count selected tracks
        int selected_count = 0;
        for (int i = 1; i <= track_browser->nitems(); i++) {
            if (track_browser->checked(i)) selected_count++;
        }

        if (selected_count == 0) {
            fl_alert("No tracks selected. Please select at least one track to export.");
            return;
        }

        // Create export directory
        system(("mkdir -p \"" + export_dir + "\"").c_str());

        // Export each selected track
        hd24song* song = song_info.song;
        __uint32 sample_rate = song_info.sample_rate;
        __uint32 song_length = song_info.song_length;

        const int BUFFER_SIZE = 1024;
        long sample_buffer[BUFFER_SIZE];
        unsigned char* byte_buffer = new unsigned char[BUFFER_SIZE * 3];

        int track_num = 0;
        int total_tracks = selected_count;

        for (int t = 1; t <= track_browser->nitems(); t++) {
            if (!track_browser->checked(t)) continue;

            track_num++;
            __uint32 track = t - 1;  // 0-based for song API

            char filename[512];
            snprintf(filename, sizeof(filename), "%s/%s_Track%02d.aif",
                    export_dir.c_str(),
                    song_info.song_name.c_str(),
                    t);

            char msg[256];
            snprintf(msg, sizeof(msg), "Exporting track %d of %d: %s...",
                    track_num, total_tracks, filename);
            status_box->label(msg);
            status_box->redraw();
            Fl::check();

            // Set up SNDFILE
            SF_INFO sfinfo;
            memset(&sfinfo, 0, sizeof(sfinfo));
            sfinfo.samplerate = sample_rate;
            sfinfo.channels = 1;
            sfinfo.format = SF_FORMAT_AIFF | SF_FORMAT_PCM_24;

            SNDFILE* outfile = sf_open(filename, SFM_WRITE, &sfinfo);
            if (!outfile) {
                fl_alert("Error: Could not create file: %s", filename);
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

                // Read multi-track samples
                for (__uint32 s = 0; s < samples_to_read; s++) {
                    song->getmultitracksample(sample_buffer, hd24song::READMODE_COPY);
                    unsigned long sample = sample_buffer[track];

                    // Write as 24-bit little-endian (correct byte order for AIFF)
                    byte_buffer[s * 3 + 0] = sample & 0xFF;
                    byte_buffer[s * 3 + 1] = (sample >> 8) & 0xFF;
                    byte_buffer[s * 3 + 2] = (sample >> 16) & 0xFF;
                }

                // Write raw 24-bit data
                sf_count_t written = sf_write_raw(outfile, byte_buffer, samples_to_read * 3);
                if (written != samples_to_read * 3) {
                    fl_alert("Error writing to file: %s", filename);
                    break;
                }

                samples_written += samples_to_read;

                // Update progress
                int percent = (track_num - 1) * 100 / total_tracks +
                             (samples_written * 100 / song_length) / total_tracks;
                snprintf(msg, sizeof(msg), "Exporting: %d%% complete...", percent);
                status_box->label(msg);
                status_box->redraw();
                Fl::check();
            }

            sf_close(outfile);
        }

        delete[] byte_buffer;

        status_box->label("Export completed successfully!");
        fl_message("Export completed!\n\n%d track(s) exported to:\n%s",
                  selected_count, export_dir.c_str());
    }
};

int main(int argc, char** argv) {
    HD24BrowserGUI gui;
    gui.show();
    return Fl::run();
}
