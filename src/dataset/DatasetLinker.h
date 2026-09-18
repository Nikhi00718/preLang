#pragma once

#include "sema/SymbolTable.h"

#include <string>
#include <vector>

// LINK: matches image files with the rows of a table whose column holds the
// file names (relative to the image folder, e.g. "cats/001.jpg").
// Done at compile time, from the image headers and the CSV values.
LinkState linkImages(const ImageState& images, const std::vector<std::string>& fileNames);

// The share of items that have a partner, in percent: 100 means every image
// has a label and every label has an image.
double matchRate(const LinkState& link);
