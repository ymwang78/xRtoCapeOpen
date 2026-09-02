// ***************************************************************
//  XOptModelDesc   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "XOptModelDesc.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <memory>
#include <sstream>
#include <system_error>

namespace {

// ***************************************************************
//  最小 JSON 解析器
//  -------------------------------------------------------------
//  刻意不引第三方 JSON 库：本仓库是单仓的一个切片，除 xOpt 的 4 个头文件外
//  不依赖仓库其余部分（AGENTS.md），为读一个五键的描述文件把那条边界撑开
//  不划算。语法覆盖 RFC 8259 的全部（含 \u 转义与代理对），只是对用不到的
//  部分（超深嵌套、大整数精度）不做特别处理。
// ***************************************************************

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;  // 保序

struct JsonValue {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
    Type type = NUL;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    // shared_ptr 而非直接内嵌容器：JsonValue 在此处还是不完整类型，
    // vector<JsonValue> 作为成员只在 C++17 起有条件合法，间接一层免掉这条讨论。
    std::shared_ptr<JsonArray> arr;
    std::shared_ptr<JsonObject> obj;

    const JsonValue* find(const std::string& key) const {
        if (type != OBJ || !obj) return nullptr;
        for (const auto& kv : *obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
};

class JsonParser {
  public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    bool parse(JsonValue& out) {
        skipWs();
        if (!parseValue(out, 0)) return false;
        skipWs();
        if (i_ != s_.size()) return err("trailing characters after the top-level value");
        return true;
    }

    const std::string& error() const { return error_; }

  private:
    static const int kMaxDepth = 32;

    bool err(const std::string& what) {
        // 只留第一条：后面的都是它的余波，报最深的那条反而离病灶最远。
        if (error_.empty()) error_ = what + " at offset " + std::to_string(i_);
        return false;
    }

    void skipWs() {
        while (i_ < s_.size() &&
               (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) {
            ++i_;
        }
    }

    bool lit(const char* text) {
        const size_t n = std::strlen(text);
        if (i_ + n > s_.size() || s_.compare(i_, n, text) != 0) return false;
        i_ += n;
        return true;
    }

    static void appendUtf8(unsigned cp, std::string& out) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parseHex4(unsigned& out) {
        if (i_ + 4 > s_.size()) return err("truncated unicode escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_ + k];
            v <<= 4;
            if (c >= '0' && c <= '9') {
                v |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                v |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                v |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return err("non-hex digit in unicode escape");
            }
        }
        i_ += 4;
        out = v;
        return true;
    }

    bool parseString(std::string& out) {
        skipWs();
        if (i_ >= s_.size() || s_[i_] != '"') return err("expected a string");
        ++i_;
        out.clear();
        while (i_ < s_.size()) {
            const unsigned char c = static_cast<unsigned char>(s_[i_]);
            if (c == '"') {
                ++i_;
                return true;
            }
            if (c == '\\') {
                ++i_;
                if (i_ >= s_.size()) break;
                const char e = s_[i_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        unsigned cp = 0;
                        if (!parseHex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {  // 高代理，必须成对
                            if (i_ + 1 >= s_.size() || s_[i_] != '\\' || s_[i_ + 1] != 'u') {
                                return err("lone high surrogate in unicode escape");
                            }
                            i_ += 2;
                            unsigned lo = 0;
                            if (!parseHex4(lo)) return false;
                            if (lo < 0xDC00 || lo > 0xDFFF) {
                                return err("bad low surrogate in unicode escape");
                            }
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            return err("lone low surrogate in unicode escape");
                        }
                        appendUtf8(cp, out);
                        break;
                    }
                    default: return err("unknown escape sequence");
                }
                continue;
            }
            if (c < 0x20) return err("raw control character in string");
            out.push_back(static_cast<char>(c));
            ++i_;
        }
        return err("unterminated string");
    }

    bool parseNumber(double& out) {
        const size_t start = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' ||
                c == '-') {
                ++i_;
            } else {
                break;
            }
        }
        if (i_ == start) return err("expected a value");
        // 走 classic locale 而不是 strtod：strtod 认当前 locale 的小数点，
        // 在 de_DE 之类的环境里 "0.5" 会被读成 0。这是个服务进程，locale 由
        // 宿主决定，我们说了不算，所以不能靠它是 "C"。
        std::istringstream is(s_.substr(start, i_ - start));
        is.imbue(std::locale::classic());
        double v = 0.0;
        is >> v;
        if (is.fail() || is.peek() != std::char_traits<char>::eof()) {
            i_ = start;
            return err("malformed number");
        }
        out = v;
        return true;
    }

    bool parseValue(JsonValue& v, int depth) {
        if (depth > kMaxDepth) return err("nesting too deep");
        skipWs();
        if (i_ >= s_.size()) return err("unexpected end of input");
        const char c = s_[i_];
        if (c == '{') {
            ++i_;
            v.type = JsonValue::OBJ;
            v.obj = std::make_shared<JsonObject>();
            skipWs();
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return true;
            }
            for (;;) {
                std::string key;
                if (!parseString(key)) return false;
                skipWs();
                if (i_ >= s_.size() || s_[i_] != ':') return err("expected a colon");
                ++i_;
                JsonValue child;
                if (!parseValue(child, depth + 1)) return false;
                v.obj->emplace_back(std::move(key), std::move(child));
                skipWs();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == '}') {
                    ++i_;
                    return true;
                }
                return err("expected a comma or a closing brace");
            }
        }
        if (c == '[') {
            ++i_;
            v.type = JsonValue::ARR;
            v.arr = std::make_shared<JsonArray>();
            skipWs();
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return true;
            }
            for (;;) {
                JsonValue child;
                if (!parseValue(child, depth + 1)) return false;
                v.arr->push_back(std::move(child));
                skipWs();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == ']') {
                    ++i_;
                    return true;
                }
                return err("expected a comma or a closing bracket");
            }
        }
        if (c == '"') {
            v.type = JsonValue::STR;
            return parseString(v.str);
        }
        if (lit("true")) {
            v.type = JsonValue::BOOL;
            v.boolean = true;
            return true;
        }
        if (lit("false")) {
            v.type = JsonValue::BOOL;
            v.boolean = false;
            return true;
        }
        if (lit("null")) {
            v.type = JsonValue::NUL;
            return true;
        }
        v.type = JsonValue::NUM;
        return parseNumber(v.number);
    }

    const std::string& s_;
    size_t i_ = 0;
    std::string error_;
};

// —— JSON -> XOptModelDesc 的取值助手 ——

bool takeString(const JsonValue& v, const char* where, std::string& out, std::string& error) {
    if (v.type != JsonValue::STR) {
        error = std::string(where) + " must be a string";
        return false;
    }
    out = v.str;
    return true;
}

bool takeStringArray(const JsonValue& v, const char* where, std::vector<std::string>& out,
                     std::string& error) {
    if (v.type != JsonValue::ARR || !v.arr) {
        error = std::string(where) + " must be an array of strings";
        return false;
    }
    out.clear();
    for (const JsonValue& item : *v.arr) {
        if (item.type != JsonValue::STR) {
            error = std::string(where) + " must be an array of strings";
            return false;
        }
        out.push_back(item.str);
    }
    return true;
}

// 从端口映射里推组分表：流股变量名形如 "fi_<组分>"（examples/Readme.md §6.1
// 的约定），按首次出现顺序去重。取的是 value（流股侧）而不是 key——模型内部
// 变量怎么命名是模型的自由，只有流股侧的名字是平台约定过的。
void collectComponentsFromPorts(const JsonValue& ports, std::vector<std::string>& out) {
    if (ports.type != JsonValue::ARR || !ports.arr) return;
    for (const JsonValue& port : *ports.arr) {
        if (port.type != JsonValue::OBJ || !port.obj) continue;
        for (const auto& kv : *port.obj) {
            if (kv.second.type != JsonValue::STR) continue;
            const std::string& stream_name = kv.second.str;
            if (stream_name.size() <= 3 || stream_name.compare(0, 3, "fi_") != 0) continue;
            const std::string comp = stream_name.substr(3);
            if (std::find(out.begin(), out.end(), comp) == out.end()) out.push_back(comp);
        }
    }
}

}  // namespace

bool XOptModelDesc::parse(const std::string& json_text, XOptModelDesc& out, std::string& error) {
    JsonParser parser(json_text);
    JsonValue root;
    if (!parser.parse(root)) {
        error = "malformed JSON: " + parser.error();
        return false;
    }
    if (root.type != JsonValue::OBJ) {
        error = "the top-level JSON value must be an object";
        return false;
    }

    XOptModelDesc d;

    if (const JsonValue* p = root.find("parameters")) {
        if (p->type != JsonValue::OBJ || !p->obj) {
            error = "\"parameters\" must be an object";
            return false;
        }
        for (const auto& kv : *p->obj) {
            if (kv.second.type != JsonValue::NUM) {
                error = "\"parameters\"/\"" + kv.first + "\" must be a number";
                return false;
            }
            d.parameters.emplace_back(kv.first, kv.second.number);
        }
    }

    if (const JsonValue* f = root.find("fixable_variables")) {
        if (!takeStringArray(*f, "\"fixable_variables\"", d.fixable_variables, error)) return false;
    }

    if (const JsonValue* f = root.find("fixed_values")) {
        if (f->type != JsonValue::OBJ || !f->obj) {
            error = "\"fixed_values\" must be an object";
            return false;
        }
        for (const auto& kv : *f->obj) {
            if (kv.second.type != JsonValue::NUM) {
                error = "\"fixed_values\"/\"" + kv.first + "\" must be a number";
                return false;
            }
            d.fixed_values[kv.first] = kv.second.number;
        }
    }

    if (const JsonValue* s = root.find("slate")) {
        if (s->type != JsonValue::OBJ) {
            error = "\"slate\" must be an object";
            return false;
        }
        if (const JsonValue* n = s->find("name")) {
            if (!takeString(*n, "\"slate\"/\"name\"", d.slate_name, error)) return false;
        }
        if (const JsonValue* t = s->find("thermo_method")) {
            if (!takeString(*t, "\"slate\"/\"thermo_method\"", d.thermo_method, error)) return false;
        }
        if (const JsonValue* c = s->find("components")) {
            if (!takeStringArray(*c, "\"slate\"/\"components\"", d.components, error)) return false;
        }
    }
    if (d.components.empty()) {
        if (const JsonValue* c = root.find("components")) {
            if (!takeStringArray(*c, "\"components\"", d.components, error)) return false;
        }
    }
    // 显式组分表优先，没有才从端口映射推。两者都写了而且不一致时以显式的为准，
    // 是唯一说得清的规则——端口里的 fi_ 条目本来就是随 slate 走的。
    if (d.components.empty()) {
        if (const JsonValue* p = root.find("inports")) collectComponentsFromPorts(*p, d.components);
        if (const JsonValue* p = root.find("outports")) collectComponentsFromPorts(*p, d.components);
    }

    out = std::move(d);
    return true;
}

bool XOptModelDesc::load(const std::string& path, XOptModelDesc& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open model description '" + path + "'";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // UTF-8 BOM：本仓库的源文件按规范都带 BOM，描述文件被同一批编辑器另存时
    // 也常带上。不吃掉它，解析器会在第 0 个字节上报 "expected a value"，
    // 而那条消息对着的是一个肉眼看起来完全正常的文件。
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (!parse(text, out, error)) {
        error = "'" + path + "': " + error;
        return false;
    }
    return true;
}

bool XOptModelDesc::discover(const std::string& dll_path, std::string& path_out,
                             std::string& error) {
    namespace fs = std::filesystem;
    path_out.clear();
    error.clear();
    if (dll_path.empty()) return true;

    std::error_code ec;
    const fs::path dll(dll_path);
    const fs::path dir = dll.has_parent_path() ? dll.parent_path() : fs::path(".");
    if (!fs::is_directory(dir, ec) || ec) return true;  // 没有目录可扫不是错误

    static const std::string kSuffix = "_Model.json";
    std::vector<std::string> hits;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec) || fec) continue;
        const std::string name = it->path().filename().string();
        if (name.size() > kSuffix.size() &&
            name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
            hits.push_back(it->path().string());
        }
    }
    if (hits.empty()) return true;
    if (hits.size() > 1) {
        std::sort(hits.begin(), hits.end());  // 报错信息要可复现
        std::string list;
        for (const std::string& h : hits) list += (list.empty() ? "" : ", ") + h;
        error = "several *_Model.json sit next to '" + dll_path + "' (" + list +
                "); pass the one to use explicitly";
        return false;
    }
    path_out = hits.front();
    return true;
}
