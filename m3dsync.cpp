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
#include "string_replace.hpp"
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
		cout<< prog_name <<" comp DB-A.dat DB-B.dat [/output/basedir]\n"
			"will compare the databases in DB-A.dat and DB-B.dat. It will create the following files:\n"
			"only-on-DB-A.txt  - containing (line by line) the paths to all files that are only in DB-A\n"
			"only-on-DB-B.txt  - containing (line by line) the paths to all files that are only in DB-B\n"
			"copy-from-DB-A.sh - a script allowing to copy all files only A has to a destination (like external drive)\n"
			"copy-from-DB-B.sh - a script allowing to copy all files only B has to a destination (like external drive)\n"
			"matches-from-DB-A-to-DB-B.dat - line by line each path in DB-A [tab] first match in DB-B\n"
			"matches-from-DB-B-to-DB-A.dat - line by line each path in DB-B [tab] first match in DB-A\n\n"
			"If /output/basedir is provided, all above output files will be created there. Otherwise, they are created in the current working directory (possibly overwriting files with the same names)."<<endl;
	}
	else if(action == "lsdup")
	{
		cout<< prog_name <<" lsdup DB.dat dup.txt\n"
			"will scan all files that have the same hash in DB.dat (and are therefore most likely identical).\n"
			"A report is written to dup.txt ."<<endl;
	}
	else
	{
		cout<< "m3dsync version 1.2.0\n"
			"usage: "<< prog_name <<" action arguments\n"
			"where action is one from the following examples:\n"
			<< prog_name <<" help [action]\n"
			<< prog_name <<" hash /some/file.mp3 [file2.avi ...]\n"
			<< prog_name <<" scan DB.dat /path/to/dir [/other/path]\n"
			<< prog_name <<" comp DB-A.dat DB-B.dat [/output/basedir]\n"
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
	if((streampos)(filesize) == (streampos)(-1)) cerr<<"Error: Could not determine size of file \""<<filepath<<"\"."<<endl;
	
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
	
	if(filepath.rfind('\n') == string::npos) // NTFS allows line breaks in filenames...
		outs<< mp3hash_ascii <<' '<< filesize <<' '<< filepath <<endl;
	else // try to avoid some trouble
	{
		string filepath2 = filepath;
		replace(filepath2.begin(), filepath2.end(), '\n', ' ');
		outs<< mp3hash_ascii <<' '<< filesize <<' '<< filepath2 <<endl;
	}
	
	return 0;
}

int scan(const string& DBpath, const vector<string>& dirpaths)
{
	const auto t0 = chrono::high_resolution_clock::now();
	
	ofstream db_file(DBpath.c_str());
	if(! db_file)
	{
		cerr<<"Error: Could not open file \""<< DBpath <<"\" for writing."<<endl;
		return 1;
	}
	
	cout<<"Scanning files... (Please wait.)"<<endl;
	
	// mimic find $dirpath -find f -exec mp3hash {} \;
	auto hash2file = [&db_file](const string& fileToBeHashed) {
		mp3hash(fileToBeHashed, db_file);
	};
	
	for(auto& dirpath: dirpaths)
		find_files_in_dir(dirpath, hash2file);
	
	const auto t1 = chrono::high_resolution_clock::now();
	cout<<"Created database file \""<< DBpath <<"\" in about "<< chrono::duration_cast<chrono::seconds>(t1-t0).count() <<" seconds."<<endl;
	
	return 0;
}

int comp(const string (&dbPaths)[2], const string (&onlyPaths)[2], const string (&copyPaths)[2], const string (&matchPaths)[2])
{
	const auto t0 = chrono::high_resolution_clock::now();
	
	// load db_files
	unordered_multimap<string, size_t> ummap[2];
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
			size_t fpos = db_files[f].tellg();
			getline(db_files[f], hash, ' '); // read from line start to first space
			db_files[f].ignore(numeric_limits<streamsize>::max(), '\n'); // ignore rest of the line
			// ummap[f].emplace(hash, pos);
			if(! hash.empty())
				ummap[f].insert(pair<string, size_t>(hash, fpos));
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
	
	// create output match files
	ofstream match_files[2];
	for(int f = 0; f < 2; ++f)
	{
		match_files[f].open(matchPaths[f].c_str());
		if(! match_files[f])
		{
			cerr<<"Error: Could not open file \""<< matchPaths[f] <<"\" for writing."<<endl;
			return 1;
		}
	}
	
	// create diff
	db_files[0].clear(); // we have read until file read pointer switched to !good(), so reset state
	db_files[1].clear();
	for(int f = 0; f < 2; ++f)
	{
		string line, line2;
		unsigned long long mem_sum = 0;
		vector<string> missing_files;
		const auto not_found = ummap[1-f].end();
		for(const auto& element: ummap[f])
		{
			db_files[f].seekg(element.second); // go back into to the corresponding line in the file
			getline(db_files[f], line); // read the complete line
			if(! db_files[f].good()) cerr<<"! db_files["<<f<<"]"<<endl;
			try
			{
				const size_t pos = line.find(' ');
				if(pos == string::npos)
					throw invalid_argument("no space found");
				
				const size_t pos2 = line.find(' ', pos+1); // find second occurrence of a space
				if(pos2 == string::npos)
					throw invalid_argument("no second space found");
				
				const string path = line.substr(pos2+1);
				const auto first_matching_partner = ummap[1-f].find(element.first);
				if(first_matching_partner == not_found) // if in file (f), but not in file (1-f)
				{
					missing_files.push_back(path); // remember missing path
					mem_sum += stoull(line.substr(pos+1, pos2)); // add up file sizes
				}
				else
				{
					db_files[1-f].seekg(first_matching_partner->second);
					getline(db_files[1-f], line2);
					match_files[f] << path <<"\t"<< &line2.at(line2.find(' ', line2.find(' ')+1)+1) <<"\n";
				}
			}
			catch(const logic_error& e) // std::invalid_argument
			{
				cerr<<"# Ignored improperly formatted line \""<< line <<"\" ("<< e.what() <<")."<<endl;
			}
		}
		
		cout<< missing_files.size() << " of "<< ummap[f].size() <<" files are only in "<<(f==0?"first":"second")<<" DB. "
			"They take "<< LW::bytes2str(mem_sum) <<" of disk memory."<<endl;
		
		sort(missing_files.begin(), missing_files.end());
		
		// write diff to txt files
		for(auto& missing_file: missing_files)
			txt_files[f] << missing_file << '\n';
		
		// write diff to sh files
		// get common prefix
		string common_prefix = "";
		size_t ppos = 0;
		if(! missing_files.empty())
		{
			missing_files.front();
			ppos = common_prefix.length();
			for(auto& missing_file: missing_files)
			{
				if(missing_file.empty()) continue;
				for(size_t i = 0; i < ppos; ++i)
					if(common_prefix[i] != missing_file[i])
						ppos = i;
			}
			// common_prefix = common_prefix.substr(0, ppos);
		}
		
		// mkdir commands
		string last_dir = "#";
		for(auto& missing_file: missing_files)
		{
			const size_t pos_last_slash = missing_file.rfind('/');
			const string dir = missing_file.substr(ppos, pos_last_slash>ppos ? pos_last_slash-ppos : string::npos);
			if(dir != last_dir)
			{
				last_dir = dir;
				sh_files[f]<<"mkdir -p \"$dest/"<< dir <<"\"\n";
			}
		}
		
		// cp commands
		for(auto& missing_file: missing_files)
		{
			string dest_path = missing_file.substr(ppos, string::npos);
			sh_files[f]<<"cp \""<< string_replace(missing_file, "`", "\\`") <<"\" \"$dest/"<< dest_path <<"\"\n";
		}
	}
	
	const auto t1 = chrono::high_resolution_clock::now();
	cout<<"Comparision done in about "<< chrono::duration_cast<chrono::milliseconds>(t1-t0).count() <<" ms.\n"
		"Use those files:\n"<< onlyPaths[0] <<'\n'<< onlyPaths[1] <<'\n'<< copyPaths[0] <<'\n'<< copyPaths[1] << '\n' << matchPaths[0] <<'\n'<< matchPaths[1] <<endl;
	
	return 0;
}

int lsdup(const string& DBpath, const string& duppath)
{
	const auto t0 = chrono::high_resolution_clock::now();
	
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
	
	sort(lines.begin(), lines.end()); // after sort, same hashes will be on consecutive lines
	
	struct dup_group
	{
		unsigned long long mem_sum;
		vector<string> paths;
	};
	vector<dup_group> dup_groups;
	
	unsigned long long wasted_mem = 0;
	string last_hash = "#", last_path = "#";
	unsigned long long last_mem = 0;
	bool last_were_equal = false;
	for(auto& line: lines)
	{
		try
		{
			const size_t pos = line.find(' ');
			if(pos == string::npos)
				throw invalid_argument("no space found");
			
			const size_t pos2 = line.find(' ', pos+1);
			if(pos2 == string::npos)
				throw invalid_argument("no second space found");
			
			const string hash = line.substr(0, pos);
			const string size = line.substr(pos+1, pos2);
			const string path = line.substr(pos2+1);
			if(path.empty())
				throw invalid_argument("empty path");
			
			const unsigned long long mem = stoull(size);
			const bool hashes_are_equal = (hash == last_hash);
			if(hashes_are_equal)
			{
				if(last_were_equal)
				{
					dup_groups.back().mem_sum += mem;
					dup_groups.back().paths.push_back(path);
					wasted_mem += mem;
				}
				else
					dup_groups.push_back({last_mem + mem, {last_path, path}});
			}
			last_were_equal = hashes_are_equal;
			
			last_hash = hash;
			last_path = path;
			last_mem  = mem;
		}
		catch(const logic_error& e)
		{
			cerr<<"# Ignored improperly formatted line \""<< line <<"\"... ("<< e.what() <<")."<<endl;
		}
	}
	
	// sort groups descending by memory
	sort(dup_groups.begin(), dup_groups.end(), [](const dup_group& g, const dup_group& h) {return g.mem_sum > h.mem_sum;});
	
	for(const dup_group& g: dup_groups)
	{
		out_file<<"# "<< LW::bytes2str(g.mem_sum) <<'\n';
		for(const string& path: g.paths)
			out_file<< path <<'\n';
		
		out_file<<'\n';
	}
	
	const auto t1 = chrono::high_resolution_clock::now();
	cout<<"Found "<< dup_groups.size() <<" group of duplicates in about "<< chrono::duration_cast<chrono::milliseconds>(t1-t0).count() <<" ms.\n"
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
		
		const string dbPaths[2] = {argv[2], argv[3]};
		const string basedir = argc>4 ? argv[4] : ".";
		const string::size_type pos[2] = {dbPaths[0].rfind('/'), dbPaths[1].rfind('/')};
		const string bases[2] = {
			(pos[0] == string::npos) ? dbPaths[0] : dbPaths[0].substr(pos[0]+1, dbPaths[0].rfind(".dat")-pos[0]-1),
			(pos[1] == string::npos) ? dbPaths[1] : dbPaths[1].substr(pos[1]+1, dbPaths[1].rfind(".dat")-pos[1]-1)
		};
		
		return comp(dbPaths, {
			basedir+"/only-on-"+bases[0]+".txt",
			basedir+"/only-on-"+bases[1]+".txt"}, {
			basedir+"/copy-from-"+bases[0]+".sh",
			basedir+"/copy-from-"+bases[1]+".sh"}, {
			basedir+"/matches-from-"+bases[0]+"-to-"+bases[1]+".dat",
			basedir+"/matches-from-"+bases[1]+"-to-"+bases[0]+".dat"}
		);
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
