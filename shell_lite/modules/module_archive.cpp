#include "../native_registry.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <vector>
#include <string>
#include <zip.h>

namespace shell_lite {

static bool archive_zip_impl(const std::string& source, const std::string& target, std::string& err_out) {
    int err = 0;
    zip* archive = zip_open(target.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!archive) {
        err_out = "Failed to create zip archive: " + target;
        return false;
    }

    std::vector<std::string> files;
    if (std::filesystem::is_directory(source)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
            if (entry.is_regular_file()) files.push_back(entry.path().string());
        }
    } else {
        files.push_back(source);
    }

    for (const auto& f : files) {
        std::string rel_path = f;
        if (f.find(source) == 0) rel_path = f.substr(source.length());
        if (!rel_path.empty() && (rel_path[0] == '/' || rel_path[0] == '\\')) rel_path = rel_path.substr(1);

        zip_source* src = zip_source_file(archive, f.c_str(), 0, 0);
        if (src) {
            zip_file_add(archive, rel_path.c_str(), src, ZIP_FL_OVERWRITE);
        }
    }
    zip_close(archive);
    return true;
}

static bool archive_unzip_impl(const std::string& source, const std::string& target, std::string& err_out) {
    int err = 0;
    zip* archive = zip_open(source.c_str(), 0, &err);
    if (!archive) {
        err_out = "Failed to open zip archive: " + source;
        return false;
    }

    std::filesystem::create_directories(target);

    zip_int64_t num_entries = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < num_entries; ++i) {
        const char* name = zip_get_name(archive, i, 0);
        if (!name) continue;

        std::filesystem::path target_path = std::filesystem::weakly_canonical(target);
        std::filesystem::path p = (target_path / name).lexically_normal();
        std::string target_str = target_path.string();
        std::string p_str = p.string();
        if (p_str.rfind(target_str, 0) != 0) {
            std::cerr << "[Archive Security Warning] Skipped zip slip entry: " << name << std::endl;
            continue;
        }

        std::string name_str(name);
        if (!name_str.empty() && (name_str.back() == '/' || name_str.back() == '\\')) {
            std::filesystem::create_directories(p);
            continue;
        }

        std::filesystem::create_directories(p.parent_path());

        zip_file* file = zip_fopen_index(archive, i, 0);
        if (file) {
            std::ofstream out(p, std::ios::binary);
            constexpr size_t ARCHIVE_BUFFER_SIZE = 8192;
            char buffer[ARCHIVE_BUFFER_SIZE];
            zip_int64_t bytes_read;
            while ((bytes_read = zip_fread(file, buffer, sizeof(buffer))) > 0) {
                out.write(buffer, bytes_read);
            }
            zip_fclose(file);
        }
    }
    zip_close(archive);
    return true;
}

void register_stdlib_archive(VM* vm) {
    NativeRegistry::bind(vm, "std_archive_zip", [](std::string source, std::string target) -> bool {
        std::string err;
        if (!archive_zip_impl(source, target, err)) {
            throw std::runtime_error(err);
        }
        return true;
    });

    NativeRegistry::bind(vm, "archive_zip", [](std::string source, std::string target) -> bool {
        std::string err;
        if (!archive_zip_impl(source, target, err)) {
            throw std::runtime_error(err);
        }
        return true;
    });

    NativeRegistry::bind(vm, "std_archive_unzip", [](std::string source, std::string target) -> bool {
        std::string err;
        if (!archive_unzip_impl(source, target, err)) {
            throw std::runtime_error(err);
        }
        return true;
    });

    NativeRegistry::bind(vm, "archive_unzip", [](std::string source, std::string target) -> bool {
        std::string err;
        if (!archive_unzip_impl(source, target, err)) {
            throw std::runtime_error(err);
        }
        return true;
    });

    // Backwards-compatible legacy dispatcher
    NativeRegistry::bind(vm, "archive_op", [](std::string op, std::string source, std::string target) -> void {
        std::string err;
        if (op == "zip" || op == "pack") {
            if (!archive_zip_impl(source, target, err)) {
                throw std::runtime_error(err);
            }
        } else if (op == "unzip" || op == "unpack") {
            if (!archive_unzip_impl(source, target, err)) {
                throw std::runtime_error(err);
            }
        }
    });
}

} // namespace shell_lite
