/**
 * Get only the extension of filename (as offset pointer into the original
 * string, starting where the extension begins). If the
 * file has no extension, an empty string is returned.
 */
const char *get_extension(const char *filename);
/** Write the filename with the extension removed into
 * filename_without_extension. Bounds are not checked, so the output buffer must
 * be large enough in all cases.
 */
void get_filename_without_extension(const char *filename,
				    char *filename_without_extension);
/** Write the path URL without the filename into path_without_file.
 * Bounds are not checked, so the output buffer must be large enough in all
 * cases.
 */
void get_path_url_without_filename(const char *path_with_file,
				   char *path_without_file);
