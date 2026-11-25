
#include "minidb/catalog/tuple.hpp"

#include "minidb/common/exception.hpp"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace minidb {

namespace {

// Binary tuple format:
// 4 bytes: Magic "TUPL"
// 1 byte : Format version
// 1 byte : Reserved
// 2 bytes: Number of fields (little-endian)
// Followed by encoded field values.

constexpr std::uint8_t kFormatVersion = 1;

constexpr std::size_t kHeaderSize = 8;


// --------------------------------------------------
// AppendLittleEndian
// --------------------------------------------------

void AppendLittleEndian(
    std::vector<std::uint8_t>& output,
    std::uint64_t value,
    std::size_t width
) {

    for (std::size_t i = 0; i < width; ++i) {

        output.push_back(
            static_cast<std::uint8_t>(value & 0xFF)
        );

        value >>= 8;
    }
}


// --------------------------------------------------
// ReadLittleEndian
// --------------------------------------------------

std::uint64_t ReadLittleEndian(
    const std::vector<std::uint8_t>& input,
    std::size_t& offset,
    std::size_t width
) {

    if (width > 8 ||
        offset > input.size() ||
        width > input.size() - offset) {

        throw MiniDBException(
            "Unexpected end of binary tuple"
        );
    }

    std::uint64_t value = 0;

    for (std::size_t i = 0; i < width; ++i) {

        value |=
            static_cast<std::uint64_t>(input[offset++])
            << (8 * i);
    }

    return value;
}


// --------------------------------------------------
// DecodeSignedInteger
// --------------------------------------------------

std::int64_t DecodeSignedInteger(std::uint64_t bits) {

    const std::uint64_t maxPositive =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()
        );

    if (bits <= maxPositive) {
        return static_cast<std::int64_t>(bits);
    }

    // Safely reconstruct the negative two's-complement value.
    return -1 - static_cast<std::int64_t>(
        std::numeric_limits<std::uint64_t>::max() - bits
    );
}


// --------------------------------------------------
// EncodeValue
// --------------------------------------------------

void EncodeValue(
    std::vector<std::uint8_t>& output,
    const Column& column,
    const FieldValue& value
) {

    // NULL marker.
    if (std::holds_alternative<std::monostate>(value)) {

        output.push_back(0);

        return;
    }

    // Non-NULL marker.
    output.push_back(1);


    switch (column.type) {

        case ColumnType::INT64: {

            const std::int64_t number =
                std::get<std::int64_t>(value);

            AppendLittleEndian(
                output,
                static_cast<std::uint64_t>(number),
                8
            );

            break;
        }


        case ColumnType::DOUBLE: {

            static_assert(
                sizeof(double) == sizeof(std::uint64_t),
                "This tuple format requires 64-bit double"
            );

            static_assert(
                std::numeric_limits<double>::is_iec559,
                "This tuple format requires IEEE-754 double"
            );

            const double number =
                std::get<double>(value);

            std::uint64_t bits = 0;

            std::memcpy(
                &bits,
                &number,
                sizeof(bits)
            );

            AppendLittleEndian(
                output,
                bits,
                8
            );

            break;
        }


        case ColumnType::VARCHAR: {

            const std::string& text =
                std::get<std::string>(value);

            if (text.size() >
                std::numeric_limits<std::uint32_t>::max()) {

                throw MiniDBException(
                    "VARCHAR value is too large to serialize"
                );
            }

            AppendLittleEndian(
                output,
                text.size(),
                4
            );

            output.insert(
                output.end(),
                text.begin(),
                text.end()
            );

            break;
        }


        case ColumnType::BOOLEAN: {

            const bool flag =
                std::get<bool>(value);

            output.push_back(flag ? 1 : 0);

            break;
        }


        default:

            throw MiniDBException(
                "Unsupported column type during tuple encoding"
            );
    }
}


// --------------------------------------------------
// DecodeValue
// --------------------------------------------------

FieldValue DecodeValue(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& offset,
    const Column& column
) {

    // Each field begins with a NULL/non-NULL marker.
    const std::uint64_t nullMarker =
        ReadLittleEndian(bytes, offset, 1);

    if (nullMarker == 0) {
        return std::monostate{};
    }

    if (nullMarker != 1) {
        throw MiniDBException(
            "Invalid tuple NULL marker"
        );
    }


    switch (column.type) {

        case ColumnType::INT64: {

            const std::uint64_t bits =
                ReadLittleEndian(bytes, offset, 8);

            return DecodeSignedInteger(bits);
        }


        case ColumnType::DOUBLE: {

            const std::uint64_t bits =
                ReadLittleEndian(bytes, offset, 8);

            double number = 0.0;

            std::memcpy(
                &number,
                &bits,
                sizeof(number)
            );

            return number;
        }


        case ColumnType::VARCHAR: {

            const std::uint64_t length =
                ReadLittleEndian(bytes, offset, 4);

            if (offset > bytes.size() ||
                length > bytes.size() - offset) {

                throw MiniDBException(
                    "Incomplete VARCHAR data in tuple"
                );
            }

            std::string text(
                reinterpret_cast<const char*>(
                    bytes.data() + offset
                ),
                static_cast<std::size_t>(length)
            );

            offset += static_cast<std::size_t>(length);

            return text;
        }


        case ColumnType::BOOLEAN: {

            const std::uint64_t flag =
                ReadLittleEndian(bytes, offset, 1);

            if (flag > 1) {
                throw MiniDBException(
                    "Invalid BOOLEAN value in tuple"
                );
            }

            return flag == 1;
        }


        default:

            throw MiniDBException(
                "Unsupported column type during tuple decoding"
            );
    }
}

} // anonymous namespace


// --------------------------------------------------
// Constructor
// --------------------------------------------------

Tuple::Tuple(
    const Schema& schema,
    std::vector<FieldValue> values
)
    : schema_(schema),
      values_(std::move(values)) {

    if (values_.size() != schema_.GetColumnCount()) {

        throw MiniDBException(
            "Tuple field count does not match schema"
        );
    }

    // Every supplied value must match its column definition.
    for (std::size_t i = 0; i < values_.size(); ++i) {

        ValidateValue(
            schema_.GetColumn(i),
            values_[i]
        );
    }
}


// --------------------------------------------------
// ValidateValue
// --------------------------------------------------

void Tuple::ValidateValue(
    const Column& column,
    const FieldValue& value
) {

    // Check NULL first.
    if (std::holds_alternative<std::monostate>(value)) {

        if (!column.nullable) {
            throw MiniDBException(
                "NULL is not allowed for column: " +
                column.name
            );
        }

        return;
    }


    switch (column.type) {

        case ColumnType::INT64:

            if (!std::holds_alternative<std::int64_t>(value)) {

                throw MiniDBException(
                    "Expected INT64 for column: " +
                    column.name
                );
            }

            break;


        case ColumnType::DOUBLE:

            if (!std::holds_alternative<double>(value)) {

                throw MiniDBException(
                    "Expected DOUBLE for column: " +
                    column.name
                );
            }

            break;


        case ColumnType::VARCHAR:

            if (!std::holds_alternative<std::string>(value)) {

                throw MiniDBException(
                    "Expected VARCHAR for column: " +
                    column.name
                );
            }

            if (column.max_length != 0 &&
                std::get<std::string>(value).size() >
                    column.max_length) {

                throw MiniDBException(
                    "VARCHAR length exceeded for column: " +
                    column.name
                );
            }

            break;


        case ColumnType::BOOLEAN:

            if (!std::holds_alternative<bool>(value)) {

                throw MiniDBException(
                    "Expected BOOLEAN for column: " +
                    column.name
                );
            }

            break;


        default:

            throw MiniDBException(
                "Unsupported column type"
            );
    }
}


// --------------------------------------------------
// GetFieldCount
// --------------------------------------------------

std::size_t Tuple::GetFieldCount() const noexcept {

    return values_.size();
}


// --------------------------------------------------
// GetSchema
// --------------------------------------------------

const Schema& Tuple::GetSchema() const noexcept {

    return schema_;
}


// --------------------------------------------------
// GetValues
// --------------------------------------------------

const std::vector<FieldValue>&
Tuple::GetValues() const noexcept {

    return values_;
}


// --------------------------------------------------
// GetValue by index
// --------------------------------------------------

const FieldValue& Tuple::GetValue(
    std::size_t index
) const {

    if (index >= values_.size()) {

        throw MiniDBException(
            "Tuple field index is out of range"
        );
    }

    return values_[index];
}


// --------------------------------------------------
// GetValue by name
// --------------------------------------------------

const FieldValue& Tuple::GetValue(
    const std::string& column_name
) const {

    const std::size_t index =
        schema_.GetColumnIndex(column_name);

    return GetValue(index);
}


// --------------------------------------------------
// SetValue by index
// --------------------------------------------------

void Tuple::SetValue(
    std::size_t index,
    FieldValue value
) {

    if (index >= values_.size()) {

        throw MiniDBException(
            "Tuple field index is out of range"
        );
    }

    // Validate first so an invalid assignment
    // never changes the existing tuple.
    ValidateValue(
        schema_.GetColumn(index),
        value
    );

    values_[index] = std::move(value);
}


// --------------------------------------------------
// SetValue by name
// --------------------------------------------------

void Tuple::SetValue(
    const std::string& column_name,
    FieldValue value
) {

    const std::size_t index =
        schema_.GetColumnIndex(column_name);

    SetValue(
        index,
        std::move(value)
    );
}


// --------------------------------------------------
// IsNull
// --------------------------------------------------

bool Tuple::IsNull(std::size_t index) const {

    return std::holds_alternative<std::monostate>(
        GetValue(index)
    );
}


// --------------------------------------------------
// ToString
// --------------------------------------------------

std::string Tuple::ToString() const {

    std::ostringstream output;

    output << "{";

    output << std::setprecision(
        std::numeric_limits<double>::max_digits10
    );


    for (std::size_t i = 0; i < values_.size(); ++i) {

        if (i != 0) {
            output << ", ";
        }

        const Column& column =
            schema_.GetColumn(i);

        output << column.name << "=";


        const FieldValue& value =
            values_[i];


        if (std::holds_alternative<std::monostate>(value)) {

            output << "NULL";
        }
        else if (std::holds_alternative<std::int64_t>(value)) {

            output << std::get<std::int64_t>(value);
        }
        else if (std::holds_alternative<double>(value)) {

            output << std::get<double>(value);
        }
        else if (std::holds_alternative<std::string>(value)) {

            output << std::quoted(
                std::get<std::string>(value)
            );
        }
        else if (std::holds_alternative<bool>(value)) {

            output << (
                std::get<bool>(value) ? "true" : "false"
            );
        }
    }

    output << "}";

    return output.str();
}


// --------------------------------------------------
// Serialize
// --------------------------------------------------

std::vector<std::uint8_t> Tuple::Serialize() const {

    if (values_.size() >
        std::numeric_limits<std::uint16_t>::max()) {

        throw MiniDBException(
            "Tuple has too many fields to serialize"
        );
    }

    std::vector<std::uint8_t> output;

    output.reserve(kHeaderSize);

    // 1. Magic signature.
    output.push_back('T');
    output.push_back('U');
    output.push_back('P');
    output.push_back('L');

    // 2. Format version.
    output.push_back(kFormatVersion);

    // 3. Reserved byte.
    output.push_back(0);

    // 4. Number of fields.
    AppendLittleEndian(
        output,
        values_.size(),
        2
    );


    // 5. Encode every field in schema order.
    for (std::size_t i = 0; i < values_.size(); ++i) {

        EncodeValue(
            output,
            schema_.GetColumn(i),
            values_[i]
        );
    }

    return output;
}


// --------------------------------------------------
// Deserialize
// --------------------------------------------------

Tuple Tuple::Deserialize(
    const Schema& schema,
    const std::vector<std::uint8_t>& bytes
) {

    if (bytes.size() < kHeaderSize) {

        throw MiniDBException(
            "Binary tuple is smaller than its header"
        );
    }


    // 1. Verify magic signature.
    if (bytes[0] != 'T' ||
        bytes[1] != 'U' ||
        bytes[2] != 'P' ||
        bytes[3] != 'L') {

        throw MiniDBException(
            "Invalid tuple magic signature"
        );
    }


    // 2. Verify format version.
    if (bytes[4] != kFormatVersion) {

        throw MiniDBException(
            "Unsupported tuple format version"
        );
    }


    // 3. Verify reserved byte.
    if (bytes[5] != 0) {

        throw MiniDBException(
            "Invalid tuple reserved byte"
        );
    }


    // 4. Read the number of fields.
    std::size_t offset = 6;

    const std::uint64_t fieldCount =
        ReadLittleEndian(bytes, offset, 2);


    // The supplied schema must match the encoded tuple.
    if (fieldCount != schema.GetColumnCount()) {

        throw MiniDBException(
            "Tuple field count does not match supplied schema"
        );
    }


    std::vector<FieldValue> values;

    values.reserve(
        static_cast<std::size_t>(fieldCount)
    );


    // 5. Decode every field.
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(fieldCount);
         ++i) {

        values.push_back(
            DecodeValue(
                bytes,
                offset,
                schema.GetColumn(i)
            )
        );
    }


    // No trailing bytes are allowed.
    if (offset != bytes.size()) {

        throw MiniDBException(
            "Unexpected trailing bytes in binary tuple"
        );
    }


    // Constructor validates decoded values again.
    return Tuple(
        schema,
        std::move(values)
    );
}


} // namespace minidb
