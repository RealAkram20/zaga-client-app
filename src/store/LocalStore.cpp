#include "LocalStore.h"

#include "DataProtection.h"

#include <windows.h>
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")

namespace zaga {

namespace {

const uint8_t MAGIC[4] = {'Z', 'G', 'S', '1'};
constexpr uint8_t FORMAT_VERSION = 1;

class Writer {
public:
    void putBytes(const uint8_t* data, size_t length) {
        buffer_.insert(buffer_.end(), data, data + length);
    }

    void putU8(uint8_t value) {
        buffer_.push_back(value);
    }

    void putU32(uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            buffer_.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
        }
    }

    void putI64(int64_t value) {
        auto unsignedValue = static_cast<uint64_t>(value);
        for (int shift = 56; shift >= 0; shift -= 8) {
            buffer_.push_back(static_cast<uint8_t>((unsignedValue >> shift) & 0xFF));
        }
    }

    void putString(const std::string& value) {
        putU32(static_cast<uint32_t>(value.size()));
        putBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
    }

    std::vector<uint8_t> take() {
        return std::move(buffer_);
    }

private:
    std::vector<uint8_t> buffer_;
};

class Reader {
public:
    Reader(const std::vector<uint8_t>& buffer) : buffer_(buffer) {}

    bool getU8(uint8_t& value) {
        if (offset_ + 1 > buffer_.size()) {
            return false;
        }
        value = buffer_[offset_++];
        return true;
    }

    bool getU32(uint32_t& value) {
        if (offset_ + 4 > buffer_.size()) {
            return false;
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            value = (value << 8) | buffer_[offset_++];
        }
        return true;
    }

    bool getI64(int64_t& value) {
        if (offset_ + 8 > buffer_.size()) {
            return false;
        }
        uint64_t unsignedValue = 0;
        for (int i = 0; i < 8; ++i) {
            unsignedValue = (unsignedValue << 8) | buffer_[offset_++];
        }
        value = static_cast<int64_t>(unsignedValue);
        return true;
    }

    bool getString(std::string& value) {
        uint32_t length = 0;
        if (!getU32(length) || offset_ + length > buffer_.size()) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(buffer_.data() + offset_), length);
        offset_ += length;
        return true;
    }

    bool matches(const uint8_t* expected, size_t length) {
        if (offset_ + length > buffer_.size()) {
            return false;
        }
        for (size_t i = 0; i < length; ++i) {
            if (buffer_[offset_ + i] != expected[i]) {
                return false;
            }
        }
        offset_ += length;
        return true;
    }

private:
    const std::vector<uint8_t>& buffer_;
    size_t offset_ = 0;
};

bool ensureParentDirectory(const std::wstring& path) {
    size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return true;
    }

    std::wstring directory = path.substr(0, separator);
    int result = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

bool writeFile(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);

    return ok && written == bytes.size();
}

bool readFile(const std::wstring& path, std::vector<uint8_t>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > (1 << 20)) {
        CloseHandle(file);
        return false;
    }

    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = bytes.empty()
        ? TRUE
        : ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);

    return ok && read == bytes.size();
}

}

std::vector<uint8_t> LocalStore::serialize(const StoredDevice& device) {
    Writer writer;
    writer.putBytes(MAGIC, sizeof(MAGIC));
    writer.putU8(FORMAT_VERSION);
    writer.putString(device.accountNumber);
    writer.putString(device.serial);
    writer.putString(device.model);
    writer.putString(device.name);
    writer.putString(device.hmacSecretHex);
    writer.putString(device.biosPassword);
    writer.putString(device.recoveryKey);
    writer.putString(device.uninstallCode);
    writer.putU32(device.state.lastCounter);
    writer.putI64(device.state.lockDeadlineDay);
    writer.putU8(static_cast<uint8_t>(device.state.status));
    writer.putU8(device.state.lastTokenWasGrace ? 1 : 0);
    return writer.take();
}

bool LocalStore::deserialize(const std::vector<uint8_t>& bytes, StoredDevice& device) {
    Reader reader(bytes);

    if (!reader.matches(MAGIC, sizeof(MAGIC))) {
        return false;
    }

    uint8_t version = 0;
    if (!reader.getU8(version) || version != FORMAT_VERSION) {
        return false;
    }

    uint8_t status = 0;
    uint8_t grace = 0;
    bool ok = reader.getString(device.accountNumber)
        && reader.getString(device.serial)
        && reader.getString(device.model)
        && reader.getString(device.name)
        && reader.getString(device.hmacSecretHex)
        && reader.getString(device.biosPassword)
        && reader.getString(device.recoveryKey)
        && reader.getString(device.uninstallCode)
        && reader.getU32(device.state.lastCounter)
        && reader.getI64(device.state.lockDeadlineDay)
        && reader.getU8(status)
        && reader.getU8(grace);

    if (!ok || status > static_cast<uint8_t>(DeviceStatus::Locked)) {
        return false;
    }

    device.state.status = static_cast<DeviceStatus>(status);
    device.state.lastTokenWasGrace = grace != 0;
    return true;
}

bool LocalStore::save(const std::wstring& path, const StoredDevice& device) {
    std::vector<uint8_t> plaintext = serialize(device);

    std::vector<uint8_t> blob;
    bool protectedOk = DataProtection::protect(plaintext, blob);
    SecureZeroMemory(plaintext.data(), plaintext.size());
    if (!protectedOk) {
        return false;
    }

    if (!ensureParentDirectory(path)) {
        return false;
    }

    // Write to a sibling temp file then rename, so a crash mid-write cannot leave
    // a half-written state file that would fail closed on next boot.
    std::wstring temporary = path + L".tmp";
    if (!writeFile(temporary, blob)) {
        return false;
    }

    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temporary.c_str());
        return false;
    }

    return true;
}

bool LocalStore::load(const std::wstring& path, StoredDevice& device) {
    std::vector<uint8_t> blob;
    if (!readFile(path, blob)) {
        return false;
    }

    std::vector<uint8_t> plaintext;
    if (!DataProtection::unprotect(blob, plaintext)) {
        return false;
    }

    bool ok = deserialize(plaintext, device);
    SecureZeroMemory(plaintext.data(), plaintext.size());
    return ok;
}

std::wstring LocalStore::defaultPath() {
    PWSTR programData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData))) {
        std::wstring path(programData);
        CoTaskMemFree(programData);
        return path + L"\\Zaga\\state.bin";
    }

    return L"C:\\ProgramData\\Zaga\\state.bin";
}

}
