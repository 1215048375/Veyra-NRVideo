#pragma once

#include <cstdint>

namespace veyra {

// Veyra-internal status codes, kept separate from HRESULT/NGX results which
// are always logged as their raw hex value plus a readable name.
enum class Status : int32_t {
    Ok = 0,
    InvalidArgument = 1,
    FileNotFound = 2,
    IoFailure = 3,
    CryptoFailure = 4,
    SignatureFailure = 5,
    VersionInfoFailure = 6,
    LoadLibraryFailure = 7,
    MissingExport = 8,
    DeviceFailure = 9,
    WindowFailure = 10,
    JsonFailure = 11,
    Cancelled = 12,
};

inline const char* statusString(Status status)
{
    switch (status) {
    case Status::Ok: return "Ok";
    case Status::InvalidArgument: return "InvalidArgument";
    case Status::FileNotFound: return "FileNotFound";
    case Status::IoFailure: return "IoFailure";
    case Status::CryptoFailure: return "CryptoFailure";
    case Status::SignatureFailure: return "SignatureFailure";
    case Status::VersionInfoFailure: return "VersionInfoFailure";
    case Status::LoadLibraryFailure: return "LoadLibraryFailure";
    case Status::MissingExport: return "MissingExport";
    case Status::DeviceFailure: return "DeviceFailure";
    case Status::WindowFailure: return "WindowFailure";
    case Status::JsonFailure: return "JsonFailure";
    case Status::Cancelled: return "Cancelled";
    default: return "Unknown";
    }
}

} // namespace veyra
