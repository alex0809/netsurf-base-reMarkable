#include <stddef.h>

#include "xochitl_import.h"
#include "utils/errors.h"

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

/** write in (up to length in_len) to out, with the characters escaped so the
 * result is a valid json string. There is no bounds checking for out buffer, so
 * it must be large enough in all cases.
 */
static void json_escape(char *out, const char *in, size_t in_len, size_t out_len)
{
	size_t i = 0;
	size_t out_idx = 0;

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
}

int import_file_to_xochitl(char *file_path, char *display_name)
{

	return NSERROR_OK;
}

