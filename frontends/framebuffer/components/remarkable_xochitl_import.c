#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <uuid/uuid.h>
#include <libgen.h>

#include "remarkable_xochitl_import.h"
#include "utils/errors.h"
#include "utils/log.h"
#include "utils/filename_utils.h"

static const char *XOCHITL_DIRECTORY =
	"/home/root/.local/share/remarkable/xochitl";

/** Metadata template file parameters:
 *  - %d lastModified
 *  - %s visibleName */
static const char *METADATA_TEMPLATE = "{"
				       "    \"deleted\": false,"
				       "    \"lastModified\": \"%d\","
				       "    \"lastOpenedPage\": 0,"
				       "    \"metadatamodified\": false,"
				       "    \"modified\": false,"
				       "    \"pinned\": false,"
				       "    \"type\": \"DocumentType\","
				       "    \"version\": 0,"
				       "    \"visibleName\": \"%s\""
				       "}";

static const char *CONTENT_PDF = "{ \"fileType\": \"pdf\" }";
static const char *CONTENT_EPUB = "{ \"fileType\": \"epub\" }";

/** write in (up to length in_len) to out, with the characters escaped so the
 * result is a valid json string. There is no bounds checking for out buffer, so
 * it must be large enough in all cases.
 */
static int json_escape(char *out, const char *in)
{
	size_t i = 0;
	size_t out_idx = 0;
	size_t in_len = strlen(in);

	for (i = 0; i < in_len; i++) {
		unsigned char ch = ((unsigned char *)in)[i];
		switch (ch) {
		case '"':
			out[out_idx++] = '\\';
			out[out_idx++] = '"';
			break;
		case '/':
			out[out_idx++] = '\\';
			out[out_idx++] = '/';
			break;
		case '\b':
			out[out_idx++] = '\\';
			out[out_idx++] = 'b';
			break;
		case '\f':
			out[out_idx++] = '\\';
			out[out_idx++] = 'f';
			break;
		case '\n':
			out[out_idx++] = '\\';
			out[out_idx++] = 'n';
			break;
		case '\r':
			out[out_idx++] = '\\';
			out[out_idx++] = 'r';
			break;
		case '\t':
			out[out_idx++] = '\\';
			out[out_idx++] = 't';
			break;
		case '\\':
			out[out_idx++] = '\\';
			out[out_idx++] = '\\';
			break;
		default:
			out[out_idx++] = ch;
			break;
		}
	}
	out[out_idx++] = '\0';
	return out_idx;
}

static int copy_file(const char *dest, const char *source)
{
	FILE *dest_file = fopen(dest, "wx");
	FILE *source_file = fopen(source, "r");

	size_t read_len, write_len;
	unsigned char buf[8192];
	do {
		read_len = fread(buf, 1, sizeof buf, source_file);
		if (read_len) {
			write_len = fwrite(buf, 1, read_len, dest_file);
		} else {
			write_len = 0;
		}

	} while ((read_len > 0) && (read_len == write_len));

	NSLOG(netsurf, INFO, "File contents copied");

	if (fclose(source_file) != 0) {
		NSLOG(netsurf, WARN, "Could not close source file");
		return -1;
	}
	if (fclose(dest_file) != 0) {
		NSLOG(netsurf, WARN, "Could not close target file");
		return -1;
	}
	return 0;
}

static int write_metadata(const char *visible_name, const char *uuid)
{
	time_t timer;
	time(&timer);

	size_t metadata_contents_len = strlen(METADATA_TEMPLATE) +
				       strlen(visible_name) * 2 + 100;
	char *metadata_contents = malloc(metadata_contents_len * sizeof(char));

	char *escaped_visible_name = malloc((strlen(visible_name) * 2 + 1) *
					    sizeof(char));
	json_escape(escaped_visible_name, visible_name);

	char *metadata_file_path = malloc((PATH_MAX + 1) * sizeof(char));
	snprintf(metadata_file_path,
		 PATH_MAX + 1,
		 "%s/%s.metadata",
		 XOCHITL_DIRECTORY,
		 uuid);

	snprintf(metadata_contents,
		 metadata_contents_len,
		 METADATA_TEMPLATE,
		 timer,
		 visible_name);

	FILE *metadata_file = fopen(metadata_file_path, "wx");
	if (metadata_file == NULL) {
		NSLOG(netsurf,
		      WARN,
		      "Could not open metadata file %s for writing",
		      metadata_file_path);
		return -1;
	}
	fputs(metadata_contents, metadata_file);
	if (fclose(metadata_file) != 0) {
		NSLOG(netsurf,
		      WARN,
		      "Could not close metadata file %s",
		      metadata_file_path);
		return -1;
	}

	free(metadata_file_path);
	free(metadata_contents);
	free(escaped_visible_name);
	return 0;
}

static int write_content_file(const char *uuid, enum Filetype file_type)
{

	char *content_file_path = malloc((PATH_MAX + 1) * sizeof(char));
	snprintf(content_file_path,
		 PATH_MAX + 1,
		 "%s/%s.content",
		 XOCHITL_DIRECTORY,
		 uuid);

	FILE *content_file = fopen(content_file_path, "wx");
	if (content_file == NULL) {
		NSLOG(netsurf,
		      WARN,
		      "Could not open content file %s for writing",
		      content_file_path);
		return -1;
	}

	switch (file_type) {
	case pdf:
		fputs(CONTENT_PDF, content_file);
		break;
	case epub:
		fputs(CONTENT_EPUB, content_file);
		break;
	}

	if (fclose(content_file) != 0) {
		NSLOG(netsurf,
		      WARN,
		      "Could not close content file %s",
		      content_file_path);
		return -1;
	}

	free(content_file_path);
	return 0;
}

int import_file_to_xochitl(const char *file_path,
			   const char *display_name,
			   enum Filetype file_type)
{
	FILE *source = fopen(file_path, "r");
	if (source == NULL) {
		NSLOG(netsurf,
		      WARN,
		      "The file %s could not be opened for importing to Xochitl.",
		      file_path);
		return NSERROR_BAD_PARAMETER;
	}

	uuid_t uuid;
	uuid_generate_random(uuid);
	char *uuid_string = malloc(UUID_STR_LEN);
	uuid_unparse_lower(uuid, uuid_string);

	NSLOG(netsurf,
	      INFO,
	      "Importing file as UUID %s, from source file %s, with name %s",
	      uuid_string,
	      file_path,
	      display_name);

	char *file_path_modifiable = malloc((PATH_MAX + 1) * sizeof(char));
	strcpy(file_path_modifiable, file_path);
	char *metadata_path = malloc((PATH_MAX + 1) * sizeof(char));
	snprintf(metadata_path,
		 PATH_MAX,
		 "%s/%s.metadata",
		 XOCHITL_DIRECTORY,
		 uuid_string);
	char *pdf_path = malloc((PATH_MAX + 1) * sizeof(char));
	snprintf(pdf_path,
		 PATH_MAX,
		 "%s/%s.pdf",
		 XOCHITL_DIRECTORY,
		 uuid_string);
	char *epub_path = malloc((PATH_MAX + 1) * sizeof(char));
	snprintf(epub_path,
		 PATH_MAX,
		 "%s/%s.epub",
		 XOCHITL_DIRECTORY,
		 uuid_string);

	const char *target_path;
	if (file_type == pdf) {
		NSLOG(netsurf, INFO, "File is PDF, copying to %s", pdf_path);
		target_path = pdf_path;
	} else if (file_type == epub) {
		NSLOG(netsurf, INFO, "File is EPUB, copying to %s", epub_path);
		target_path = epub_path;
	} else {
		NSLOG(netsurf, ERROR, "Invalid file type");
		return -1;
	}

	int result = NSERROR_OK;
	if (copy_file(target_path, file_path) != 0) {
		NSLOG(netsurf, WARN, "Error copying file to %s", target_path);
		result = NSERROR_INVALID;
	} else {
		NSLOG(netsurf, INFO, "File copied to %s", target_path);
	}

	if (write_metadata(display_name, uuid_string) != 0) {
		NSLOG(netsurf,
		      WARN,
		      "Error writing metadata for %s",
		      file_path);
		result = NSERROR_INVALID;
	}

	if (write_content_file(uuid_string, file_type) != 0) {
		NSLOG(netsurf,
		      WARN,
		      "Error writing content file for %s",
		      file_path);
		result = NSERROR_INVALID;
	}

	free(uuid_string);
	free(file_path_modifiable);
	free(metadata_path);
	free(epub_path);
	free(pdf_path);
	return result;
}
