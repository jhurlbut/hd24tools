#include <iostream>
#include <string>
#include <convertlib.h>
#include <hd24fs.h>

using namespace std;

void showsongs(hd24project* currentproj, int projnum)
{
	int numsongs = currentproj->songcount();

	if (numsongs == 0)
	{
		cout << "      No songs in this project." << endl;
		return;
	}

	for (int i = 1; i <= numsongs; i++)
	{
		hd24song currsong = *(currentproj->getsong(i));
		string* songname1 = new string("");

		cout << "  Song " << projnum << "." << i << ": ";
		string* currsname = currsong.songname();
		*songname1 += *currsname;

		cout << *(Convert::padright(*songname1, 20, " "));
		cout << *(currsong.display_duration()) << ", ";

		string* chans = Convert::int2str(currsong.logical_channels());
		chans = Convert::padleft(*chans,2," ");

		cout << *chans << " ch, " << currsong.samplerate() << " Hz";

		delete(currsname);
		delete(songname1);
		delete(chans);

		cout << endl;
	}
}

void showprojects(hd24fs* currenthd24)
{
	int numprojs = currenthd24->projectcount();

	if (numprojs == 0)
	{
		cout << "No projects found on this HD24 disk." << endl;
		return;
	}

	for (int i = 1; i <= numprojs; i++)
	{
		hd24project currproj = *(currenthd24->getproject(i));
		string* projname1 = new string("");
		string* currpname = currproj.projectname();
		*projname1 += *currpname;

		cout << "======================================================================" << endl;
		cout << "Project " << i << ": " << *projname1 << endl;

		showsongs (&currproj, i);

		delete(currpname);
		delete(projname1);
	}
	cout << "======================================================================" << endl;
}

int main(int argc, char** argv)
{
	const char* device = NULL;

	// Parse command line arguments
	for (int i = 1; i < argc; i++)
	{
		string arg = argv[i];
		if (arg.find("--dev=") == 0)
		{
			device = argv[i] + 6;  // Skip "--dev="
		}
	}

	hd24fs* fsysp = new hd24fs(device);
	hd24fs fsys = *fsysp;
	string* volname;

	if (!fsys.isOpen())
	{
		cout << "Cannot open HD24 device." << endl;
		if (device == NULL)
		{
			cout << "Tip: Try specifying device with --dev=/dev/rdiskX" << endl;
		}
		return 1;
	}

	int devcount = fsys.hd24devicecount();

	cout << "HD24 device(s) found: " << devcount << endl;
	cout << "======================================================================" << endl;

	for (int i = 0; i < devcount; i++)
	{
		hd24fs* currhd24 = new hd24fs(device, fsys.mode(), i);

		if (devcount > 1)
		{
			cout << "Device #" << i << endl;
		}

		volname = currhd24->volumename();
		string vname = "";
		vname += *volname;
		cout << "Volume name: " << vname << endl;
		delete volname;

		showprojects(currhd24);
		delete currhd24;
	}

	return 0;
}
