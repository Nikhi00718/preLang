#include "backend/OpenCVCodegen.h"

#include "backend/PandasCodegen.h"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

std::string variable(const std::string& dataset) {
    std::string text;
    for (char c : dataset) {
        text += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
    return "images_" + text;
}

std::string hex(std::uint64_t value) {
    std::ostringstream text;
    text << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return text.str();
}

std::string number(double value) {
    std::ostringstream text;
    text << std::setprecision(10) << value;
    return text.str();
}

}  // namespace

OpenCVCodegen::OpenCVCodegen(std::filesystem::path outputFolder) : outputFolder_(std::move(outputFolder)) {}

std::string OpenCVCodegen::helpers(bool letterbox, bool grayscale) {
    std::string code = R"(IMAGE_TYPES = {".png", ".jpg", ".jpeg"}


def list_images_checked(path, recursive, single, fingerprint):
    """Lists the image files the compiler saw, and refuses to run if any file
    was added, removed or changed in size after compilation."""
    if single:
        folder, names = path.parent, [path.name]
    else:
        folder = path
        found = folder.rglob("*") if recursive else folder.glob("*")
        names = sorted(p.relative_to(folder).as_posix() for p in found
                       if p.is_file() and p.suffix.lower() in IMAGE_TYPES)
    listing = "".join(f"{name}|{(folder / name).stat().st_size}\n" for name in names)
    if zlib.crc32(listing.encode("utf-8")) != fingerprint:
        raise ContractError(f"{path} changed after compilation; compile the .prep file again")
    return folder, names


def read_image(path):
    """Decodes an image with its own channels, or returns None if it is broken."""
    data = np.fromfile(str(path), dtype=np.uint8)
    return cv2.imdecode(data, cv2.IMREAD_UNCHANGED) if data.size else None


created_folders = set()


def write_image(path, img, quality):
    if path.parent not in created_folders:  # once per folder, not once per image
        path.parent.mkdir(parents=True, exist_ok=True)
        created_folders.add(path.parent)
    if path.suffix == ".npy":
        np.save(path, img)
        return
    params = [cv2.IMWRITE_JPEG_QUALITY, quality] if quality and path.suffix.lower() in (".jpg", ".jpeg") else []
    ok, data = cv2.imencode(path.suffix, img, params)
    if not ok:
        raise ContractError(f"could not encode {path}")
    data.tofile(str(path))


)";
    if (letterbox) {
        code += R"(def letterbox(img, width, height):
    """Resizes without stretching and pads the rest with black (KEEP_ASPECT)."""
    h, w = img.shape[:2]
    scale = min(width / w, height / h)
    new_w, new_h = max(1, round(w * scale)), max(1, round(h * scale))
    img = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_AREA)
    top, left = (height - new_h) // 2, (width - new_w) // 2
    return cv2.copyMakeBorder(img, top, height - new_h - top, left, width - new_w - left,
                              cv2.BORDER_CONSTANT, value=0)


)";
    }
    if (grayscale) {
        code += R"(def to_gray(img):
    if img.ndim == 2:
        return img
    return cv2.cvtColor(img, cv2.COLOR_BGRA2GRAY if img.shape[2] == 4 else cv2.COLOR_BGR2GRAY)


)";
    }
    return code;
}

std::string OpenCVCodegen::code(const Instruction& in, const IRProgram& program) {
    Pipeline& pipeline = pipelines_[in.dataset];
    const std::string images = variable(in.dataset);
    const std::string origin = "  # line " + std::to_string(in.line);

    switch (in.op) {
        case Opcode::LoadImages: {
            const ImageSetSchema& schema = program.images.at(in.dataset);
            pipeline = Pipeline{};
            return images + "_folder, " + images + "_files = list_images_checked(\n    HERE / " +
                   pythonString(pathFrom(outputFolder_, schema.root)) + ", recursive=" +
                   (schema.recursive ? "True" : "False") + ", single=" + (schema.singleFile ? "True" : "False") +
                   ", fingerprint=" + hex(schema.fingerprint) + ",\n)\n";
        }
        case Opcode::Resize: {
            const std::string width = std::to_string(in.numbers[0]);
            const std::string height = std::to_string(in.numbers[1]);
            pipeline.steps.push_back(in.numbers[2] ? "img = letterbox(img, " + width + ", " + height + ")" + origin
                                                   : "img = cv2.resize(img, (" + width + ", " + height +
                                                         "), interpolation=cv2.INTER_AREA)" + origin);
            break;
        }
        case Opcode::Grayscale:
            pipeline.steps.push_back("img = to_gray(img)" + origin);
            break;
        case Opcode::NormalizeImages: {
            // Maps the known source range [from, to] onto [low, high].
            const double low = std::stod(in.values[0].value);
            const double high = std::stod(in.values[1].value);
            const double from = std::stod(in.values[2].value);
            const double to = std::stod(in.values[3].value);
            const double scale = (high - low) / (to - from);
            pipeline.steps.push_back("img = img.astype(np.float32) * " + number(scale) + " + " +
                                     number(low - from * scale) + origin);
            break;
        }
        case Opcode::Compress:
            pipeline.quality = in.numbers[0];
            break;
        case Opcode::Convert:
            pipeline.format = in.text;
            break;
        case Opcode::RemoveCorrupt:
            pipeline.skipBroken = true;
            break;
        case Opcode::SaveImages:
            return saveLoop(in);
        case Opcode::Check: {  // MATCH_RATE: labels and images are compared at run time
            const std::string labels = PandasCodegen::tableVariable(in.columns[1], "");
            const std::string files = variable(in.columns[0]) + "_files";
            return "names = set(" + labels + "[" + pythonString(in.columns[2]) + "].dropna())\n" +
                   "matched = len(names & set(" + files + "))\n" + "rate = 100 * matched / max(len(names), len(" +
                   files + "), 1)\n" + "if rate < " + in.values[0].value + ":\n    raise ContractError(" +
                   pythonString("line " + std::to_string(in.line) + ": " + in.dataset + " match rate is ") +
                   " + f\"{rate:.2f}%\")\n";
        }
        default:
            break;
    }
    return "# applied to each image in the streaming loop of SAVE_IMAGES\n";
}

// One pass over the files: read, run the pipeline, write. Folder structure is
// kept, so class labels stored as sub-folders (cats/, dogs/) survive.
std::string OpenCVCodegen::saveLoop(const Instruction& in) {
    const Pipeline& pipeline = pipelines_[in.dataset];
    const std::string images = variable(in.dataset);
    const bool single = in.numbers[0] == 1;
    static const std::map<std::string, std::string> suffixes = {{"JPG", ".jpg"}, {"PNG", ".png"}, {"NPY", ".npy"}};
    const auto suffix = suffixes.find(pipeline.format);
    const std::string destination = "HERE / " + pythonString(in.text);

    std::string output = single ? "target" : "target / name";
    if (suffix != suffixes.end()) {
        output = "(" + output + ").with_suffix(" + pythonString(suffix->second) + ")";
    }
    std::string code = "target = " + destination + "\nwritten = skipped = 0\nfor name in " + images + "_files:\n" +
                       "    img = read_image(" + images + "_folder / name)\n    if img is None:\n";
    code += pipeline.skipBroken ? "        skipped += 1\n        continue\n"
                                : "        raise ContractError(f\"{name} could not be decoded\")\n";
    for (const std::string& step : pipeline.steps) {
        code += "    " + step + "\n";
    }
    code += "    write_image(" + output + ", img, " + std::to_string(pipeline.quality) + ")\n    written += 1\n";
    code += "outputs.append({\"" + std::string(single ? "file" : "folder") + "\": " + pythonString(in.text) +
            ", \"images\": written, \"skipped\": skipped})\n";
    code += "print(\"Saved\", written, \"image(s) to\", target, f\"({skipped} skipped)\" if skipped else \"\")\n";
    return code;
}
