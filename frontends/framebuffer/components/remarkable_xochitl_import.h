#ifndef XOCHITL_IMPORT
#define XOCHITL_IMPORT

enum Filetype {
    pdf,
    epub,
};

/**
 * Import a file to Xochitl, by copying it to the appropriate location
 * and creating necessary metadata.
 *
 * \param[in] file_path full path to the file
 * \param[in] display_name display name to set in Xochitl
 * \param[in] file_type the type of the file (PDF or EPUB)
 *
 * \return NSERROR_OK if import was successful or appropriate error code on failure
 */
int import_file_to_xochitl(const char *file_path, const char *display_name, enum Filetype file_type);

#endif
