#include "notify/xml.h"

#include <cstdlib>
#include <cstring>

namespace xml {

namespace {

const std::string kEmpty;

bool iequals(const std::string& a, const char* b) { return _stricmp(a.c_str(), b) == 0; }

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x110000) {
        out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
    }
}

std::string decode(const char* s, size_t n) {
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        const char* semi = (const char*)memchr(s + i, ';', n - i);
        if (!semi || semi - (s + i) > 10) { out += '&'; continue; }
        std::string ent(s + i + 1, semi);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#')
            appendUtf8(out, ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? strtoul(ent.c_str() + 2, nullptr, 16)
                                                                              : strtoul(ent.c_str() + 1, nullptr, 10));
        else { out += '&'; continue; }
        i = size_t(semi - s);
    }
    return out;
}

struct Parser {
    const std::string& s;
    size_t p = 0;

    bool eof() const { return p >= s.size(); }
    void skipSpace() { while (!eof() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p; }
    bool startsWith(const char* t) const { return s.compare(p, strlen(t), t) == 0; }
    bool skipPast(const char* t) {
        size_t q = s.find(t, p);
        if (q == std::string::npos) { p = s.size(); return false; }
        p = q + strlen(t);
        return true;
    }
    std::string name() {
        size_t b = p;
        while (!eof() && !strchr(" \t\r\n/>=", s[p])) ++p;
        return s.substr(b, p - b);
    }

    // Skips prolog, comments and processing instructions before an element.
    void skipMisc() {
        for (;;) {
            skipSpace();
            if (startsWith("<?")) skipPast("?>");
            else if (startsWith("<!--")) skipPast("-->");
            else if (startsWith("<!")) skipPast(">");
            else return;
        }
    }

    std::unique_ptr<Node> element(int depth) {
        if (depth > 64 || eof() || s[p] != '<') return nullptr;
        ++p;
        auto n = std::make_unique<Node>();
        n->name = name();
        if (n->name.empty()) return nullptr;
        for (;;) {
            skipSpace();
            if (eof()) return nullptr;
            if (s[p] == '/') {
                if (!startsWith("/>")) return nullptr;
                p += 2;
                return n;
            }
            if (s[p] == '>') { ++p; break; }
            std::string key = name();
            if (key.empty()) return nullptr;
            skipSpace();
            if (eof() || s[p] != '=') return nullptr;
            ++p;
            skipSpace();
            if (eof() || (s[p] != '"' && s[p] != '\'')) return nullptr;
            char q = s[p++];
            size_t e = s.find(q, p);
            if (e == std::string::npos) return nullptr;
            n->attrs.emplace_back(std::move(key), decode(s.data() + p, e - p));
            p = e + 1;
        }
        // Content.
        for (;;) {
            if (eof()) return nullptr;
            if (startsWith("</")) {
                p += 2;
                name();
                skipPast(">");
                return n;
            }
            if (startsWith("<!--")) { skipPast("-->"); continue; }
            if (startsWith("<![CDATA[")) {
                size_t b = p + 9, e = s.find("]]>", b);
                if (e == std::string::npos) return nullptr;
                auto t = std::make_unique<Node>();
                t->text = s.substr(b, e - b);
                n->children.push_back(std::move(t));
                p = e + 3;
                continue;
            }
            if (s[p] == '<') {
                if (startsWith("<?")) { skipPast("?>"); continue; }
                auto c = element(depth + 1);
                if (!c) return nullptr;
                n->children.push_back(std::move(c));
                continue;
            }
            size_t e = s.find('<', p);
            if (e == std::string::npos) return nullptr;
            auto t = std::make_unique<Node>();
            t->text = decode(s.data() + p, e - p);
            n->children.push_back(std::move(t));
            p = e;
        }
    }
};

} // namespace

const std::string& Node::attr(const char* key) const {
    for (auto& a : attrs)
        if (iequals(a.first, key)) return a.second;
    return kEmpty;
}

bool Node::hasAttr(const char* key) const {
    for (auto& a : attrs)
        if (iequals(a.first, key)) return true;
    return false;
}

std::string Node::innerText() const {
    std::string t;
    for (auto& c : children)
        if (c->isText()) t += c->text;
    return t;
}

const Node* Node::child(const char* n) const {
    for (auto& c : children)
        if (!c->isText() && iequals(c->name, n)) return c.get();
    return nullptr;
}

std::unique_ptr<Node> parse(const std::string& text) {
    Parser p{text};
    // UTF-8 byte order mark.
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) p.p = 3;
    p.skipMisc();
    return p.element(0);
}

} // namespace xml
