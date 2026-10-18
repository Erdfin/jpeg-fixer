#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <array>

constexpr int MAX_TAIL_SIZE = 10;
const char* HEALTHY_FILE_A = "hardcoded abspath string 1";
const char* HEALTHY_FILE_B = "hardcoded abspath string 2";
const char* ID_A = "id string A";
const char* ID_B = "id string B";

/*
    CompleteScan and IncompleteHeader -> fixable
    IncompleteScan and IncompleteHeader -> fixable, only part of the image is still there though
    MissingHeader -> whatever cut the header also took part of the scan, this is can be recoverable but correct reading depends on the decoder
    MarkersMissing -> can only guess that whats there is part of scan from volume composition of a normal jpeg, a lenient enough decoder might still read it if the correct header is supplied
*/
enum class IS { // Image state tags
    CompleteHeader,
    CompleteScan,
    IncompleteHeader,
    IncompleteScan,
    MissingHeader,
    MarkersMissing,
    BytesAfterEOF
};

enum class Options {
    SuppressCorrect,
    MakeFixedCopy,
    TrimTail,
    AddTail
};

const std::unordered_map<std::string, Options> VALID_OPTIONS = {
    {"--suppress-correct", Options::SuppressCorrect},
    {"-S", Options::SuppressCorrect},
    {"--make-fixed-copy", Options::MakeFixedCopy},
    {"-M", Options::MakeFixedCopy},
    {"--trim-tail", Options::TrimTail}, // Not implemented
    {"--T", Options::TrimTail},
    {"--add-tail", Options::AddTail}, // Not implemented
    {"--A", Options::AddTail}
};

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Failed to open: " + path.string());

    auto size = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> buf(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

bool isImage(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    // lowercase the extension for comparison
    for (auto& c : ext) c = tolower(c);
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
}

int getHealthyJPGHeader(const char* filename, std::vector<uint8_t>* header) {
    std::ifstream file(filename, std::ios::binary);

    if (!file) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    std::vector<uint8_t> buffer{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };

    file.close();

    // Check if jpeg is healthy
    bool file_start = false;
    bool scan_start = false;
    int scan_start_index = -1;

    if (buffer[0] == 0xFF and buffer[1] == 0xD8) {
        file_start = true;
    }

    // Search for the SOS marker (0xFF 0xDA)
    for (size_t i = 0; i < buffer.size() - 1; ++i) {
        if (buffer[i] == 0xFF and buffer[i+1] == 0xDA) {
            scan_start_index = i+1;
            scan_start = true;
            break;
        }
    }

    if (file_start and scan_start) {
        for (size_t i = 0; i <= scan_start_index; ++i) {
            header->push_back(buffer[i]);
        }
    } else {
        return 1;
    }

    return 0;
}


std::pair<std::vector<uint8_t>, std::array<uint8_t,MAX_TAIL_SIZE>>
readPartial(const std::filesystem::path& path, size_t head_size = 65536) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open: " + path.string());

    // Read head
    std::vector<uint8_t> head(head_size);
    file.read(reinterpret_cast<char*>(head.data()), head_size);
    head.resize(file.gcount()); // trim to actual bytes read

    // Read tail
    std::array<uint8_t,MAX_TAIL_SIZE> tail{};
    file.seekg(-MAX_TAIL_SIZE, std::ios::end);
    file.read(reinterpret_cast<char*>(tail.data()), MAX_TAIL_SIZE);

    return {std::move(head), tail};
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << std::filesystem::path(argv[0]).filename().string() << " <directory> [--suppress-correct | -S] [--make-fixed-copy | -M]\n";
        return 1;
    }

    std::vector<uint8_t> replacement_header_a;
    std::vector<uint8_t> replacement_header_b;
    int err = getHealthyJPGHeader(HEALTHY_FILE_A, &replacement_header_a);
    if (err != 0) {
        std::cerr << "Could not load healthy A header for repairs";
        return 1;
    }

    err = getHealthyJPGHeader(HEALTHY_FILE_B, &replacement_header_b);
    if (err != 0) {
        std::cerr << "Could not load healthy B header for repairs";
        return 1;
    }

    size_t head_size = std::max(replacement_header_a.size(), replacement_header_b.size()) + 5; // +5 for safety padding

    bool suppress_correct = false;
    bool make_fixed_copy = false;
    if (argc > 2) {
        if (VALID_OPTIONS.contains(argv[2])) {
            if (VALID_OPTIONS.at(argv[2]) == Options::SuppressCorrect) suppress_correct = true;
            if (VALID_OPTIONS.at(argv[2]) == Options::MakeFixedCopy) make_fixed_copy = true;
        }
    }
    if (argc > 3) {
        if (VALID_OPTIONS.contains(argv[3])) {
            if (VALID_OPTIONS.at(argv[3]) == Options::SuppressCorrect) suppress_correct = true;
            if (VALID_OPTIONS.at(argv[3]) == Options::MakeFixedCopy) make_fixed_copy = true;
        }
    }

    if (suppress_correct) printf("Supress correct is ON");
    else printf("Supress correct is OFF");
    printf("\n");

    if (make_fixed_copy) printf("Make fixed copy is ON");
    else printf("Make fixed copy is OFF");
    printf("\n");

    std::filesystem::path dir(argv[1]);
    if (!std::filesystem::is_directory(dir)) {
        std::cerr << "Not a directory: " << dir << "\n";
        return 1;
    }

    std::filesystem::path fixed_dir = dir / "fixed";
    std::filesystem::create_directories(fixed_dir);

    // Collect paths
    std::cout << "Collecting paths...\n";
    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && isImage(entry.path())) {
            uintmax_t filesize = entry.file_size();
            if (filesize >= head_size and filesize >= MAX_TAIL_SIZE) {
                paths.push_back(entry.path());
            } else {
                std::cout << entry.path().filename().string() << " - File too small to analyse\n";
                continue;
            }
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "Got: " << paths.size() << " paths in: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms\n";

    // Process in parallel — output needs a mutex to avoid interleaving
    std::mutex io_mutex;

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < (int)paths.size(); ++i) {
        std::string filename = paths[i].filename().string();
        try {
            auto partial = readPartial(paths[i], head_size);

            std::ostringstream message_buffer;

            message_buffer << filename << " - ";

            bool file_start = false;
            bool scan_start = false;
            bool file_end = false;

            std::vector<IS> state_tags;

            if (partial.first[0] == 0xFF and partial.first[1] == 0xD8) {
                file_start = true;
            }

            // Search for the SOS marker (0xFF 0xDA) regardless of whether SOI was found — the two flags are independent.
            int scan_start_index = 0;
            for (size_t i = 0; i < partial.first.size() - 1; ++i) {
                if (partial.first[i] == 0xFF and partial.first[i+1] == 0xDA) {
                    scan_start = true;
                    scan_start_index = i+1;
                    break;
                }
            }

            if (file_start and scan_start) {
                state_tags.push_back(IS::CompleteHeader);
            } else if (scan_start) {
                state_tags.push_back(IS::IncompleteHeader);
            } else {
                state_tags.push_back(IS::MissingHeader);
            }

            int file_end_index = 0;
            for (size_t i = partial.second.size() - 1; i > 0; --i) {
                if (partial.second[i] == 0xD9 and partial.second[i-1] == 0xFF) {
                    file_end = true;
                    file_end_index = i;
                    break;
                }
            }

            if (file_end and file_end_index != partial.second.size() - 1) {
                state_tags.push_back(IS::BytesAfterEOF);
            }

            switch (state_tags[0])
            {
            case IS::MissingHeader:
                if (file_end) {
                    state_tags.push_back(IS::IncompleteScan);
                } else {
                    state_tags.push_back(IS::MarkersMissing);
                }
                break;

            default:
                if (file_end) {
                    state_tags.push_back(IS::CompleteScan);
                } else {
                    state_tags.push_back(IS::IncompleteScan);
                }
                break;
            }

            int correctness = 0; // correctness == 2 -> perfectly good jpeg
            bool needs_new_header = false;
            for (size_t i = 0; i < state_tags.size(); i++) {
                switch (state_tags[i])
                {
                case IS::CompleteHeader:
                    message_buffer << "Complete header - ";
                    ++correctness;
                    break;

                case IS::IncompleteHeader:
                    message_buffer << "Incomplete header - ";
                    needs_new_header = true;
                    break;

                case IS::MissingHeader:
                    message_buffer << "Missing header - ";
                    break;

                case IS::BytesAfterEOF:
                    message_buffer << "Bytes after end of file -";
                    break;

                case IS::CompleteScan:
                    message_buffer << "Complete Scan";
                    ++correctness;
                    break;

                case IS::IncompleteScan:
                    message_buffer << "Incomplete scan";
                    break;

                case IS::MarkersMissing:
                    message_buffer << "Markers missing - data is scrambled";
                    break;
                }
            }

            if (make_fixed_copy and needs_new_header) message_buffer << " - Making Fixed Copy";
            else message_buffer << "\n";

            if (!suppress_correct and correctness == 2) {
                std::lock_guard<std::mutex> lock(io_mutex);
                std::cout << message_buffer.str();
            }

            if (make_fixed_copy and needs_new_header) {
                auto buffer = readFile(paths[i]);
                std::vector<uint8_t> fixed_buffer;
                fixed_buffer.reserve(head_size + buffer.size());

                // Insert healthy header
                if (filename.contains(ID_A)) {
                    fixed_buffer.insert(fixed_buffer.end(),
                                        replacement_header_a.begin(),
                                        replacement_header_a.end());
                } else if (filename.contains(ID_B)) {
                    fixed_buffer.insert(fixed_buffer.end(),
                                        replacement_header_b.begin(),
                                        replacement_header_b.end());
                } else {
                    message_buffer << " - Not supported - aborted repair of: " << filename << "\n";
                    std::lock_guard<std::mutex> lock(io_mutex);
                    std::cout << message_buffer.str();
                    continue;
                }

                // Apped original image data
                fixed_buffer.insert(fixed_buffer.end(),
                                    buffer.begin() + scan_start_index + 1, // +1 to skip the 0xDA marker itself
                                    buffer.end());

                std::filesystem::path out_path = fixed_dir / paths[i].filename();
                message_buffer << " in: " << out_path << "\n";

                std::ofstream out(out_path, std::ios::binary);
                if (!out) {
                    message_buffer << "Failed to write: " << out_path << "\n";
                    message_buffer << " - Failed to write\n";
                    std::lock_guard<std::mutex> lock(io_mutex);
                    std::cout << message_buffer.str();
                    continue;
                }

                out.write(reinterpret_cast<const char*>(fixed_buffer.data()), fixed_buffer.size());

                if (!out) {
                    message_buffer << "Write failed: " << out_path << "\n";
                    message_buffer << " - Failed to write\n";
                } else {
                    message_buffer << " - Done\n";
                }

                out.close();
            }

            if (correctness < 2) {
                std::lock_guard<std::mutex> lock(io_mutex);
                std::cout << message_buffer.str();
            }


        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(io_mutex);
            std::cerr << "Error: " << e.what() << " " << filename << "\n";
        }
    }
    return 0;
}