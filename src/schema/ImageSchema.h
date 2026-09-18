#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Shape of one image, read from its file header. No pixels are decoded at
// compile time, so profiling thousands of images is cheap.
struct ImageInfo {
    std::string path;     // relative to the image set folder, with '/' separators
    std::string format;   // PNG or JPG
    int width = 0;
    int height = 0;
    int channels = 0;
    int bitDepth = 8;
    bool corrupt = false;
    std::string problem;  // why the header could not be read
    std::uintmax_t bytes = 0;
};

// LOAD_IMAGE reads one file; LOAD_IMAGES reads every PNG/JPG file of a folder.
struct ImageSetSchema {
    std::string root;               // the folder, or the file for LOAD_IMAGE
    bool singleFile = false;
    bool recursive = false;
    std::vector<ImageInfo> images;  // sorted by path
    std::uint64_t fingerprint = 0;  // CRC-32 of every path and file size, checked again at run time
};
