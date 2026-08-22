#include "config.h"

#include "assets.h"
#include "fsutil.h"
#include "util.h"

namespace nashi {

namespace {

JValue ReadAll(const std::wstring& path) {
    std::string text;
    JValue cfg = JValue::makeObj();
    std::string err;
    if (ReadBinaryFile(path, text)) {
        JValue parsed;
        if (JsonParse(ToUtf8(text), parsed, err) && parsed.isObj()) cfg = parsed;
    }
    return cfg;
}

} // namespace

void Config::Init() {
    exeDir_ = ExeDir();
    projectsDir_ = PathJoin(exeDir_, L"projects");
    outputDir_ = PathJoin(exeDir_, L"output");
    path_ = PathJoin(exeDir_, L"nashi-studio.json");

    EnsureDir(projectsDir_);

    // 初回起動のときだけ、埋め込んだ見本を置きます
    if (ListFiles(projectsDir_, L"*.json").empty()) {
        std::string sample;
        if (LoadEmbedded(IDR_SAMPLE_PROJECT, sample)) {
            WriteBinaryFile(PathJoin(projectsDir_, L"なしちゃん.json"), sample);
        }
    }
}

JValue Config::Load() const { return ReadAll(path_); }

void Config::Save(const std::string& key, const JValue& value) {
    JValue cfg = ReadAll(path_);
    cfg.set(key, value);
    WriteBinaryFile(path_, cfg.dump(2));
}

std::string Config::DllBytes() const {
    // exe のとなりに nashi.dll があればそちらを優先（栞を作りなおしたときのため）
    std::string data;
    if (ReadBinaryFile(PathJoin(exeDir_, L"nashi.dll"), data) && !data.empty()) return data;
    if (LoadEmbedded(IDR_NASHI_DLL, data)) return data;
    return std::string();
}

} // namespace nashi
