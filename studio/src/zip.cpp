#include "zip.h"
#include "deflate.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nashi {

namespace {

void PutU16(std::string& s, unsigned v) {
    s += (char)(unsigned char)(v & 0xFF);
    s += (char)(unsigned char)((v >> 8) & 0xFF);
}

void PutU32(std::string& s, unsigned v) {
    s += (char)(unsigned char)(v & 0xFF);
    s += (char)(unsigned char)((v >> 8) & 0xFF);
    s += (char)(unsigned char)((v >> 16) & 0xFF);
    s += (char)(unsigned char)((v >> 24) & 0xFF);
}

} // namespace

std::string CreateZip(const std::vector<ZipEntry>& entries) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    unsigned dosTime = ((unsigned)st.wHour << 11) | ((unsigned)st.wMinute << 5) | (st.wSecond / 2);
    unsigned year = st.wYear < 1980 ? 0 : (unsigned)(st.wYear - 1980);
    unsigned dosDate = (year << 9) | ((unsigned)st.wMonth << 5) | (unsigned)st.wDay;

    std::string locals, centrals;
    unsigned offset = 0;

    for (size_t i = 0; i < entries.size(); i++) {
        std::string name = entries[i].name;
        for (size_t k = 0; k < name.size(); k++) {
            if (name[k] == '\\') name[k] = '/';
        }
        const std::string& data = entries[i].data;
        std::string deflated = DeflateRaw((const unsigned char*)data.c_str(), data.size());
        bool store = deflated.size() >= data.size();
        const std::string& body = store ? data : deflated;
        unsigned method = store ? 0 : 8;
        unsigned crc = Crc32((const unsigned char*)data.c_str(), data.size());

        std::string local;
        PutU32(local, 0x04034B50);
        PutU16(local, 20);
        PutU16(local, 0x0800);          // ファイル名は UTF-8
        PutU16(local, method);
        PutU16(local, dosTime);
        PutU16(local, dosDate);
        PutU32(local, crc);
        PutU32(local, (unsigned)body.size());
        PutU32(local, (unsigned)data.size());
        PutU16(local, (unsigned)name.size());
        PutU16(local, 0);
        locals += local;
        locals += name;
        locals += body;

        std::string central;
        PutU32(central, 0x02014B50);
        PutU16(central, 20);
        PutU16(central, 20);
        PutU16(central, 0x0800);
        PutU16(central, method);
        PutU16(central, dosTime);
        PutU16(central, dosDate);
        PutU32(central, crc);
        PutU32(central, (unsigned)body.size());
        PutU32(central, (unsigned)data.size());
        PutU16(central, (unsigned)name.size());
        PutU16(central, 0);             // extra
        PutU16(central, 0);             // comment
        PutU16(central, 0);             // disk
        PutU16(central, 0);             // internal attrs
        PutU32(central, 0);             // external attrs
        PutU32(central, offset);
        centrals += central;
        centrals += name;

        offset += (unsigned)(local.size() + name.size() + body.size());
    }

    std::string end;
    PutU32(end, 0x06054B50);
    PutU16(end, 0);
    PutU16(end, 0);
    PutU16(end, (unsigned)entries.size());
    PutU16(end, (unsigned)entries.size());
    PutU32(end, (unsigned)centrals.size());
    PutU32(end, offset);
    PutU16(end, 0);

    return locals + centrals + end;
}

} // namespace nashi
