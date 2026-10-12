#pragma once
// Minimal XML DOM, enough for toast payloads: elements, attributes, text,
// entities. No namespaces, DTD or CDATA (toasts never use them).
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xml {

struct Node {
    std::string name;                                       // empty for text nodes
    std::string text;                                       // text nodes only
    std::vector<std::pair<std::string, std::string>> attrs; // in document order
    std::vector<std::unique_ptr<Node>> children;

    bool isText() const { return name.empty(); }
    const std::string& attr(const char* key) const; // "" if missing
    bool hasAttr(const char* key) const;
    // Concatenated text of the direct text children.
    std::string innerText() const;
    const Node* child(const char* name) const; // first child element with this name (case-insensitive)
};

// Returns the root element, or null on a malformed document.
std::unique_ptr<Node> parse(const std::string& text);

} // namespace xml
