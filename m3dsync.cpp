#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <chrono>
#include <cryptopp/sha.h>
#include <sys/stat.h> // for chmod
// #include <io.h>
#include "bytes2str.hpp"
#include "find_files_in_dir.hpp"
using namespace std;

int help(const string& prog_name, const string& action)
{
	if(action == "hash")
	{
		cout<< prog_name <<" hash /some/file.mp3 [file2.avi ...]\n"
			"will write one line for each of the supplied files.\n"
			"Each line will contain the hash, a space, the size in bytes, a space, the file path.\n"
			"About the hash:\n"
			"- If two files are identical, the hashes will be identical.\n"
			"- If two files are mp3 and differ only in their ID3 tags, the hashes will most likely be identical.\n"
			"- Otherwise, the outputs will most likely be different." <<endl;
	}
	else if(action == "scan")
	{
		cout<< prog_name <<" scan DB.dat /path/to/dir [/other/path]\n"
			"will create a database in file DB.dat for all the files found in paths (like /path/to/dir) supplied as argument.\n"
			"It does this by applying the \"hash\" action to each file found in the supplied paths." <<endl;
	}
	else if(action == "comp")
	{
		cout<< prog_name <<" comp DB-A.dat DB-B.dat\n"
			"will compare the databases in DB-A.dat and DB-B.dat. It will create the following files:\n"
			"only-on-DB-A.txt  - containing (line by line) the paths to all files that are only in DB-A\n"
			"only-on-DB-B.txt  - containing (line by line) the paths to all files that are only in DB-B\n"
			"copy-from-DB-A.sh - a script allowing to copy all files only A has to a destination (like external drive)\n"
			"copy-from-DB-B.sh - a script allowing to copy all files only B has to a destination (like external drive)"<<endl;
	}
	else if(action == "lsdup")
	{
		cout<< prog_name <<" lsdup DB.dat dup.txt\n"
			"will scan all files that have the same hash in DB.dat (and are therefore most likely identical).\n"
			"A report is written to dup.txt ."<<endl;
	}
	else
	{
		cout<< "m3dsync version 1.0.0\n"
			"usage: "<< prog_name <<" action arguments\n"
			"where action is one from the following examples:\n"
			<< prog_name <<" help [action]\n"
			<< prog_name <<" hash /some/file.mp3 [file2.avi ...]\n"
			<< prog_name <<" scan DB.dat /path/to/dir [/other/path]\n"
			<< prog_name <<" comp DB-A.dat DB-B.dat\n"
			<< prog_name <<" lsdup DB.dat dup.txt" <<endl;
		
		if(action != "")
		{
			cerr<<"unknown action \""<< action <<"\""<<endl;
			return 1;
		}
	}
	return 0;
}

int mp3hash(const string& filepath, ostream& outs=cout)
{
	const unsigned id31size = 128;
	const unsigned hash_len = 64; // 64 bytes == 512 bits
	
	ifstream mp3file(filepath.c_str());
	if(! mp3file)
	{
		cerr<<"Error: Could not open file \""<<filepath<<"\" for reading."<<endl;
		return 1;
	}
	
	// get file size
	mp3file.seekg(0, ios::end);
	unsigned filesize = mp3file.tellg();
	mp3file.seekg(0, ios::beg);
	if(filesize == -1) cerr<<"Error: Could not determine size of file \""<<filepath<<"\"."<<endl;
	
	// choosing hashing method (how much to read from file)
	string prefix;
	unsigned sample_size, skipback = 0;
	
	if(filesize >= id31size)
	{
		// check if we got id3v1. if so, remember id3 offset (128 bytes)
		mp3file.seekg(filesize - id31size);
		
		if(mp3file.get() == 'T' && mp3file.get() == 'A' && mp3file.get() == 'G')
			skipback = id31size;
	}
	
	if(filesize < 100*1024 + skipback)
	{
		// file is maller than 100 KiB -> read in full file to memory and hash it
		// (same result as sha512sum utility would produce)
		prefix = "0F-";
		sample_size = filesize;
	}
	else if(filesize < 1048576 + skipback)
	{
		// file is greater than 100 KiB, but smaller than 1 MiB -> hash last 100 KiB
		prefix = "01-";
		sample_size = 100*1024;
	}
	else if(filesize < 100*1048576 + skipback)
	{
		// file is greater than 1 MiB, but smaller than 100 MiB -> hash last 1 MiB
		prefix = "02-";
		sample_size = 1048576;
	}
	else
	{
		// file is greater than 100 MiB -> hash 1 MiB, 50 MiB before end
		prefix = "03-";
		sample_size = 1048576;
		skipback += 50*1048576;
	}
	
	// allocate buffer, read sample from disk to buffer and hash
	CryptoPP::SHA512 hashsum;
	byte sha512hash[hash_len];
	char* sample_buffer = new char[sample_size];
	mp3file.seekg(filesize - sample_size - skipback);
	mp3file.read(sample_buffer, sample_size);
	hashsum.Update((byte*)sample_buffer, sample_size);
	hashsum.Final(sha512hash);
	delete [] sample_buffer;
	
	// generate hash as hexadecimal string
	string hexhash(2*hash_len, 0);
	const unsigned char hex[] = "0123456789abcdef";
	unsigned n = 0;
	for(unsigned i = 0; i < hash_len; ++i)
	{
		unsigned short c = (unsigned short)(sha512hash[i]);
		if(c < 256) // should always be true
		{
			hexhash[n++] = hex[c / 16];
			hexhash[n++] = hex[c % 16];
		}
	}
	const string mp3hash_ascii = prefix+hexhash;
	
	outs<< mp3hash_ascii <<' '<< filesize <<' '<< filepath <<endl;
	
	return 0;
}

int scan(const string& DBpath, const vector<string>& dirpaths)
{
	auto t0 = chrono::high_resolution_clock::now();
	
	ofstream db_file(DBpath.c_str());
	if(! db_file)
	{
		cerr<<"Error: Could not open file \""<< DBpath <<"\" for writing."<<endl;
		return 1;
	}
	
	// mimic find $dirpath -find f -exec mp3hash {} \;
	auto hash2file = [&db_file](const string& fileToBeHashed) {
		mp3hash(fileToBeHashed, db_file);
	};
	
	for(auto dirpath: dirpaths)
		find_files_in_dir(dirpath, hash2file);
	
	auto t1 = chrono::high_resolution_clock::now();
	cout<<"Created database file \""<< DBpath <<"\" in about "<< chrono::duration_cast<chrono::seconds>(t1-t0).count() <<" seconds."<<endl;
	
	return 0;
}

int comp(const string (&dbPaths)[2], const string (&onlyPaths)[2], const string (&copyPaths)[2])
{
	auto t0 = chrono::high_resolution_clock::now();
	
	// load db_files
	unordered_multimap<string, unsigned> ummap[2];
	ifstream db_files[2];
	for(int f = 0; f < 2; ++f)
	{
		db_files[f].open(dbPaths[f].c_str());
		if(! db_files[f])
		{
			cerr<<"Error: Could not open file \""<<dbPaths[f]<<"\" for reading."<<endl;
			return 1;
		}
		
		string hash;
		while(db_files[f].good())
		{
			unsigned fpos = db_files[f].tellg();
			getline(db_files[f], hash, ' '); // read from line start to first space
			db_files[f].ignore(numeric_limits<streamsize>::max(), '\n'); // ignore rest of the line
			// ummap[f].emplace(hash, pos);
			ummap[f].insert(pair<string, unsigned>(hash, fpos));
		}
	}
	
	// create output txt files
	ofstream txt_files[2];
	for(int h = 0; h < 2; ++h)
	{
		txt_files[h].open(onlyPaths[h].c_str());
		if(! txt_files[h])
		{
			cerr<<"Error: Could not open file \""<<onlyPaths[h]<<"\" for writing."<<endl;
			return 1;
		}
	}
	
	// create output sh files
	ofstream sh_files[2];
	for(int h = 0; h < 2; ++h)
	{
		sh_files[h].open(copyPaths[h].c_str());
		if(! sh_files[h])
		{
			cerr<<"Error: Could not open file \""<<copyPaths[h]<<"\" for writing."<<endl;
			return 1;
		}
		sh_files[h]<<
			"#!/bin/bash\n"
			"dest=\"$1\"\n"
			"if [ -z \"$dest\" ]; then\n"
			"\techo \"usage: $0 /mnt/destination/path\"\n"
			"\texit 1\n"
			"fi\n";
		
		chmod(copyPaths[h].c_str(), S_IWRITE | S_IREAD | S_IEXEC); // return value not checked, if it fails... we dont care
	}
	
	for(int f = 0; f < 2; ++f)
	{
		// create diff
		db_files[f].clear(); // we have read until file read pointer switched to !good(), so reset state
		string line;
		unsigned long long mem_sum = 0;
		vector<string> missing_files;
		for(auto element: ummap[f])
		{
			auto not_found = ummap[1-f].end();
			if(ummap[1-f].find(element.first) == not_found) // if in file (f), but not in file (1-f)
			{
				db_files[f].seekg(element.second); // go back into to the corresponding line in the file
				getline(db_files[f], line); // read the complete line
				
				unsigned pos = line.find(' ');
				unsigned pos2 = line.find(' ', pos+1); // find second occurance of a space
				string path = line.substr(pos2+1);
				// if(path.empty()) continue;
				missing_files.push_back(path); // remember missing path
				
				mem_sum += stoll(line.substr(pos+1, pos2)); // add up file sizes
			}
		}
		
		cout<< missing_files.size() << " of "<< ummap[f].size() <<" files are only in "<<(f==0?"first":"second")<<" DB. "
			"They take "<< LW::bytes2str(mem_sum) <<" of disk memory."<<endl;
		
		sort(missing_files.begin(), missing_files.end());
		
		// write diff to txt files
		for(auto missing_file: missing_files)
			txt_files[f] << missing_file << '\n';
		
		// write diff to sh files
		// get common prefix
		string common_prefix = missing_files.front();
		unsigned ppos = common_prefix.length();
		for(auto missing_file: missing_files)
		{
			if(missing_file.empty()) continue;
			for(unsigned i = 0; i < ppos; ++i)
				if(common_prefix[i] != missing_file[i])
					ppos = i;
		}
		// common_prefix = common_prefix.substr(0, ppos);
		
		// mkdir commands
		string last_dir = "#";
		for(auto missing_file: missing_files)
		{
			string dir = missing_file.substr(ppos, missing_file.rfind('/'));
			if(dir != last_dir)
			{
				last_dir = dir;
				sh_files[f]<<"mkdir -p \"$dest/"<< dir <<"\"\n";
			}
		}
		
		// cp commands
		for(auto missing_file: missing_files)
		{
			string dest_path = missing_file.substr(ppos, string::npos);
			sh_files[f]<<"cp \""<< missing_file <<"\" \"$dest/"<< dest_path <<"\"\n";
		}
	}
	
	auto t1 = chrono::high_resolution_clock::now();
	cout<<"Comparision done in about "<< chrono::duration_cast<chrono::milliseconds>(t1-t0).count() <<" ms.\n"
		"Use those files:\n"<< onlyPaths[0] <<'\n'<< onlyPaths[1] <<'\n'<< copyPaths[0] <<'\n'<< copyPaths[1] <<endl;
	
	return 0;
}

int lsdup(const string& DBpath, const string& duppath)
{
	auto t0 = chrono::high_resolution_clock::now();
	
	ifstream db_file(DBpath);
	if(! db_file)
	{
		cerr<<"Error: Could not open file \""<< DBpath <<"\" for reading."<<endl;
		return 1;
	}
	
	ofstream out_file(duppath);
	if(! out_file)
	{
		cerr<<"Error: Could not open file \""<< duppath <<"\" for writing."<<endl;
		return 1;
	}
	
	vector<string> lines;
	string line;
	while(db_file.good())
	{
		getline(db_file, line);
		lines.push_back(line);
	}
	
	sort(lines.begin(), lines.end());
	
	unsigned ngroups = 0;
	unsigned long long wasted_mem = 0;
	string last_hash = "#", last_path = "#";
	unsigned long long last_mem = 0;
	bool last_were_equal = false;
	for(auto line: lines)
	{
		unsigned pos = line.find(' ');
		unsigned pos2 = line.find(' ', pos+1);
		string hash = line.substr(0, pos);
		string size = line.substr(pos+1, pos2);
		string path = line.substr(pos2+1);
		if(path.empty()) continue;
		unsigned long long mem = stoll(size);
		if(hash == last_hash)
		{
			out_file<< last_path <<'\n';
			wasted_mem += last_mem;
			last_were_equal = true;
		}
		else
		{
			if(last_were_equal)
			{
				++ngroups;
				wasted_mem += last_mem;
				out_file<< last_path <<"\n\n";
			}
			last_were_equal = false;
		}
		
		last_hash = hash;
		last_path = path;
		last_mem  = mem;
	}
	
	auto t1 = chrono::high_resolution_clock::now();
	cout<<"Found "<< ngroups <<" group of duplicates in about "<< chrono::duration_cast<chrono::milliseconds>(t1-t0).count() <<" ms.\n"
		"Potentially wasting "<< LW::bytes2str(wasted_mem) <<". "
		"See file \""<< duppath <<"\"."<<endl;
	
	return 0;
}

int main(int argc, char** argv)
{
	// handle command line arguments and call above functions accordingly
	const string prog_name = argv[0];
	if(argc <= 1)
		return help(prog_name, "");
	
	const string action = argv[1];
	if(action == "help")
		return help(prog_name, argc <= 2 ? "" : argv[2]);
	else if(action == "hash")
	{
		if(argc <= 2)
			return help(prog_name, action);
		
		int ret = 0;
		for(int k = 2; k < argc; ++k)
		{
			if(! mp3hash(argv[k]) )
				ret = 1;
		}
		return ret;
	}
	else if(action == "scan")
	{
		if(argc <= 3)
			return help(prog_name, action);
		
		const string DBpath  = argv[2];
		vector<string> dirpaths;
		for(int k = 3; k < argc; ++k)
			dirpaths.push_back(argv[k]);
		
		return scan(DBpath, dirpaths);
	}
	else if(action == "comp")
	{
		if(argc <= 3)
			return help(prog_name, action);
		
		string dbPaths[2] = {argv[2], argv[3]};
		string::size_type pos[2] = {dbPaths[0].rfind('/'), dbPaths[1].rfind('/')};
		if(pos[0] != string::npos) dbPaths[0] = dbPaths[0].substr(pos[0]+1);
		if(pos[1] != string::npos) dbPaths[1] = dbPaths[1].substr(pos[1]+1);
		const string bases[2] = {
			dbPaths[0].substr(0, dbPaths[0].rfind(".dat")),
			dbPaths[1].substr(0, dbPaths[1].rfind(".dat"))
		};
		const string onlyPaths[2] = {"only-on-"+bases[0]+".txt", "only-on-"+bases[1]+".txt"};
		const string copyPaths[2] = {"copy-from-"+bases[0]+".sh", "copy-from-"+bases[1]+".sh"};
		
		cout<<onlyPaths[0]<<endl;
		cout<<onlyPaths[1]<<endl;
		cout<<copyPaths[0]<<endl;
		cout<<copyPaths[1]<<endl;
		
		return comp(dbPaths, onlyPaths, copyPaths);
	}
	else if(action == "lsdup")
	{
		if(argc <= 3)
			return help(prog_name, action);
		
		const string DBpath  = argv[2];
		const string duppath = argv[3];
		
		return lsdup(DBpath, duppath);
	}
	else
	{
		cerr<<"unknown action \""<< action <<"\""<<endl;
		return 1;
	}
	return 0;
}
