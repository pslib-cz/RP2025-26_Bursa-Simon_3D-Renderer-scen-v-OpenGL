#include "dialogs.h"
#define NOGDI
#define NOUSER
#include <windows.h>
#include <commdlg.h>

std::string SaveFileDialog() {
    char fileName[MAX_PATH] = "map.data";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Map Files (*.data)\0*.data\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "data";
    if (GetSaveFileNameA(&ofn)) return std::string(fileName);
    return "";
}

std::string OpenFileDialog() {
    char fileName[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Map Files (*.data)\0*.data\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) return std::string(fileName);
    return "";
}