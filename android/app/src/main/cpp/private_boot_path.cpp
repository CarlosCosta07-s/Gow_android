#include "private_boot_path.h"

#include <cctype>
#include <system_error>
#include <utility>

namespace gow::disc {
namespace {

void setError(std::string &error, const char *message) {
    error = message;
}

bool normalizeBootPath(std::string_view input,
                       std::filesystem::path &normalized,
                       std::string &error) {
    normalized.clear();
    if (input.empty() || input.size() > 1024u) {
        setError(error, "The BOOT2 path is empty or exceeds the supported length.");
        return false;
    }
    if (input.front() == '/' || input.front() == '\\' ||
        input.find(':') != std::string_view::npos ||
        input.find('\\0') != std::string_view::npos) {
        setError(error, "The BOOT2 path must be relative and cannot contain a device prefix.");
        return false;
    }

    std::string portable(input);
    for (char &ch : portable) {
        if (ch == '\\') ch = '/';
    }

    size_t cursor = 0u;
    size_t depth = 0u;
    while (cursor < portable.size()) {
        const size_t end = portable.find('/', cursor);
        const size_t finish = end == std::string::npos ? portable.size() : end;
        std::string component = portable.substr(cursor, finish - cursor);
        if (component.empty() || component == "." || component == ".." ||
            component.size() > 255u || ++depth > 32u) {
            setError(error, "The BOOT2 path contains an unsafe component or exceeds its depth limit.");
            return false;
        }

        const size_t version = component.find(';');
        if (version != std::string::npos) component.resize(version);
        if (component.empty()) {
            setError(error, "The BOOT2 path contains an empty ISO9660 identifier.");
            return false;
        }
        for (char &ch : component) {
            ch = static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch)));
        }
        normalized /= component;

        if (end == std::string::npos) break;
        cursor = end + 1u;
    }

    if (normalized.empty()) {
        setError(error, "The BOOT2 path does not identify a file.");
        return false;
    }
    return true;
}

bool isStrictChildPath(const std::filesystem::path &root,
                       const std::filesystem::path &candidate) {
    auto rootPart = root.begin();
    auto candidatePart = candidate.begin();
    for (; rootPart != root.end() && candidatePart != candidate.end();
         ++rootPart, ++candidatePart) {
        if (*rootPart != *candidatePart) return false;
    }
    return rootPart == root.end() && candidatePart != candidate.end();
}

} // namespace

bool resolvePrivateBootElfPath(
    std::string_view privateRoot,
    std::string_view bootRelativePath,
    std::filesystem::path &resolvedPath,
    std::string &error) {
    resolvedPath.clear();
    error.clear();

    if (privateRoot.empty() || privateRoot.size() > 4095u ||
        privateRoot.find('\\0') != std::string_view::npos) {
        setError(error, "The private cdRoot path is empty or invalid.");
        return false;
    }

    std::filesystem::path relative;
    if (!normalizeBootPath(bootRelativePath, relative, error)) return false;

    try {
        const std::filesystem::path canonicalRoot =
            std::filesystem::weakly_canonical(
                std::filesystem::u8path(privateRoot));
        const std::filesystem::path candidate =
            std::filesystem::weakly_canonical(canonicalRoot / relative);

        if (!isStrictChildPath(canonicalRoot, candidate)) {
            setError(error, "The resolved BOOT2 path escapes the private installation.");
            return false;
        }
        std::error_code statusError;
        if (!std::filesystem::is_regular_file(candidate, statusError) ||
            statusError) {
            setError(error, "The BOOT2 path does not resolve to a regular file.");
            return false;
        }

        resolvedPath = candidate;
        return true;
    } catch (const std::filesystem::filesystem_error &) {
        setError(error, "The private BOOT2 path could not be resolved safely.");
        return false;
    }
}

} // namespace gow::disc
