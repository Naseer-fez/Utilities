#include <windows.h>
#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

enum class JsonTokenType {
    BeginObject, // {
    EndObject,   // }
    BeginArray,  // [
    EndArray,    // ]
    Colon,       // :
    Comma,       // ,
    String,
    Number,
    Boolean,
    Null,
    End
};

struct JsonToken {
    JsonTokenType type;
    std::wstring value;
};

class JsonTokenizer {
    std::wstring m_json;
    size_t m_pos;
public:
    JsonTokenizer(const std::wstring& json) : m_json(json), m_pos(0) {}
    
    JsonToken nextToken() {
        skipWhitespace();
        if (m_pos >= m_json.length()) {
            return { JsonTokenType::End, L"" };
        }
        
        wchar_t c = m_json[m_pos];
        if (c == L'{') { m_pos++; return { JsonTokenType::BeginObject, L"{" }; }
        if (c == L'}') { m_pos++; return { JsonTokenType::EndObject, L"}" }; }
        if (c == L'[') { m_pos++; return { JsonTokenType::BeginArray, L"[" }; }
        if (c == L']') { m_pos++; return { JsonTokenType::EndArray, L"]" }; }
        if (c == L':') { m_pos++; return { JsonTokenType::Colon, L":" }; }
        if (c == L',') { m_pos++; return { JsonTokenType::Comma, L"," }; }
        
        if (c == L'"') {
            return parseString();
        }
        
        if (iswdigit(c) || c == L'-') {
            return parseNumber();
        }
        
        if (m_json.compare(m_pos, 4, L"true") == 0) { m_pos += 4; return { JsonTokenType::Boolean, L"true" }; }
        if (m_json.compare(m_pos, 5, L"false") == 0) { m_pos += 5; return { JsonTokenType::Boolean, L"false" }; }
        if (m_json.compare(m_pos, 4, L"null") == 0) { m_pos += 4; return { JsonTokenType::Null, L"null" }; }
        
        m_pos++;
        return nextToken();
    }
    
private:
    void skipWhitespace() {
        while (m_pos < m_json.length() && iswspace(m_json[m_pos])) {
            m_pos++;
        }
    }
    
    JsonToken parseString() {
        m_pos++; // skip initial quote
        std::wstring str;
        while (m_pos < m_json.length()) {
            wchar_t c = m_json[m_pos];
            if (c == L'"') {
                m_pos++; // skip end quote
                return { JsonTokenType::String, str };
            }
            if (c == L'\\' && m_pos + 1 < m_json.length()) {
                wchar_t next = m_json[m_pos + 1];
                if (next == L'"' || next == L'\\' || next == L'/') {
                    str += next;
                } else if (next == L'b') str += L'\b';
                else if (next == L'f') str += L'\f';
                else if (next == L'n') str += L'\n';
                else if (next == L'r') str += L'\r';
                else if (next == L't') str += L'\t';
                else {
                    str += c;
                    str += next;
                }
                m_pos += 2;
            } else {
                str += c;
                m_pos++;
            }
        }
        return { JsonTokenType::String, str };
    }
    
    JsonToken parseNumber() {
        size_t start = m_pos;
        if (m_json[m_pos] == L'-') m_pos++;
        while (m_pos < m_json.length() && iswdigit(m_json[m_pos])) {
            m_pos++;
        }
        if (m_pos < m_json.length() && m_json[m_pos] == L'.') {
            m_pos++;
            while (m_pos < m_json.length() && iswdigit(m_json[m_pos])) {
                m_pos++;
            }
        }
        return { JsonTokenType::Number, m_json.substr(start, m_pos - start) };
    }
};

struct SimpleJsonValue {
    enum class Type { Object, Array, String, Number, Boolean, Null } type = Type::Null;
    std::wstring stringVal;
    double numberVal = 0.0;
    bool boolVal = false;
    std::vector<std::pair<std::wstring, SimpleJsonValue>> objectVal;
    std::vector<SimpleJsonValue> arrayVal;
    
    const SimpleJsonValue& operator[](const std::wstring& key) const {
        static SimpleJsonValue nullVal;
        if (type != Type::Object) return nullVal;
        for (const auto& pair : objectVal) {
            if (pair.first == key) return pair.second;
        }
        return nullVal;
    }
    
    size_t size() const {
        if (type == Type::Array) return arrayVal.size();
        return 0;
    }
    
    const SimpleJsonValue& operator[](size_t index) const {
        static SimpleJsonValue nullVal;
        if (type != Type::Array || index >= arrayVal.size()) return nullVal;
        return arrayVal[index];
    }
};

class SimpleJsonParser {
    JsonTokenizer m_tokenizer;
    JsonToken m_currentToken;
    
    void next() {
        m_currentToken = m_tokenizer.nextToken();
    }
    
public:
    SimpleJsonParser(const std::wstring& json) : m_tokenizer(json) {
        next();
    }
    
    SimpleJsonValue parseValue() {
        SimpleJsonValue val;
        if (m_currentToken.type == JsonTokenType::String) {
            val.type = SimpleJsonValue::Type::String;
            val.stringVal = m_currentToken.value;
            next();
        } else if (m_currentToken.type == JsonTokenType::Number) {
            val.type = SimpleJsonValue::Type::Number;
            try {
                val.numberVal = std::stod(m_currentToken.value);
            } catch (...) {
                val.numberVal = 0.0;
            }
            next();
        } else if (m_currentToken.type == JsonTokenType::Boolean) {
            val.type = SimpleJsonValue::Type::Boolean;
            val.boolVal = (m_currentToken.value == L"true");
            next();
        } else if (m_currentToken.type == JsonTokenType::Null) {
            val.type = SimpleJsonValue::Type::Null;
            next();
        } else if (m_currentToken.type == JsonTokenType::BeginObject) {
            val = parseObject();
        } else if (m_currentToken.type == JsonTokenType::BeginArray) {
            val = parseArray();
        }
        return val;
    }
    
private:
    SimpleJsonValue parseObject() {
        SimpleJsonValue val;
        val.type = SimpleJsonValue::Type::Object;
        next(); // skip '{'
        while (m_currentToken.type != JsonTokenType::EndObject && m_currentToken.type != JsonTokenType::End) {
            if (m_currentToken.type != JsonTokenType::String) {
                next();
                continue;
            }
            std::wstring key = m_currentToken.value;
            next();
            if (m_currentToken.type == JsonTokenType::Colon) {
                next();
            }
            SimpleJsonValue subVal = parseValue();
            val.objectVal.push_back({key, subVal});
            if (m_currentToken.type == JsonTokenType::Comma) {
                next();
            }
        }
        if (m_currentToken.type == JsonTokenType::EndObject) {
            next();
        }
        return val;
    }
    
    SimpleJsonValue parseArray() {
        SimpleJsonValue val;
        val.type = SimpleJsonValue::Type::Array;
        next(); // skip '['
        while (m_currentToken.type != JsonTokenType::EndArray && m_currentToken.type != JsonTokenType::End) {
            SimpleJsonValue subVal = parseValue();
            val.arrayVal.push_back(subVal);
            if (m_currentToken.type == JsonTokenType::Comma) {
                next();
            }
        }
        if (m_currentToken.type == JsonTokenType::EndArray) {
            next();
        }
        return val;
    }
};

std::vector<Profile> loadProfiles(const std::wstring& configPath) {
    std::vector<Profile> profiles;
    
    std::ifstream f(configPath.c_str(), std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return profiles;
    }
    
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string utf8Str = buffer.str();
    f.close();
    
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, NULL, 0);
    if (wlen <= 0) return profiles;
    std::wstring json(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &json[0], wlen);
    
    if (!json.empty() && json.back() == L'\0') {
        json.pop_back();
    }
    
    SimpleJsonParser parser(json);
    SimpleJsonValue root = parser.parseValue();
    SimpleJsonValue profilesVal = root[L"profiles"];
    
    if (profilesVal.type == SimpleJsonValue::Type::Array) {
        for (size_t i = 0; i < profilesVal.size(); ++i) {
            SimpleJsonValue pVal = profilesVal[i];
            Profile p;
            p.name = pVal[L"name"].stringVal;
            p.blockAllExceptAllowed = pVal[L"blockAllExceptAllowed"].boolVal;
            
            SimpleJsonValue allowed = pVal[L"allowedApps"];
            if (allowed.type == SimpleJsonValue::Type::Array) {
                for (size_t j = 0; j < allowed.size(); ++j) {
                    p.allowedApps.push_back(allowed[j].stringVal);
                }
            }
            
            SimpleJsonValue close = pVal[L"appsToClose"];
            if (close.type == SimpleJsonValue::Type::Array) {
                for (size_t j = 0; j < close.size(); ++j) {
                    p.appsToClose.push_back(close[j].stringVal);
                }
            }
            
            SimpleJsonValue launch = pVal[L"appsToLaunch"];
            if (launch.type == SimpleJsonValue::Type::Array) {
                for (size_t j = 0; j < launch.size(); ++j) {
                    p.appsToLaunch.push_back(launch[j].stringVal);
                }
            }
            
            p.wallpaperPath = pVal[L"wallpaperPath"].stringVal;
            p.volume = pVal[L"volume"].type == SimpleJsonValue::Type::Number ? static_cast<int>(pVal[L"volume"].numberVal) : -1;
            p.durationMinutes = pVal[L"durationMinutes"].type == SimpleJsonValue::Type::Number ? static_cast<int>(pVal[L"durationMinutes"].numberVal) : 25;
            
            if (!p.name.empty()) {
                profiles.push_back(p);
            }
        }
    }
    
    return profiles;
}

bool loadSmtpConfig(const std::wstring& configPath, SmtpConfig& smtp) {
    std::ifstream f(configPath.c_str(), std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string utf8Str = buffer.str();
    f.close();
    
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, NULL, 0);
    if (wlen <= 0) return false;
    std::wstring json(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &json[0], wlen);
    
    if (!json.empty() && json.back() == L'\0') {
        json.pop_back();
    }
    
    SimpleJsonParser parser(json);
    SimpleJsonValue root = parser.parseValue();
    SimpleJsonValue smtpVal = root[L"smtp"];
    
    if (smtpVal.type == SimpleJsonValue::Type::Object) {
        smtp.smtpUser = smtpVal[L"smtpUser"].stringVal;
        smtp.smtpPassword = smtpVal[L"smtpPassword"].stringVal;
        smtp.emailTo1 = smtpVal[L"emailTo1"].stringVal;
        smtp.emailTo2 = smtpVal[L"emailTo2"].stringVal;
        return (!smtp.smtpUser.empty() && !smtp.smtpPassword.empty());
    }
    
    return false;
}
