#pragma once

#include "certify/Certifier.h"
#include "ir/IR.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Target code for IMAGE and IMAGESET instructions: Python using OpenCV.
// Image operations are not run one by one over the whole dataset. They are
// collected into a per-image pipeline, and each SAVE_IMAGES becomes a single
// streaming pass: read one file, apply every operation, write it, move on.
// Memory use therefore stays at one image, whatever the dataset size.
class OpenCVCodegen {
public:
    // The certificate says which facts another image folder must have to be admitted.
    OpenCVCodegen(std::filesystem::path outputFolder, const Certificate& certificate);

    // The Python statements for one image instruction (or a MATCH_RATE check).
    std::string code(const Instruction& instruction, const IRProgram& program);

    // Helper functions; only the ones the program needs are included.
    static std::string helpers(bool letterbox, bool grayscale);

private:
    struct Pipeline {
        std::vector<std::string> steps;  // Python statements applied to `img`
        bool skipBroken = false;         // REMOVE_CORRUPT: skip files that fail to decode
        std::string format = "SOURCE";   // output format
        int quality = 0;                 // JPG quality, 0 for the default
    };

    std::filesystem::path outputFolder_;
    const Certificate& certificate_;
    std::map<std::string, Pipeline> pipelines_;

    std::string certificateDict(const std::string& dataset) const;

    std::string saveLoop(const Instruction& instruction);
};
