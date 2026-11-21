#include "services/import/ImportFile.hpp"

#include "core/parsing/Csv.hpp"
#include "data/readers/XlsxReader.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>

#include <format>
#include <string_view>
#include <utility>

namespace fdss::services {

namespace {

constexpr std::string_view kZipSignature = "PK\x03\x04";
constexpr std::string_view kOleSignature = "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1";
// Enough of the start of a file to tell text from binary.
constexpr qsizetype kTextSample = 64 * 1024;

core::Error fileError(core::ErrorCode code, std::string detail, const char* field) {
    return core::Error{code, std::move(detail), field};
}

bool startsWith(const QByteArray& bytes, std::string_view prefix) {
    return bytes.startsWith(QByteArrayView(prefix.data(), static_cast<qsizetype>(prefix.size())));
}

// No control characters other than tab, line breaks and form feed; UTF-16 is recognised by its
// BOM before this check.
bool looksLikeText(const QByteArray& bytes) {
    const QByteArray sample = bytes.left(kTextSample);
    for (const char character : sample) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f') {
            return false;
        }
    }
    return true;
}

bool isJson(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    return first != std::string_view::npos && (text[first] == '{' || text[first] == '[');
}

core::Status readCsv(const QByteArray& bytes, ImportSource& source) {
    core::DecodedText decoded = core::decodeText(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (isJson(decoded.utf8)) {
        return std::unexpected(
            fileError(core::ErrorCode::UnsupportedFormat, "a JSON file", "import_json"));
    }
    source.kind = ImportFileKind::Csv;
    source.encoding = decoded.encoding;
    source.delimiter = core::detectDelimiter(decoded.utf8);
    core::CsvRecords records = core::parseCsv(decoded.utf8, source.delimiter);
    if (records.unterminatedQuoteLine) {
        source.errorLine = records.unterminatedQuoteLine;
        source.tableStatus = std::unexpected(fileError(
            core::ErrorCode::InvalidInput,
            std::format("unterminated CSV quote at line {}", *records.unterminatedQuoteLine),
            "import_csv_quote"));
        return {};
    }
    auto table = core::toSourceTable(std::move(records.rows));
    if (!table) {
        return std::unexpected(table.error());
    }
    source.table = std::move(*table);
    return {};
}

core::Status readXlsxFile(const QByteArray& bytes, std::optional<int> sheet, ImportSource& source) {
    source.kind = ImportFileKind::Xlsx;
    auto book = data::readXlsx(bytes, sheet.value_or(0));
    if (!book) {
        return std::unexpected(book.error());
    }
    source.sheets = book->sheets;
    source.sheet = book->sheet;
    if (auto table = core::toSourceTable(std::move(book->rows))) {
        source.table = std::move(*table);
    } else {
        source.tableStatus = std::unexpected(table.error());
    }
    if (sheet || source.tableStatus) {
        return {};
    }
    // No sheet chosen: the first one with data rows, else the first sheet and its error.
    const core::Status firstError = source.tableStatus;
    for (int index = 1; index < source.sheets.size(); ++index) {
        auto other = data::readXlsx(bytes, index);
        if (!other) {
            return std::unexpected(other.error());
        }
        auto table = core::toSourceTable(std::move(other->rows));
        if (table) {
            source.sheet = index;
            source.table = std::move(*table);
            source.tableStatus = {};
            return {};
        }
    }
    source.tableStatus = firstError;
    return {};
}

} // namespace

core::Result<ImportSource> readImportFile(const QString& path, std::optional<int> sheet) {
    const QFileInfo info(path);
    if (!info.isFile()) {
        return std::unexpected(fileError(core::ErrorCode::Io, "not a file", "import_file"));
    }
    if (info.size() > kMaxImportFileBytes) {
        return std::unexpected(fileError(core::ErrorCode::InvalidInput,
                                         std::format("{} bytes", info.size()), "import_file_size"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(
            fileError(core::ErrorCode::Io, file.errorString().toStdString(), "import_file"));
    }
    // Read at most one byte past the limit: the file may have grown since the check.
    const QByteArray bytes = file.read(kMaxImportFileBytes + 1);
    if (bytes.size() > kMaxImportFileBytes) {
        return std::unexpected(
            fileError(core::ErrorCode::InvalidInput, "grew past the limit", "import_file_size"));
    }
    if (bytes.isEmpty()) {
        return std::unexpected(
            fileError(core::ErrorCode::InvalidInput, "an empty file", "import_empty"));
    }

    ImportSource source;
    source.path = path;
    source.fileName = info.fileName();
    source.hash =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());

    const QMimeType mime = QMimeDatabase().mimeTypeForData(bytes);
    core::Status read;
    if (startsWith(bytes, kZipSignature)) {
        read = readXlsxFile(bytes, sheet, source);
    } else if (startsWith(bytes, kOleSignature) ||
               mime.inherits(QStringLiteral("application/x-ole-storage")) ||
               mime.inherits(QStringLiteral("application/vnd.ms-excel"))) {
        return std::unexpected(fileError(core::ErrorCode::UnsupportedFormat,
                                         "an XLS (Excel 97-2003) file", "import_xls"));
    } else if (mime.inherits(QStringLiteral("application/json"))) {
        return std::unexpected(
            fileError(core::ErrorCode::UnsupportedFormat, "a JSON file", "import_json"));
    } else if (startsWith(bytes, "\xFF\xFE") || startsWith(bytes, "\xFE\xFF") ||
               ((mime.inherits(QStringLiteral("text/plain")) || mime.isDefault()) &&
                looksLikeText(bytes))) {
        read = readCsv(bytes, source);
    } else {
        return std::unexpected(fileError(
            core::ErrorCode::UnsupportedFormat,
            std::format("content of type {}", mime.name().toStdString()), "import_file_type"));
    }
    if (!read) {
        return std::unexpected(read.error());
    }
    return source;
}

} // namespace fdss::services
