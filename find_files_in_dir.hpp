#ifndef _LW_FIND_FILES_IN_DIR_
#define _LW_FIND_FILES_IN_DIR_

// readable C++ implementation, slower than C due to many mallocs

#include <string>
#include <functional>
#include <sys/types.h>
#include <dirent.h>

bool find_files_in_dir(const std::string& dir, std::function<void (const std::string& filepath)> callback)
{
	DIR *dp = opendir(dir.c_str());
	if(dp == NULL)
		return false;
	
	struct dirent* ep;
	while( (ep = readdir(dp)) )
	{
		std::string fname = ep->d_name;
		if(fname=="." || fname=="..")
			continue;
		
		std::string path = dir + "/" + fname;
		if(ep->d_type & DT_REG)
			callback(path);
		else if(ep->d_type & DT_DIR)
			find_files_in_dir(path, callback);
	}
	
	closedir(dp);
	return true;
}

/*

// old and ugly C code, faster

#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

bool find_files_in_dir(const char* dir, void (*callback)(const char* filepath))
{
	unsigned dirlen = strlen(dir);
	DIR *dp = opendir(dir);
	if(dp == NULL)
		return false;
	
	const unsigned buflen = 1024;
	char buffer[buflen];
	struct dirent* ep;
	while(ep = readdir(dp))
	{
		if(strcmp(ep->d_name, ".")==0 || strcmp(ep->d_name, "..")==0)
			continue;
		
		unsigned len = dirlen + strlen(ep->d_name) + 2;
		char* buffer2 = (len > buflen) ? (char*)malloc(len) : buffer;
		
		strcpy(buffer2, dir);
		strcat(buffer2, "/");
		strcat(buffer2, ep->d_name);
		
		if(ep->d_type & DT_REG)
			callback(buffer2);
		else if(ep->d_type & DT_DIR)
			find_files_in_dir(buffer2, callback);
		
		if(buffer2 != buffer)
			free(buffer2);
	}
	
	closedir(dp);
	return true;
}
*/

#endif // _LW_FIND_FILES_IN_DIR_
