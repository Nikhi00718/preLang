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

OpenCVCodegen::OpenCVCodegen(std::filesystem::path outputFolder, const Certificate& certificate)
    : outputFolder_(std::move(outputFolder)), certificate_(certificate) {}

// {"depths": [8], "broken_allowed": False, "broken_for": "...", "groups": {"jpg": {...}}}
std::string OpenCVCodegen::certificateDict(const std::string& dataset) const {
    const ImageCertificate* image = certificate_.image(dataset);
    if (certificate_.exactOnly || !image || image->fixed) {
        return "None";  // only the compiled folder is accepted
    }
    return imageDict(*image);
}

std::string OpenCVCodegen::imageDict(const ImageCertificate& certificate) {
    if (certificate.fixed) {
        return "{\"fixed\": True}";
    }
    const ImageCertificate* image = &certificate;
    const auto join = [](const std::vector<std::string>& items) {
        std::string text;
        for (const std::string& item : items) {
            text += (text.empty() ? "" : "; ") + item;
        }
        return pythonString(text);
    };
    const auto numbers = [](const std::vector<int>& values) {
        std::string text = "[";
        for (size_t i = 0; i < values.size(); ++i) {
            text += (i > 0 ? ", " : "") + std::to_string(values[i]);
        }
        return text + "]";
    };
    std::string code = "{\"depths\": " + numbers(image->depths) +
                       ", \"broken_allowed\": " + (image->brokenAllowed ? "True" : "False") +
                       ", \"broken_for\": " + join(image->brokenFor) + ",\n     \"groups\": {";
    for (size_t i = 0; i < image->groups.size(); ++i) {
        const ImageGroupFacts& g = image->groups[i];
        code += std::string(i > 0 ? ",\n                " : "") + "\"" + g.extension +
                "\": {\"channels\": " + numbers(g.channels) + ", \"channels_for\": " + join(g.channelsFor) +
                ", \"width\": [" + std::to_string(g.minWidth) + ", " + std::to_string(g.maxWidth) +
                "], \"width_for\": [" + join(g.minWidthFor) + ", " + join(g.maxWidthFor) + "], \"height\": [" +
                std::to_string(g.minHeight) + ", " + std::to_string(g.maxHeight) + "], \"height_for\": [" +
                join(g.minHeightFor) + ", " + join(g.maxHeightFor) + "]}";
    }
    return code + "}}";
}

std::string OpenCVCodegen::helpers(bool letterbox, bool grayscale) {
    std::string code = R"(IMAGE_TYPES = {".png", ".jpg", ".jpeg"}


broken_images = {}  # image input -> files whose header cannot be read


def read_image_header(path):
    """(width, height, channels, bits) from the PNG or JPEG header, or None if the
    header cannot be read. The same rules as the compiler: the format comes from
    the first bytes, and no pixel is decoded."""
    with open(path, "rb") as file:
        head = file.read(26)
        if head[:2] == b"\x89P" or (path.suffix.lower() == ".png" and head[:2] != b"\xff\xd8"):
            if len(head) < 26 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
                return None
            width, height = int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")
            channels = {0: 1, 2: 3, 3: 3, 4: 4, 6: 4}.get(head[25])
            return (width, height, channels, head[24]) if channels and width > 0 and height > 0 else None
        if head[:2] != b"\xff\xd8":
            return None
        file.seek(2)
        while True:
            if file.read(1) != b"\xff":
                return None
            marker = file.read(1)
            while marker == b"\xff":
                marker = file.read(1)
            if not marker or marker[0] in (0xD9, 0xDA):
                return None
            if 0xD0 <= marker[0] <= 0xD7 or marker[0] == 0x01:
                continue
            size = file.read(2)
            if len(size) < 2 or int.from_bytes(size, "big") < 2:
                return None
            if 0xC0 <= marker[0] <= 0xCF and marker[0] not in (0xC4, 0xC8, 0xCC):
                frame = file.read(6)
                if len(frame) < 6:
                    return None
                width, height = int.from_bytes(frame[3:5], "big"), int.from_bytes(frame[1:3], "big")
                return (width, height, frame[5], frame[0]) if width > 0 and height > 0 and frame[5] > 0 else None
            file.seek(int.from_bytes(size, "big") - 2, 1)


def admit_images(name, path, folder, names, certificate):
    """A folder other than the compiled one may be used only if every file's header
    has the facts of the data certificate. Returns the files that cannot be read."""
    if certificate is None:
        refuse(path, "the compiled program's proofs depend on the exact files of its folder", "")
    if not names:
        refuse(path, "it has no PNG or JPG images", "")
    broken, depths, groups = [], set(), {}
    for file in names:
        header = read_image_header(folder / file)
        if header is None:
            if not certificate["broken_allowed"]:
                refuse(path, f"{file} cannot be read", certificate["broken_for"])
            broken.append(file)
            continue
        width, height, channels, bits = header
        depths.add(16 if bits > 8 else 8)
        suffix = Path(file).suffix.lower()
        facts = groups.setdefault("png" if suffix == ".png" else "jpg",
                                  {"channels": set(), "width": [width, width], "height": [height, height]})
        facts["channels"].add(channels)
        facts["width"] = [min(facts["width"][0], width), max(facts["width"][1], width)]
        facts["height"] = [min(facts["height"][0], height), max(facts["height"][1], height)]
        group = certificate["groups"].get("png" if suffix == ".png" else "jpg")
        if group is None:
            refuse(path, f"{file} is a {suffix} file, which the compiled folder had none of", "")
        if channels not in group["channels"]:
            refuse(path, f"{file} has {channels} channels", group["channels_for"])
        for value, (low, high), (low_for, high_for), what in ((width, group["width"], group["width_for"], "width"),
                                                              (height, group["height"], group["height_for"], "height")):
            if not low <= value <= high:
                refuse(path, f"{file} has {what} {value}", low_for if value < low else high_for)
    if depths and sorted(depths) != certificate["depths"]:
        refuse(path, f"its bit depths {sorted(depths)} differ from the compiled {certificate['depths']}", "")
    print(f"Admitted {path} for {name}: every image header satisfies the data certificate")
    image_facts[name] = {"broken": len(broken), "groups": groups}
    return broken


def image_problem(facts, certificate):
    """Why a new folder breaks the item certificate of a removed operation; "" if it does not."""
    if certificate.get("fixed"):
        return "the folder is not the compiled one"
    if facts["broken"] and not certificate["broken_allowed"]:
        return f"{facts['broken']} files cannot be read"
    for extension, found in facts["groups"].items():
        group = certificate["groups"].get(extension)
        if group is None:
            continue
        other = sorted(found["channels"] - set(group["channels"]))
        if other:
            return f"a .{extension} image has {other[0]} channels"
        for what in ("width", "height"):
            (low, high), (least, most) = group[what], found[what]
            if least < low or most > high:
                return f".{extension} images have {what}s {least} to {most}, outside {low} to {high}"
    return ""


def list_images_checked(name, path, recursive, single, fingerprint, broken, certificate):
    """Lists the image files. If the names and sizes are those the compiler saw,
    every proof holds; another folder is admitted only if it satisfies the data
    certificate. `broken` lists the compiled folder's files that cannot be read."""
    path = Path(INPUTS[name]).resolve() if name in INPUTS else path
    if single:
        folder, names = path.parent, [path.name]
    else:
        folder = path
        found = folder.rglob("*") if recursive else folder.glob("*")
        names = sorted(p.relative_to(folder).as_posix() for p in found
                       if p.is_file() and p.suffix.lower() in IMAGE_TYPES)
    listing = "".join(f"{file}|{(folder / file).stat().st_size}\n" for file in names)
    same_input[name] = zlib.crc32(listing.encode("utf-8")) == fingerprint or TRUST_INPUTS
    broken_images[name] = broken if same_input[name] else admit_images(name, path, folder, names, certificate)
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

std::string OpenCVCodegen::code(const Instruction& in, const IRProgram& program, const std::string& guard) {
    Pipeline& pipeline = pipelines_[in.dataset];
    const std::string images = variable(in.dataset);
    const std::string origin = "  # line " + std::to_string(in.line);
    // A speculatively removed step runs only for a folder that breaks its item certificate.
    const std::string when = guard.empty() ? "" : "if deopt(" + pythonString(guard) + "): ";

    switch (in.op) {
        case Opcode::LoadImages: {
            const ImageSetSchema& schema = program.images.at(in.dataset);
            pipeline = Pipeline{};
            std::vector<std::string> broken;
            for (const ImageInfo& file : schema.images) {
                if (file.corrupt) {
                    broken.push_back(file.path);
                }
            }
            return images + "_folder, " + images + "_files = list_images_checked(\n    " + pythonString(in.dataset) +
                   ", HERE / " + pythonString(pathFrom(outputFolder_, schema.root)) +
                   ", recursive=" + (schema.recursive ? "True" : "False") +
                   ", single=" + (schema.singleFile ? "True" : "False") + ", fingerprint=" + hex(schema.fingerprint) +
                   ",\n    broken=" + pythonList(broken) + ",\n    certificate=" + certificateDict(in.dataset) +
                   ",\n)\n";
        }
        case Opcode::Resize: {
            const std::string width = std::to_string(in.numbers[0]);
            const std::string height = std::to_string(in.numbers[1]);
            pipeline.steps.push_back(when +
                                     (in.numbers[2] ? "img = letterbox(img, " + width + ", " + height + ")"
                                                    : "img = cv2.resize(img, (" + width + ", " + height +
                                                          "), interpolation=cv2.INTER_AREA)") +
                                     origin);
            break;
        }
        case Opcode::Grayscale:       pipeline.steps.push_back(when + "img = to_gray(img)" + origin); break;
        case Opcode::NormalizeImages: {
            // Maps the known source range [from, to] onto [low, high].
            const double low = std::stod(in.values[0].value);
            const double high = std::stod(in.values[1].value);
            const double from = std::stod(in.values[2].value);
            const double to = std::stod(in.values[3].value);
            const double scale = (high - low) / (to - from);
            pipeline.steps.push_back(when + "img = img.astype(np.float32) * " + number(scale) + " + " +
                                     number(low - from * scale) + origin);
            break;
        }
        case Opcode::Compress:
            pipeline.quality = guard.empty() ? std::to_string(in.numbers[0])
                                             : "(" + std::to_string(in.numbers[0]) + " if deopt(" +
                                                   pythonString(guard) + ") else " + pipeline.quality + ")";
            break;
        case Opcode::Convert:
            pipeline.formatBefore = guard.empty() ? "" : pipeline.format;
            pipeline.formatGuard = guard;
            pipeline.format = in.text;
            break;
        case Opcode::RemoveCorrupt: pipeline.skipBroken = true; break;
        case Opcode::SaveImages:    return saveLoop(in);
        case Opcode::Check:         {  // MATCH_RATE: labels and images are compared at run time
            const std::string labels = PandasCodegen::tableVariable(in.columns[1], "");
            std::string files = variable(in.columns[0]) + "_files";
            if (in.columns.size() > 3) {  // REMOVE_CORRUPT ran before the LINK: broken files are not linked
                files = "[name for name in " + files + " if name not in broken_images[" + pythonString(in.columns[0]) +
                        "]]";
            }
            // Proven from the values: only needed when the labels file changed.
            const std::string indent = in.onlyIfInputChanged ? "    " : "";
            return (in.onlyIfInputChanged ? "if " + PandasCodegen::inputChanged(in) + ":\n" : "") + indent +
                   "names = set(" + labels + "[" + pythonString(in.columns[2]) + "].dropna())\n" + indent +
                   "matched = len(names & set(" + files + "))\n" + indent +
                   "rate = 100 * matched / max(len(names), len(" + files + "), 1)\n" + indent + "if rate < " +
                   in.values[0].value + ":\n" + indent + "    raise ContractError(" +
                   pythonString("line " + std::to_string(in.line) + ": " + in.dataset + " match rate is ") +
                   " + f\"{rate:.2f}%\")\n";
        }
        default: break;
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
    const std::string destination = "HERE / " + pythonString(in.text);
    const auto outputFor = [&](const std::string& format) {
        const auto suffix = suffixes.find(format);
        const std::string path = single ? "target" : "target / name";
        return suffix == suffixes.end() ? path : "(" + path + ").with_suffix(" + pythonString(suffix->second) + ")";
    };
    std::string output = outputFor(pipeline.format);
    if (!pipeline.formatGuard.empty()) {  // a speculative CONVERT
        output = "(" + output + " if deopt(" + pythonString(pipeline.formatGuard) + ") else " +
                 outputFor(pipeline.formatBefore) + ")";
    }
    std::string code = "target = " + destination + "\nwritten = skipped = 0\nfor name in " + images + "_files:\n" +
                       "    img = read_image(" + images + "_folder / name)\n    if img is None:\n";
    code += pipeline.skipBroken ? "        skipped += 1\n        continue\n"
                                : "        raise ContractError(f\"{name} could not be decoded\")\n";
    for (const std::string& step : pipeline.steps) {
        code += "    " + step + "\n";
    }
    code += "    write_image(" + output + ", img, " + pipeline.quality + ")\n    written += 1\n";
    code += "outputs.append({\"" + std::string(single ? "file" : "folder") + "\": " + pythonString(in.text) +
            ", \"images\": written, \"skipped\": skipped})\n";
    code += "print(\"Saved\", written, \"image(s) to\", target, f\"({skipped} skipped)\" if skipped else \"\")\n";
    return code;
}
