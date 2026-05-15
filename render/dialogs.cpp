#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#define NOUSER

#include "dialogs.h"
#include "tinyfiledialogs.h"

static const char* filterPatterns[] = { "*.data" };

std::string SaveFileDialog() {
    const char* result = tinyfd_saveFileDialog(
        "Save Map",      
        "map.data",        
        1, filterPatterns,   
        "Map Files (*.data)"
    );
    return result ? std::string(result) : "";
}

std::string OpenFileDialog() {
    const char* result = tinyfd_openFileDialog(
        "Open Map",          
        "",     
        1, filterPatterns,    
        "Map Files (*.data)", 
        0
    );
    return result ? std::string(result) : "";
}