#include "dataset/DatasetLinker.h"

#include <algorithm>
#include <set>

LinkState linkImages(const ImageState& images, const std::vector<std::string>& fileNames) {
    std::set<std::string> imagePaths;
    for (const ImageShape& shape : images.shapes) {
        imagePaths.insert(shape.path);
    }
    std::set<std::string> labels;
    for (const std::string& name : fileNames) {
        if (!name.empty()) {
            labels.insert(name);
        }
    }

    LinkState link;
    link.exact = true;
    link.imageCount = static_cast<int>(imagePaths.size());
    link.labelCount = static_cast<int>(labels.size());
    for (const std::string& path : imagePaths) {
        if (labels.count(path) != 0) {
            ++link.matched;
        } else {
            link.imagesWithoutLabel.push_back(path);
        }
    }
    for (const std::string& label : labels) {
        if (imagePaths.count(label) == 0) {
            link.labelsWithoutImage.push_back(label);
        }
    }
    return link;
}

double matchRate(const LinkState& link) {
    const int larger = std::max(link.imageCount, link.labelCount);
    return larger == 0 ? 100.0 : 100.0 * link.matched / larger;
}
