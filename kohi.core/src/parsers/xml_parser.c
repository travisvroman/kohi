#include "xml_parser.h"

#include "memory/kmemory.h"
#include "strings/kstring.h"

static char* skip_whitespace(char* str);
static xml_attribute* parse_attribute(char** ptr);
static xml_node* parse_node(char** input);

xml_node* xml_parse(const char* source_str) {
    if (!source_str) {
        return 0;
    }

    char* ptr = (char*)source_str;
    return parse_node(&ptr);
}

void xml_free(xml_node* node) {
    if (!node) {
        return;
    }

    if (node->tag) {
        string_free(node->tag);
    }

    if (node->content) {
        string_free(node->content);
    }

    xml_attribute* attr = node->attributes;
    while (attr) {
        xml_attribute* next = attr->next;
        string_free(attr->key);
        string_free(attr->value);
        KFREE_TYPE(attr, xml_attribute, MEMORY_TAG_XML);
        attr = next;
    }

    xml_node* child = node->children;
    while (child) {
        xml_node* next = child->next;
        xml_free(child);
        child = next;
    }

    KFREE_TYPE(node, xml_node, MEMORY_TAG_XML);
}

const xml_node* xml_child_find(const xml_node* parent, const char* tag) {
    if (!parent) {
        return 0;
    }

    xml_node* child = parent->children;
    while (child) {
        if (strings_equal(child->tag, tag)) {
            return child;
        }
        child = child->next;
    }

    return 0;
}

const char* xml_content_get(const xml_node* node) {
    return node ? node->content : 0;
}

const char* xml_attribute_get(const xml_node* node, const char* key) {
    if (!node) {
        return 0;
    }

    xml_attribute* attr = node->attributes;
    while (attr) {
        if (strings_equal(attr->key, key)) {
            return attr->value;
        }
        attr = attr->next;
    }

    return 0;
}

static char* skip_whitespace(char* str) {
    while (*str && char_is_whitespace(*str)) {
        ++str;
    }

    return str;
}

static xml_attribute* parse_attribute(char** ptr) {
    *ptr = skip_whitespace(*ptr);

    // Extract key/name
    char key[256];
    i32 i = 0;
    while (**ptr && **ptr != '=' && !char_is_whitespace(**ptr)) {
        key[i++] = **ptr;
        (*ptr)++;
    }
    key[i] = 0;

    *ptr = skip_whitespace(*ptr);
    if (**ptr != '=') {
        // Malformed attribute
        return 0;
    }
    (*ptr)++;

    *ptr = skip_whitespace(*ptr);
    if (**ptr != '\"') {
        // Malformed attribute
        return 0;
    }
    (*ptr)++;

    // Extract value
    char value[32768];
    i = 0;
    while (**ptr && **ptr != '\"') {
        value[i++] = **ptr;
        (*ptr)++;
    }
    value[i] = 0;

    // Skip closing quote
    if (**ptr == '\"') {
        (*ptr)++;
    }

    xml_attribute* attr = KALLOC_TYPE(xml_attribute, MEMORY_TAG_XML);
    attr->key = string_duplicate(key);
    attr->value = string_duplicate(value);
    attr->next = 0;

    return attr;
}

static xml_node* parse_node(char** input) {
    char* ptr = *input;
    ptr = skip_whitespace(ptr);

    if (*ptr != '<') {
        return 0;
    }
    ptr++; // skip '<'

    // Extract tag name
    char tag[256];
    i32 i = 0;
    while (*ptr && *ptr != '>' && !char_is_whitespace(*ptr)) {
        tag[i++] = *ptr++;
    }
    tag[i] = 0;

    // Initialize the node.
    xml_node* node = KALLOC_TYPE(xml_node, MEMORY_TAG_XML);
    node->tag = string_duplicate(tag);
    node->content = 0;
    node->children = 0;
    node->next = 0;
    node->attributes = 0;

    // Parse attributes
    xml_attribute* last_attr = 0;
    while (*ptr && *ptr != '>' && *ptr != '/') {
        xml_attribute* attr = parse_attribute(&ptr);
        if (attr) {
            if (!node->attributes) {
                node->attributes = attr;
            } else {
                last_attr->next = attr;
            }
            last_attr = attr;
        }
        ptr = skip_whitespace(ptr);
    }

    // Slip closing '>'
    if (*ptr == '>') {
        ptr++;
    }

    // Check from self-closing tag
    if (*(ptr - 2) == '/') {
        *input = ptr;
        return node;
    }

    // Extract content or children.
    ptr = skip_whitespace(ptr);
    if (*ptr == '<' && *(ptr + 1) != '/') {
        // Child nodes
        xml_node* last_child = 0;
        while (*ptr == '<' && *(ptr + 1) != '/') {
            xml_node* child = parse_node(&ptr);
            if (child) {
                if (!node->children) {
                    node->children = child;
                } else {
                    last_child->next = child;
                }
                last_child = child;
            }
            ptr = skip_whitespace(ptr);
        }
    } else {
        // Text content
        char content[1024];
        i = 0;
        while (*ptr && *ptr != '<') {
            content[i++] = *ptr++;
        }
        content[i] = 0;
        node->content = string_duplicate(content);
    }

    // Skip closing tag
    if (*ptr == '<' && *(ptr + 1) == '/') {
        ptr += 2;
        while (*ptr && *ptr != '>') {
            ptr++;
        }
        if (*ptr == '>') {
            ptr++;
        }
    }

    *input = ptr;
    return node;
}
