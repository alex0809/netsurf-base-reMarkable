#include <limits.h>
#include <string.h>

#include "filename_utils.h"

const char *get_extension(const char *filename)
{
	const char *dot = strrchr(filename, '.');
	if (!dot || dot == filename)
		return "";
	return dot + 1;
}

void get_filename_without_extension(const char *filename,
				    char *filename_without_extension)
{
	strcpy(filename_without_extension, filename);
	char *end = filename_without_extension +
		    strlen(filename_without_extension);
	while (end > filename_without_extension && *end != '.') {
		--end;
	}
	if (end > filename_without_extension) {
		*end = '\0';
	}
}

void get_path_url_without_filename(const char *path_with_file,
				   char *path_without_file)
{
	char buf[PATH_MAX + 10];
	strcpy(buf, path_with_file);
	char *end = buf + strlen(buf);
	while (end > buf && *end != '/') {
		--end;
	}
	if (end > buf) {
		*end = '\0';
	}

	strcpy(path_without_file, "file://");
	strcat(path_without_file, buf);
}
