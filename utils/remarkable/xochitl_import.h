/**
 * Import a file to Xochitl, by copying it to the appropriate location
 * and creating necessary metadata.
 *
 * \param[in] file_path full path to the file
 * \param[in] display_name display name to set in Xochitl
 *
 * \return NSERROR_OK if import was successful or appropriate error code on failure
 */
int import_file_to_xochitl(char *file_path, char *display_name);
